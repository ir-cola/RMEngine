#include "rm_engine/game_production/runtime/SceneFile.h"

#include "core/asset/AssetIndex.h"

#include <algorithm>
#include <format>

namespace rm::game_production {

AssetRef OpenScene(const SceneList& list) {
    for (const AssetRef& s : list.scenes) {
        if (!list.open.Empty() && s == list.open) return s;
    }
    for (const AssetRef& s : list.scenes) {
        if (!s.Empty()) return s;
    }
    return {};
}

AssetRef FirstScene(const SceneList& list) {
    for (const AssetRef& s : list.scenes) {
        if (!list.first.Empty() && s == list.first) return s;
    }
    for (const AssetRef& s : list.scenes) {
        if (!s.Empty()) return s;
    }
    return {};
}

std::vector<AssetRef> GameScenes(const SceneList& list, const AssetRef& open) {
    std::vector<AssetRef> out;
    for (const AssetRef& s : list.scenes) {
        if (!s.Empty() && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
    if (open.Empty() || std::find(out.begin(), out.end(), open) == out.end()) out.push_back(open);
    return out;
}

std::string SceneNameOf(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (name.size() > std::string_view(kSceneExtension).size() && name.ends_with(kSceneExtension)) {
        name.remove_suffix(std::string_view(kSceneExtension).size());
    }
    return std::string(name);
}

std::optional<size_t> FindSceneByName(const std::vector<std::string>& names, std::string_view name) {
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return i;
    }
    return std::nullopt;
}

std::string SceneFileText(const json::Value& scene) {
    json::Value doc = json::Value::MakeObject();
    doc.Set("kind", kSceneKind);
    doc.Set("format", kSceneFormat);
    if (scene.IsObject()) {
        for (const auto& [key, value] : scene.Members()) {
            if (key != "kind" && key != "format") doc.Set(key, value);
        }
    }
    return json::Write(doc) + "\n";
}

std::optional<json::Value> ReadSceneFile(std::string_view text, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<json::Value> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    json::ParseError pe;
    auto doc = json::Parse(text, &pe);
    if (!doc) return fail(std::format("JSON が壊れています ({} 行 {} 文字目: {})", pe.line, pe.column, pe.message));
    if (!doc->IsObject() || doc->Get("kind").AsString() != kSceneKind) return fail("シーンのファイルではありません (kind)");
    const int64_t format = doc->Get("format").AsInt(0);
    if (format > kSceneFormat) {
        return fail(std::format("新しい版のエンジンで作られたシーンです (形式 {} / この版は {} まで)。読まず、上書きもしません", format, kSceneFormat));
    }
    return doc;
}

uint64_t TextSign(std::string_view text) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h != 0 ? h : 1;
}

std::optional<json::Value> TakeOldScene(json::Value& data) {
    if (!data.IsObject() || data.Find("sceneList")) return std::nullopt;
    json::Value* old = data.Find("scene");
    if (!old) return std::nullopt;
    json::Value scene = std::move(*old);
    data.Erase("scene");
    return scene;
}

ScenePlan PlanSceneSave(const json::Value& scene, const SceneSlot& slot, const std::string& newGuid) {
    ScenePlan plan;
    plan.after = slot;
    if (plan.after.guid.empty()) {
        plan.after.guid = newGuid;
        // The name kept in the .meta (asset::MetaText) is the file's own.
        const size_t slash = slot.path.find_last_of('/');
        plan.files.push_back({slot.path + std::string(asset::kMetaSuffix), asset::MetaText("", newGuid, slot.path.substr(slash + 1))});
    }
    std::string text = SceneFileText(scene);
    const uint64_t sign = TextSign(text);
    if (sign != slot.onDisk) {
        plan.files.push_back({slot.path, std::move(text)});
        plan.after.onDisk = sign;
    }
    return plan;
}

SceneSlot SlotAfterFailedSave(const SceneSlot& slot, const SceneSlot& tried, bool newMetaThere) {
    SceneSlot out = slot.guid.empty() && newMetaThere ? tried : slot;
    out.onDisk = 0;
    return out;
}

}  // namespace rm::game_production
