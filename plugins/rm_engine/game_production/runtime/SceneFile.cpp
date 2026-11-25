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

bool SceneBuilt(const SceneList& list, const AssetRef& scene) {
    if (scene == FirstScene(list)) return true;
    return std::find(list.notBuilt.begin(), list.notBuilt.end(), scene) == list.notBuilt.end();
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

std::vector<AssetRef> PlayScenes(const SceneList& list, const AssetRef& open) {
    std::vector<AssetRef> out;
    for (const AssetRef& s : GameScenes(list, open)) {
        if (s.Empty() || SceneBuilt(list, s)) out.push_back(s);
    }
    if (std::find(out.begin(), out.end(), open) == out.end()) out.push_back(open);  // (外してあるが開いている: 遊べる)
    return out;
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

std::pair<std::string, int> SplitSceneNumber(std::string_view name) {
    size_t digits = 0;
    while (digits < name.size() && name[name.size() - 1 - digits] >= '0' && name[name.size() - 1 - digits] <= '9') ++digits;
    // "<名前> <数>" だけ: 前に空白と名前があり、数は 1 から 6 桁 (大きすぎる数で溢れない)。
    if (digits == 0 || digits > 6 || digits + 1 >= name.size() || name[name.size() - 1 - digits] != ' ') return {std::string(name), 0};
    const int number = std::stoi(std::string(name.substr(name.size() - digits)));
    if (number <= 0) return {std::string(name), 0};
    return {std::string(name.substr(0, name.size() - digits - 1)), number};
}

SceneList SceneListWithCopy(const SceneList& list, const AssetRef& original, const AssetRef& copy, const AssetRef& open) {
    SceneList out = list;
    if (copy.Empty() || std::find(out.scenes.begin(), out.scenes.end(), copy) != out.scenes.end()) return out;
    const auto at = original.Empty() ? out.scenes.end() : std::find(out.scenes.begin(), out.scenes.end(), original);
    if (at != out.scenes.end()) {
        out.scenes.insert(at + 1, copy);
    } else if (original == open) {
        out.scenes.push_back(copy);
    }
    return out;
}

SceneList SceneListWithout(const SceneList& list, const AssetRef& gone) {
    SceneList out = list;
    if (gone.Empty()) return out;
    std::erase(out.scenes, gone);
    std::erase(out.notBuilt, gone);
    if (out.first == gone) out.first = {};
    if (out.open == gone) out.open = {};
    return out;
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

void NegateJsonNumber(json::Value& data, std::string_view key, size_t index) {
    json::Value* v = data.IsObject() ? data.Find(std::string(key)) : nullptr;
    if (!v || !v->IsArray() || index >= v->Items().size()) return;
    const json::Value& n = v->Items()[index];
    if (!n.IsNumber()) return;
    json::Value next = json::Value::MakeArray();
    for (size_t i = 0; i < v->Items().size(); ++i) next.Push(i == index ? json::Value(-v->Items()[i].AsDouble()) : v->Items()[i]);
    *v = std::move(next);
}

void MirrorZ(json::Value& doc, const RuntimeRegistry& registry) {
    auto mirrorList = [&](json::Value* list) {
        if (!list || !list->IsArray()) return;
        json::Value next = json::Value::MakeArray();
        for (json::Value c : list->Items()) {
            if (c.IsObject()) {
                const std::string type = c.Get("type").AsString();
                for (const auto& m : registry.Contributions<MirrorZInfo>(kMirrorZPoint)) {
                    if (!m.item->type || !m.item->mirror || type != m.item->type) continue;
                    if (json::Value* data = c.Find("data"); data && data->IsObject()) m.item->mirror(*data);
                }
            }
            next.Push(std::move(c));
        }
        *list = std::move(next);
    };
    if (!doc.IsObject()) return;
    if (json::Value* entities = doc.Find("entities"); entities && entities->IsArray()) {
        json::Value next = json::Value::MakeArray();
        for (json::Value e : entities->Items()) {
            if (e.IsObject()) mirrorList(e.Find("components"));
            next.Push(std::move(e));
        }
        *entities = std::move(next);
    }
    mirrorList(doc.Find("settings"));
}

std::optional<json::Value> ReadSceneFile(std::string_view text, std::string* error, const RuntimeRegistry* registry) {
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
    if (format < 2 && registry) {  // (右手系のころのシーン: 左手系へ写す。次の保存で形式 2 になる)
        MirrorZ(*doc, *registry);
        doc->Set("format", kSceneFormat);
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
        // .meta に残る名前 (asset::MetaText) は、そのファイル自身のもの。
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
