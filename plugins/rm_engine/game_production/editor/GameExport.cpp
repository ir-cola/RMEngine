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

bool MakeRequest(const GameExportInput& in, editor::exporter::Request& out, std::string* error) {
    auto fail = [&](std::string msg) {
        if (error) *error = std::move(msg);
        return false;
    };
    out = {};
    out.outDir = in.outDir;
    out.exeName = in.exeName;
    out.titleUtf8 = in.title;
    out.plugins = in.plugins;
    out.scriptsDll = in.scriptsDll;
    out.privatePaths = {PathToUtf8(in.projectRoot)};

    // The scenes, and every asset id they write (the prefabs they place bring theirs: followed until
    // nothing new comes).
    std::vector<std::string> ids;
    for (const auto& [path, text] : in.scenes) {
        if (!editor::exporter::PortablePath(path)) return fail(editor::TrU8F("シーンのファイルがプロジェクトの外にあります: {}", path));
        out.files.push_back({path, text});
        if (auto doc = json::Parse(text)) {
            for (const std::string& id : AssetIdsIn(*doc)) {
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            }
        }
    }
    if (!in.icon.Empty() && std::find(ids.begin(), ids.end(), in.icon.guid) == ids.end()) ids.push_back(in.icon.guid);
    json::Value assets = json::Value::MakeObject();
    std::unordered_map<std::string, std::string> taken;  // pak path -> its id (two ids never share a file)
    for (size_t i = 0; i < ids.size(); ++i) {
        const fs::path file = in.assetFile(AssetRef{ids[i]});
        if (file.empty()) continue;  // an id of something else (an entity's), or a file that is gone: the game draws without it
        const std::string rel = PathToUtf8(file.lexically_normal().lexically_relative(in.projectRoot.lexically_normal()).generic_wstring());
        if (!editor::exporter::PortablePath(rel)) continue;  // not in the project: never written into the game
        if (std::any_of(in.scenes.begin(), in.scenes.end(), [&](const auto& s) { return s.first == rel; })) {
            assets.Set(ids[i], rel);
            continue;  // a scene: already in (as it is now)
        }
        if (!taken.emplace(rel, ids[i]).second) continue;
        const auto bytes = ReadAll(file);
        if (!bytes) return fail(editor::TrU8F("アセットを読めません: {}", rel));
        assets.Set(ids[i], rel);
        out.files.push_back({rel, *bytes});
        if (ids[i] == in.icon.guid) out.iconPng = *bytes;
        // A prefab places more (its own assets, the prefabs inside it).
        if (fs::path(file).extension() == L".rmprefab") {
            if (auto doc = json::Parse(*bytes)) {
                for (const std::string& id : AssetIdsIn(*doc)) {
                    if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
                }
            }
        }
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
    game.Set("open", static_cast<int64_t>(in.open));
    game.Set("assets", std::move(assets));
    // Never a path of this PC in what the game reads.
    if (const auto abs = editor::exporter::AbsolutePathsIn(game); !abs.empty()) {
        return fail(editor::TrU8F("ゲームのデータにこの PC のパスが入っています: {}\n相対パスにしてから書き出してください。", abs.front()));
    }
    out.files.insert(out.files.begin(), {kGameDataPath, json::Write(game)});
    return true;
}

}  // namespace rm::game_production
