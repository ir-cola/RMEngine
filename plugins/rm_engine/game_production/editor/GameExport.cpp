#include "rm_engine/game_production/editor/GameExport.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "editor/export/ExportPlan.h"
#include "editor/lang/Lang.h"
#include "rm_engine/game_production/runtime/GamePlayer.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace rm::game_production {

namespace {

std::optional<std::string> ReadAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

ExportScenes ScenesForExport(const SceneList& list, const AssetRef& open, const std::string& openPath, const std::string& openText,
                             const std::function<std::optional<std::pair<std::string, std::string>>(const AssetRef&)>& read) {
    ExportScenes out;
    const AssetRef first = FirstScene(list);
    std::optional<size_t> firstAt, openAt;
    for (const AssetRef& ref : GameScenes(list, open)) {
        if (ref == open) {
            openAt = out.scenes.size();
            if (!first.Empty() && ref == first) firstAt = openAt;
            out.scenes.push_back({openPath, openText});
            continue;
        }
        auto file = read(ref);
        if (!file) {
            out.unread.push_back(ref.guid);
            continue;
        }
        if (ref == first) firstAt = out.scenes.size();
        out.scenes.push_back(std::move(*file));
    }
    out.first = firstAt ? *firstAt : openAt.value_or(0);
    return out;
}

std::vector<std::string> AssetIdsIn(const json::Value& v) {
    std::vector<std::string> out;
    auto visit = [&](auto&& self, const json::Value& x) -> void {
        if (x.IsString()) {
            if (asset::IsGuid(x.AsString()) && std::find(out.begin(), out.end(), x.AsString()) == out.end()) out.push_back(x.AsString());
        } else if (x.IsArray()) {
            for (const auto& i : x.Items()) self(self, i);
        } else if (x.IsObject()) {
            for (const auto& [k, i] : x.Members()) self(self, i);
        }
    };
    visit(visit, v);
    return out;
}

std::vector<ExportAsset> CollectAssets(const GameExportInput& in) {
    std::vector<std::string> ids;
    auto add = [&](const json::Value& doc) {
        for (const std::string& id : AssetIdsIn(doc)) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        }
    };
    for (const auto& s : in.scenes) {
        if (auto doc = json::Parse(s.second)) add(*doc);
    }
    std::vector<ExportAsset> out;
    for (size_t i = 0; i < ids.size(); ++i) {
        const fs::path file = in.assetFile(AssetRef{ids[i]});
        if (file.empty()) continue;  // an id of something else (an entity's), or a file that is gone: the game draws without it
        const std::string rel = PathToUtf8(file.lexically_normal().lexically_relative(in.projectRoot.lexically_normal()).generic_wstring());
        if (!editor::exporter::PortablePath(rel)) continue;  // not in the project: never written into the game
        if (std::any_of(out.begin(), out.end(), [&](const ExportAsset& a) { return a.rel == rel; })) continue;
        out.push_back({ids[i], file, rel, in.projectRoot});
        // A prefab places more (its own assets, the prefabs inside it); a file of another of the engine's
        // own kinds (.rm..., JSON) may name more too (a particle effect: its modules' .rmfx files).
        if (const std::wstring ext = file.extension().wstring(); ext.size() > 3 && ext.starts_with(L".rm")) {
            if (const auto bytes = ReadAll(file)) {
                if (auto doc = json::Parse(*bytes)) add(*doc);
            }
        }
    }
    return out;
}

bool MakeRequest(const GameExportInput& in, const std::vector<ExportAsset>& assets, const std::vector<const ExportBaker*>& bakers,
                 editor::exporter::Request& out, const std::function<void(const std::string&)>& note, std::string* error) {
    auto fail = [&](std::string msg) {
        if (error) *error = std::move(msg);
        return false;
    };
    out = {};
    out.outDir = in.outDir;
    out.exeName = in.exeName;
    out.titleUtf8 = in.title;
    out.plugins = in.plugins;
    // The scripts: built now from the sources as they are (never an older DLL).
    if (ScriptSourcesTime(in.scripts.project) != 0) {
        if (note) note(editor::TrU8("スクリプトをビルドしています (書き出し用。エンジンと同じ Release か Debug)"));
        ScriptBuildRequest r = in.scripts;
        r.forExport = true;
        const ScriptBuildResult built = BuildScripts(r);
        if (!built.ok) {
            std::string why = built.summary;
            for (const std::string& e : built.errors) why += "\n" + e;
            return fail(editor::TrU8F("スクリプトをビルドできないので書き出しません:\n{}", why));
        }
        out.scriptsDll = built.dll;
    }
    out.privatePaths = {PathToUtf8(in.projectRoot)};
    if (!in.iconFile.empty()) {
        if (auto bytes = ReadAll(in.iconFile)) out.iconPng = std::move(*bytes);
    }
    // The scenes, as they are now.
    for (const auto& [path, text] : in.scenes) {
        if (!editor::exporter::PortablePath(path)) return fail(editor::TrU8F("シーンのファイルがプロジェクトの外にあります: {}", path));
        out.files.push_back({path, text});
    }
    auto has = [&](const std::string& path) {
        return std::any_of(out.files.begin(), out.files.end(), [&](const pak::Entry& e) { return e.path == path; });
    };
    // The assets: baked into the game's own form by the first baker that takes each, else as their file.
    json::Value map = json::Value::MakeObject();
    for (const ExportAsset& a : assets) {
        if (std::any_of(in.scenes.begin(), in.scenes.end(), [&](const auto& s) { return s.first == a.rel; })) {
            map.Set(a.id, a.rel);  // a scene: already in (as it is now)
            continue;
        }
        bool baked = false;
        for (const ExportBaker* b : bakers) {
            ExportBaked made;
            std::string why;
            const BakeResult r = b->bake(a, made, &why);
            if (r == BakeResult::NotMine) continue;
            if (r == BakeResult::Failed) return fail(editor::TrU8F("「{}」を焼けませんでした: {}", a.rel, why));
            for (pak::Entry& e : made.files) {
                if (!has(e.path)) out.files.push_back(std::move(e));
            }
            map.Set(a.id, made.path);
            if (!made.note.empty() && note) note(made.note);
            baked = true;
            break;
        }
        if (baked) continue;
        const auto bytes = ReadAll(a.file);
        if (!bytes) return fail(editor::TrU8F("アセットを読めません: {}", a.rel));
        map.Set(a.id, a.rel);
        if (!has(a.rel)) out.files.push_back({a.rel, *bytes});
    }

    // game.json: what the game reads first (GamePlayer.h).
    json::Value game = json::Value::MakeObject();
    game.Set("kind", kGameDataKind);
    game.Set("format", kGameDataFormat);
    game.Set("title", in.title);
    game.Set("settings", in.settings);
    json::Value scenes = json::Value::MakeArray();
    for (const auto& s : in.scenes) scenes.Push(s.first);
    game.Set("scenes", std::move(scenes));
    game.Set("first", static_cast<int64_t>(in.first));
    game.Set("assets", std::move(map));
    // Never a path of this PC in what the game reads.
    if (const auto abs = editor::exporter::AbsolutePathsIn(game); !abs.empty()) {
        return fail(editor::TrU8F("ゲームのデータにこの PC のパスが入っています: {}\n相対パスにしてから書き出してください。", abs.front()));
    }
    out.files.insert(out.files.begin(), {kGameDataPath, json::Write(game)});
    return true;
}

}  // namespace rm::game_production
