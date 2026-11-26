#include "rm_engine/game_x_navigation/editor/Connector.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "editor/lang/Lang.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_navigation/editor/NavLines.h"
#include "rm_engine/game_x_navigation/editor/NavTab.h"
#include "rm_engine/game_x_navigation/runtime/Components.h"
#include "rm_engine/navigation/runtime/NavFile.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <format>
#include <string>

namespace rm::game_x_navigation {

namespace {

using editor::TrKey;
using editor::TrU8;
using game_production::Scene;

// --- 部品の欄のドロップダウン: エージェントの型 / エリアの種類 (シーンの設定「ナビ」から) ---------------------
bool FieldIs(const reflect::TypeDesc& type, const reflect::FieldPath& path, std::string_view name) {
    return path.size() == 1 && path[0] < type.fields.size() && std::string_view(type.fields[path[0]].name) == name;
}

std::vector<editor::inspector::Choice> AreaChoices(const Scene& scene) {
    std::vector<editor::inspector::Choice> out;
    if (const NavSettings* s = scene.Setting<NavSettings>()) {
        for (const NavAreaKind& a : s->areas) out.push_back({std::to_string(a.id), Utf8ToWide(TrU8(a.name))});
    }
    return out;
}

std::optional<std::vector<editor::inspector::Choice>> VolumeChoices(editor::SoftwareHost&, const Scene& scene, int, const reflect::FieldPath& path) {
    if (!FieldIs(reflect::TypeOf<NavMeshVolume>(), path, "agentType")) return std::nullopt;
    std::vector<editor::inspector::Choice> out;
    if (const NavSettings* s = scene.Setting<NavSettings>()) {
        for (const NavAgentType& t : s->agentTypes) out.push_back({std::to_string(t.id), Utf8ToWide(TrU8(t.name))});
    }
    return out;
}
std::optional<std::vector<editor::inspector::Choice>> GeometryChoices(editor::SoftwareHost&, const Scene& scene, int, const reflect::FieldPath& path) {
    if (!FieldIs(reflect::TypeOf<NavGeometry>(), path, "area")) return std::nullopt;
    return AreaChoices(scene);
}
std::optional<std::vector<editor::inspector::Choice>> AreaFieldChoices(editor::SoftwareHost&, const Scene& scene, int, const reflect::FieldPath& path) {
    if (!FieldIs(reflect::TypeOf<NavArea>(), path, "area")) return std::nullopt;
    return AreaChoices(scene);
}

// --- 書き出し: .rmnav を確かめて Baked/<id>.rmnav に (知らないチャンク・エディターだけの物は入れない) ----------------
game_production::BakeResult BakeNav(const game_production::ExportAsset& a, game_production::ExportBaked& out, std::string* error) {
    if (asset::Extension(PathToUtf8(a.file.filename())) != ".rmnav") return game_production::BakeResult::NotMine;
    std::string why;
    const auto bytes = platform::ReadFileBytes(a.file, &why);
    if (!bytes) {
        if (error) *error = why;
        return game_production::BakeResult::Failed;
    }
    navigation::NavFileHead head;
    const auto data = navigation::ReadNavFile(*bytes, &head, &why);
    if (!data) {
        if (error) *error = why;
        return game_production::BakeResult::Failed;
    }
    out.path = "Baked/" + a.id + ".rmnav";
    out.files.push_back({out.path, navigation::WriteNavFile(*data, head)});
    out.note = std::format("ナビメッシュを入れました: {} (区画 {}・型「{}」・焼いた版 {})", a.rel, data->tiles.size(), head.agentName, head.engine);
    return game_production::BakeResult::Done;
}
bool NeedsNav(const game_production::ExportAsset&) { return true; }

}  // namespace

int MakeVolume(Scene& scene, int parent, size_t index) {
    const int id = scene.Create(TrU8("ナビメッシュ"), parent, index);
    game_production::AddComponentTo<space3d::Transform>(scene, id);
    game_production::AddComponentTo<NavMeshVolume>(scene, id);
    return id;
}

int MakeArea(Scene& scene, int parent, size_t index) {
    const int id = scene.Create(TrU8("ナビのエリア"), parent, index);
    game_production::AddComponentTo<space3d::Transform>(scene, id);
    game_production::AddComponentTo<NavArea>(scene, id);
    return id;
}

void RegisterEditor(editor::PluginRegistry& registry) {
    // 「ナビ」タブ: 焼く・古い印・経路を試す・型とエリア (NavTab.h)。
    static const game_production::ViewPanelInfo kTab{"navigation", TrKey(L"ナビ"), &CreateNavTab};
    registry.Contribute(game_production::kViewPanelPoint, kTab);
    // シーンビューの線: 範囲の箱・焼いたナビメッシュの縁・エリアの形 (焼いたファイルを読むので hostLines)。
    static const game_production::SceneLinesInfo kLines{.hostLines = &NavSceneLines};
    registry.Contribute(game_production::kSceneLinesPoint, kLines);
    // 階層の 追加 ▸ ナビ。
    static const game_production::CreateItemInfo kItems[] = {
        {.label = TrKey(L"ナビメッシュ"), .folder = TrKey(L"ナビ"), .make = &MakeVolume},
        {.label = TrKey(L"ナビのエリア"), .folder = TrKey(L"ナビ"), .make = &MakeArea},
    };
    for (const auto& item : kItems) registry.Contribute(game_production::kCreateItemPoint, item);
    // 部品の欄の型・エリアをドロップダウンで。
    static const game_production::ComponentLook kVolumeLook{.type = &reflect::TypeOf<NavMeshVolume>, .choices = &VolumeChoices};
    registry.Contribute(game_production::kComponentLookPoint, kVolumeLook);
    static const game_production::ComponentLook kGeometryLook{.type = &reflect::TypeOf<NavGeometry>, .choices = &GeometryChoices};
    registry.Contribute(game_production::kComponentLookPoint, kGeometryLook);
    static const game_production::ComponentLook kAreaLook{.type = &reflect::TypeOf<NavArea>, .choices = &AreaFieldChoices};
    registry.Contribute(game_production::kComponentLookPoint, kAreaLook);
    // 書き出し: 焼いたナビメッシュ (.rmnav)。
    static const game_production::ExportBaker kBaker{&BakeNav, &NeedsNav};
    registry.Contribute(game_production::kExportBakePoint, kBaker);
}

}  // namespace rm::game_x_navigation
