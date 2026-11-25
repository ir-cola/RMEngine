#include "rm_engine/climate/editor/ClimateTab.h"

#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "core/reflect/Object.h"
#include "editor/inspector/Inspector.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/climate/editor/ClimatePages.h"
#include "rm_engine/climate/editor/ZoneLines.h"
#include "rm_engine/climate/runtime/Climate.h"
#include "rm_engine/game_production/editor/SceneSettingLook.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rm::climate {

namespace {

namespace w = editor::w;
namespace theme = editor::theme;
namespace inspector = editor::inspector;
using editor::Tr;
using editor::TrF;
using editor::TrU8;
using game_production::SceneViewHost;
using ui::Rect;

constexpr float kPad = 8, kRow = 26, kBarH = 28, kSideW = 300;
constexpr float kMinHalfWidth = 2.f, kMaxHalfWidth = 200000.f;
constexpr float kCellChoices[] = {1, 2, 4, 8, 16};  // マスの大きさ (m) の選べるもの
constexpr float kHeatColdest = -20.f, kHeatHottest = 40.f;  // 見る = 気温: 色の段の両端 (℃)
constexpr float kToolSquare = 37.f, kToolGap = 4.f;  // 地図の左上の道具のボタン (シーンビューの左の列と同じ大きさ)
constexpr int kTools = 4;

float CellSizeOf(const ClimateMap& map) { return std::isfinite(map.cellSize) ? std::clamp(map.cellSize, kMinCellSize, kMaxCellSize) : 4.f; }

int FloorCell(float v) {
    const float f = std::floor(v);
    return f < -1e9f ? -1000000000 : f > 1e9f ? 1000000000 : static_cast<int>(f);
}

// シーンの設定 (`type` の値。`resolve` が今ある場所) を `change` で変え、変わったフィールドごとに
// reflect::SetFieldCommand にして、履歴に Undo の 1 件として積む (編集 = Undo = 未保存: 憲法 7)。
// `merge` != 0: 1 つの欄に打っている間は 1 件。変わらなかったら false。
bool EditSetting(editor::SoftwareHost& host, const reflect::TypeDesc& type, const std::function<reflect::EditTarget()>& resolve,
                 const std::string& label, uint64_t merge, const std::function<void(void*)>& change) {
    const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
    if (!t.object) return false;
    reflect::Object next(type);
    type.copy(next.Get(), t.object);
    change(next.Get());
    const uint64_t diff = reflect::Diff(type, t.object, next.Get());
    if (diff == 0) return false;
    std::vector<std::unique_ptr<ICommand>> cmds;
    for (size_t i = 0; i < type.fields.size() && i < 64; ++i) {
        if (!(diff & (uint64_t{1} << i))) continue;
        const reflect::FieldDesc& f = type.fields[i];
        cmds.push_back(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{i}, reflect::SaveField(f, t.object, t.unknowns),
                                                                  reflect::SaveField(f, next.Get(), t.unknowns), resolve, label, merge));
    }
    if (cmds.size() == 1) {
        host.History().Push(std::move(cmds.front()), false);
        return true;
    }
    auto group = std::make_unique<CommandGroup>(label, merge);
    for (auto& cmd : cmds) group->Add(std::move(cmd));
    host.History().Push(std::move(group), false);
    return true;
}

void Note(ui::Context& c, const Rect& r, const std::wstring& text) {
    ui::TextStyle ts;
    ts.wrap = true;
    ts.color = theme::TextDim;
    ts.v = ui::VAlign::Top;
    c.P().Text(r, text, ts);
}

void Label(ui::Context& c, const Rect& r, const std::wstring& text, bool bold = false) {
    ui::TextStyle ts;
    ts.color = theme::Text;
    ts.bold = bold;
    c.P().Text(r, text, ts);
}

// 3D の Transform を持つエンティティのワールドの位置 (地図で「全体を見る」の範囲)。
std::vector<Vec3> EntityPositions(const game_production::Scene& scene) {
    const std::string t3 = reflect::TypeOf<space3d::Transform>().name;
    std::unordered_map<int, space3d::Mat4> worlds;
    std::vector<Vec3> out;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const game_production::SceneEntity& e = *t.entity;
        const auto up = worlds.find(e.data.parent);
        space3d::Mat4 world = up != worlds.end() ? up->second : space3d::Mat4{};
        if (const game_production::Component* c = e.FindComponent(t3); c && c->Known()) {
            world = space3d::WorldMatrix(world, *static_cast<const space3d::Transform*>(c->value.Get()));
            out.push_back({world.m[3][0], world.m[3][1], world.m[3][2]});
        }
        worlds[e.data.id] = world;
    }
    return out;
}

// 地図の上の場所: 画面 (DIP) とワールドの (x, z)。画面の右が +X、上が +Z (SceneViewHost::Overhead)。
struct MapView {
    Rect r;
    Vec2 center;
    float scale = 1;  // 1 m あたりの DIP
    float CX() const { return r.x + r.w * 0.5f; }
    float CY() const { return r.y + r.h * 0.5f; }
    Vec2 ToScreen(float x, float z) const { return {CX() + (x - center.x) * scale, CY() - (z - center.y) * scale}; }
    Vec2 ToWorld(float sx, float sy) const { return {center.x + (sx - CX()) / scale, center.y - (sy - CY()) / scale}; }
};

class ClimateTab final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& r, SceneViewHost& host) override {
        if (r.w < 120 || r.h < 120) return;
        const Rect bar{r.x + kPad, r.y + kPad, r.w - 2 * kPad, kBarH};
        const std::wstring modes[] = {Tr(L"気候"), Tr(L"天候")};
        const std::wstring tips[] = {Tr(L"地図に気候 (気温と湿り気) を塗ります"), Tr(L"地図に雲と雨雲を描きます。シーン全体の天気 (降水量・霧・風) もここで変えます")};
        const std::wstring_view modeLabels[] = {modes[0], modes[1]};
        const std::wstring_view modeTips[] = {tips[0], tips[1]};
        if (const int picked = w::Segmented(c, "climate.mode", {bar.x, bar.y, std::min(220.f, bar.w * 0.5f), bar.h}, modeLabels, state_.mode == 1 ? 1 : 0, modeTips);
            picked >= 0 && picked != state_.mode) {
            state_.mode = picked;
            selected_ = -1;
            pen_.clear();
        }
        const Rect body{r.x + kPad, bar.Bottom() + kPad, r.w - 2 * kPad, r.Bottom() - kPad - (bar.Bottom() + kPad)};
        if (host.space == "2d") {
            Note(c, body, Tr(L"気候の地図は、今は 3D のシーンだけです。2D のシーンでは、部品「気候エリア 2D」で場所ごとの気候を変えられます。"));
            return;
        }
        const ClimateMap* map = host.scene.Setting<ClimateMap>();
        if (!map) {
            Note(c, body, Tr(L"このシーンには気候の地図がありません。"));
            return;
        }
        const bool frame = w::Button(c, "climate.frame", {bar.Right() - 130, bar.y, 130, bar.h}, Tr(L"全体を見る"));
        {  // 見る: 塗り / 気温 / 湿り気 / 雲 / 雨
            const float x0 = bar.x + std::min(220.f, bar.w * 0.5f) + 12;
            const float room = bar.Right() - 138 - x0;
            if (room >= 220) {
                const float labelW = 40;
                Label(c, {x0, bar.y, labelW, bar.h}, Tr(L"見る"));
                const std::wstring views[] = {Tr(L"塗り"), Tr(L"気温"), Tr(L"湿り気"), Tr(L"雲"), Tr(L"雨")};
                const std::wstring viewTips[] = {Tr(L"描いたもの (気候の種類 / 天候では雲・雨雲) を色で"), Tr(L"その場所の気温 (基準の高さで) を色の段で"),
                                                 Tr(L"その場所の湿り気を色の段で"), Tr(L"地図に塗った雲の量を色の段で (塗っていない所は天気のまま)"),
                                                 Tr(L"描いた雨雲の雨の濃さを色の段で (描いていない所はシーン全体の降水量)")};
                const std::wstring_view labels[] = {views[0], views[1], views[2], views[3], views[4]};
                const std::wstring_view tipViews[] = {viewTips[0], viewTips[1], viewTips[2], viewTips[3], viewTips[4]};
                if (const int picked = w::Segmented(c, "climate.view", {x0 + labelW, bar.y, std::min(360.f, room - labelW), bar.h}, labels,
                                                    std::clamp(state_.view, 0, 4), tipViews);
                    picked >= 0) {
                    state_.view = picked;
                }
            }
        }

        // 広いときは地図が左、設定が右。狭いときは上下に。
        Rect mapR, side;
        if (body.w >= 520) {
            const float sideW = std::min(kSideW, body.w * 0.42f);
            mapR = {body.x, body.y, body.w - sideW - kPad, body.h};
            side = {mapR.Right() + kPad, body.y, sideW, body.h};
        } else {
            const float mapH = std::floor(body.h * 0.5f);
            mapR = {body.x, body.y, body.w, mapH};
            side = {body.x, mapR.Bottom() + kPad, body.w, body.h - mapH - kPad};
        }
        if (frame || !(state_.halfWidth > 0)) Frame(host, *map, mapR);
        state_.halfWidth = std::clamp(std::isfinite(state_.halfWidth) ? state_.halfWidth : 50.f, kMinHalfWidth, kMaxHalfWidth);
        if (!std::isfinite(state_.center.x) || !std::isfinite(state_.center.y)) state_.center = {0, 0};
        if (map->kinds.empty() || !FindKind(*map, state_.kind)) state_.kind = map->kinds.empty() ? 0 : map->kinds.front().id;
        state_.tool = std::clamp(state_.tool, 0, kTools - 1);
        state_.weatherLayer = state_.weatherLayer == 0 ? 0 : 1;
        state_.view = std::clamp(state_.view, 0, 4);
        state_.clouds = std::isfinite(state_.clouds) ? std::clamp(state_.clouds, 0.f, 100.f) : 80.f;
        state_.rain = std::isfinite(state_.rain) ? std::clamp(state_.rain, 0.f, 100.f) : 60.f;
        state_.soft = std::isfinite(state_.soft) ? std::clamp(state_.soft, 0.f, 100.f) : 100.f;

        DrawMap(c, mapR, host, *map);
        if (side.w >= 60 && side.h >= 60) {
            if (state_.mode == 1) {
                DrawWeatherSide(c, side, host, *map);
            } else {
                DrawClimateSide(c, side, host, *map);
            }
        }
    }

    void LoadState(const json::Value& s) override {
        state_ = {};
        unknowns_ = {};
        if (s.IsObject()) reflect::Load(state_, s, &unknowns_);
    }
    json::Value SaveState() const override { return reflect::Save(state_, &unknowns_); }

private:
    static std::function<reflect::EditTarget()> MapTarget(const SceneViewHost& host) {
        return host.setting ? host.setting(reflect::TypeOf<ClimateMap>().name) : std::function<reflect::EditTarget()>{};
    }
    static bool CanEdit(const SceneViewHost& host) { return !host.playing && host.setting != nullptr; }

    // 地図を変える (Undo の 1 件)。
    bool EditMap(SceneViewHost& host, const std::string& label, uint64_t merge, const std::function<void(ClimateMap&)>& change) {
        if (!CanEdit(host)) return false;
        return EditSetting(host.host, reflect::TypeOf<ClimateMap>(), MapTarget(host), label, merge,
                           [&](void* p) { change(*static_cast<ClimateMap*>(p)); });
    }

    MapView ViewOf(const Rect& r) const { return {r, state_.center, r.w / (2.f * state_.halfWidth)}; }

    // 地図にシーン全体 (エンティティ・気候エリア・塗った所) が入るように。
    void Frame(const SceneViewHost& host, const ClimateMap& map, const Rect& r) {
        float x0 = std::numeric_limits<float>::max(), z0 = x0, x1 = -x0, z1 = -x0;
        auto add = [&](float x, float z) {
            if (!std::isfinite(x) || !std::isfinite(z)) return;
            x0 = std::min(x0, x), x1 = std::max(x1, x), z0 = std::min(z0, z), z1 = std::max(z1, z);
        };
        for (const Vec3& p : EntityPositions(host.scene)) add(p.x, p.z);
        const ClimateField zones(host.scene);
        for (const ClimateField::Zone3D& z : zones.Zones3D()) {
            const ZoneBody b = BodyOf(z.zone, z.world);
            const float reach = std::sqrt(b.half.x * b.half.x + b.half.z * b.half.z);
            add(z.world.m[3][0] - reach, z.world.m[3][2] - reach);
            add(z.world.m[3][0] + reach, z.world.m[3][2] + reach);
        }
        for (const MapLayer layer : {MapLayer::Climate, MapLayer::Clouds, MapLayer::Rain}) {
            const PaintGrid g = PaintOf(map, layer);
            if (g.Empty()) continue;
            const float cs = CellSizeOf(map);
            add(static_cast<float>(g.cellX) * cs, static_cast<float>(g.cellZ) * cs);
            add(static_cast<float>(g.cellX + g.width) * cs, static_cast<float>(g.cellZ + g.depth) * cs);
        }
        for (const ClimateShape& shape : map.shapes) {
            for (const Vec2& p : shape.points) add(p.x, p.y);
        }
        if (x0 > x1) {
            state_.center = {0, 0};
            state_.halfWidth = 50;
            return;
        }
        state_.center = {(x0 + x1) * 0.5f, (z0 + z1) * 0.5f};
        const float aspect = r.h > 1 ? r.w / r.h : 1.f;
        state_.halfWidth = std::clamp(std::max((x1 - x0) * 0.5f, (z1 - z0) * 0.5f * aspect) * 1.15f + 10.f, kMinHalfWidth, kMaxHalfWidth);
    }

    // --- 地図 ---------------------------------------------------------------------------------
    // 描いている層: 気候の側は気候、天候の側は雲か雨雲。
    MapLayer Layer() const { return state_.mode != 1 ? MapLayer::Climate : state_.weatherLayer == 0 ? MapLayer::Clouds : MapLayer::Rain; }
    // 地図の左上の道具の列 (シーンビューの左の列と同じ所)。
    static Rect ToolsRect(const Rect& map) { return {map.x + 7, map.y + 7, kToolSquare, kTools * kToolSquare + (kTools - 1) * kToolGap}; }
    bool OverTools(ui::Context& c, const Rect& map) const { return ToolsRect(map).Contains(c.In().mouseX, c.In().mouseY); }

    void DrawMap(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateMap& map) {
        if (r.w < 20 || r.h < 20) return;
        if (state_.tool != 2) pen_.clear();
        ValidateSelection(map);
        HandleInput(c, r, host, map);
        ValidateSelection(map);
        if (!madeView_) {  // 3D のシーンビューを 1 つ (真上から描かせる)
            madeView_ = true;
            for (const auto& v : host.host.Registry().Contributions<game_production::SceneViewInfo>(game_production::kSceneViewPoint)) {
                if (v.item->create && v.item->space && std::string_view(v.item->space) == "3d") {
                    view_ = v.item->create();
                    break;
                }
            }
        }
        if (view_) {
            SceneViewHost overhead = host;
            overhead.selection = {};
            overhead.overhead = SceneViewHost::Overhead{state_.center, state_.halfWidth};
            view_->Draw(c, r, overhead);
        } else {
            w::DrawSurface(c, r, w::Surface::Sunken, 0);
        }
        c.PushClip(r);
        const ClimateField field(host.scene);
        if (state_.view == 0) {
            DrawPaint(c, r, map);
        } else {
            DrawHeat(c, r, field);
        }
        DrawShapes(c, r, map);
        DrawZones(c, r, field);
        const MapView v = ViewOf(r);
        const ui::Input& in = c.In();
        const bool hovered = c.IsHovered(r) && !OverTools(c, r);
        if (CanEdit(host) && hovered && !panning_ && state_.tool <= 1) DrawBrushAt(c, v, in.mouseX, in.mouseY);
        if (hovered && !panning_) DrawReadout(c, r, v, field, in.mouseX, in.mouseY);
        DrawLegend(c, r);
        // 下に、縮尺と使い方。上に、描けないわけ。
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        const float cs = CellSizeOf(map);
        c.P().Text({r.x + 8, r.Bottom() - 38, r.w - 16, 16}, TrF(L"1 マス {} m ・ 地図の幅 {} m", cs, static_cast<int>(std::round(state_.halfWidth * 2))), ts);
        c.P().Text({r.x + 8, r.Bottom() - 20, r.w - 16, 16}, HelpLine(), ts);
        if (!view_) Note(c, r.Inset(16), Tr(L"3D 空間のプラグイン (ゲーム制作 × 3D) が入ると、ここにシーンを真上から見た絵が出ます。塗ることはできます。"));
        if (host.playing) Note(c, {r.x + kToolSquare + 18, r.y + 8, r.w - kToolSquare - 26, 40}, Tr(L"Play 中は描けません (■ で止めてから)"));
        DrawTools(c, r);
        c.PopClip();
    }

    // 地図の左上の道具: 筆・消しゴム・ペン・選ぶ (シーンビューの左の列と同じ正方形のボタン)。
    void DrawTools(ui::Context& c, const Rect& map) {
        const Rect col = ToolsRect(map);
        if (map.h < col.h + 40 || map.w < col.w + 60) return;
        const std::wstring tips[kTools] = {
            Tr(L"筆: 丸い筆でマスを塗ります"), Tr(L"消しゴム: 筆で塗ったマスを消します (ペンの形は「選ぶ」で消します)"),
            Tr(L"ペン: 線を引いて囲みます。囲んだ中がその気候 (天候では雲・雨雲) になります"), Tr(L"選ぶ: 囲んだ形を選んで、点を動かして直します")};
        constexpr w::Icon kIcons[kTools] = {w::Icon::Brush, w::Icon::Eraser, w::Icon::Pen, w::Icon::Cursor};
        constexpr const char* kIds[kTools] = {"climate.tool.brush", "climate.tool.eraser", "climate.tool.pen", "climate.tool.select"};
        for (int i = 0; i < kTools; ++i) {
            const Rect sq{col.x, col.y + static_cast<float>(i) * (kToolSquare + kToolGap), kToolSquare, kToolSquare};
            if (w::ToolSquare(c, kIds[i], sq, kIcons[i], state_.tool == i, tips[i]).clicked && state_.tool != i) {
                state_.tool = i;
                c.RequestRedraw();
            }
        }
    }

    std::wstring HelpLine() const {
        switch (state_.tool) {
            case 1: return Tr(L"左ドラッグ: 筆で塗った所を消す ・ ホイール: 拡大縮小 ・ 右 / 中ドラッグ: 動かす");
            case 2: return Tr(L"クリック: 点を置く ・ ドラッグ: なぞる ・ 最初の点 / ダブルクリック / Enter: 閉じる ・ Esc: やめる");
            case 3: return Tr(L"クリック: 形を選ぶ ・ 点 / 中をドラッグ: 動かす ・ 点を右クリック: 消す ・ Delete: 形を消す");
            default: return Tr(L"左ドラッグ: 塗る ・ ホイール: 拡大縮小 ・ 右 / 中ドラッグ: 動かす");
        }
    }

    void DrawBrushAt(ui::Context& c, const MapView& v, float sx, float sy) const { w::DrawMapBrush(c, sx, sy, state_.brush * v.scale, state_.tool == 1); }

    // 選んでいる形が今もあり、今の層のものか。
    void ValidateSelection(const ClimateMap& map) {
        if (selected_ >= 0 && (static_cast<size_t>(selected_) >= map.shapes.size() || map.shapes[static_cast<size_t>(selected_)].layer != Layer())) {
            selected_ = -1;
            drag_ = Drag::None;
        }
        if (state_.tool != 3) drag_ = Drag::None;
        if (selected_ < 0 && drag_ != Drag::None) drag_ = Drag::None;
    }
    const ClimateShape* Selected(const ClimateMap& map) const {
        return selected_ >= 0 && static_cast<size_t>(selected_) < map.shapes.size() ? &map.shapes[static_cast<size_t>(selected_)] : nullptr;
    }

    // ホイール: カーソルの所を中心に拡大縮小。右 / 中ドラッグ: 動かす。左: 道具ごと (筆・消しゴムは離したら Undo の 1 件)。
    // 左上の道具のボタンの上は地図ではない (ボタンはこのあと描くので、押したのはボタンのものになる)。
    void HandleInput(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateMap& map) {
        const ui::Input& in = c.In();
        const bool overTools = OverTools(c, r);
        const bool hovered = c.IsHovered(r) && !overTools;
        const MapView v = ViewOf(r);
        if (hovered && in.wheel != 0) {
            const Vec2 at = v.ToWorld(in.mouseX, in.mouseY);
            const float hw = std::clamp(state_.halfWidth * std::pow(0.85f, in.wheel), kMinHalfWidth, kMaxHalfWidth);
            const float scale = r.w / (2.f * hw);
            state_.center = {at.x - (in.mouseX - v.CX()) / scale, at.y + (in.mouseY - v.CY()) / scale};
            state_.halfWidth = hw;
            c.RequestRedraw();
        }
        const bool editing = CanEdit(host);
        // 選ぶ: 点の上で右クリックすると、その点を消す (動かす代わりに)。
        bool rightUsed = false;
        if (editing && state_.tool == 3 && hovered && c.MousePressed(ui::Mouse::Right)) rightUsed = RemovePointAt(host, map, v, in.mouseX, in.mouseY);
        if (hovered && !rightUsed && (c.MousePressed(ui::Mouse::Right) || c.MousePressed(ui::Mouse::Middle))) panning_ = true;
        if (panning_) {
            if (in.down[1] || in.down[2]) {
                const float scale = v.scale;
                state_.center.x -= in.mouseDX / scale;
                state_.center.y += in.mouseDY / scale;
                if (in.mouseDX != 0 || in.mouseDY != 0) c.RequestRedraw();
            } else {
                panning_ = false;
            }
        }
        auto b = c.ButtonBehavior(c.MakeId("climate.map"), r);
        if (overTools && !stroking_ && !penDragging_ && drag_ == Drag::None) b = {};  // (ボタンの上: 地図は何もしない)
        if (!editing) {
            stroking_ = false;
            pen_.clear();
            drag_ = Drag::None;
            return;
        }
        switch (state_.tool) {
            case 2: PenInput(c, host, map, v, b); break;
            case 3: SelectInput(c, host, map, v, b); break;
            default: BrushInput(c, host, map, v, b); break;
        }
    }

    void BrushInput(ui::Context& c, SceneViewHost& host, const ClimateMap& map, const MapView& v, const ui::Context::ButtonResult& b) {
        const ui::Input& in = c.In();
        const bool erase = state_.tool == 1;
        const MapLayer layer = Layer();
        const int id = erase                         ? 0
                       : layer == MapLayer::Clouds   ? CloudCellOf(state_.clouds)
                       : layer == MapLayer::Rain     ? CloudCellOf(state_.rain)
                                                     : state_.kind;
        // (押して離したのが同じフレームに届いたクリックも、1 回塗る)
        const bool down = b.held || (b.clicked && c.MousePressed());
        if (down && (erase || layer != MapLayer::Climate || FindKind(map, id))) {
            const Vec2 at = v.ToWorld(in.mouseX, in.mouseY);
            const float cs = CellSizeOf(map);
            if (!stroking_) {
                stroking_ = true;
                strokeLayer_ = layer;
                stroke_ = PaintOf(map, layer);
                PaintCircle(stroke_, cs, at, state_.brush, id);
            } else {
                // 前の場所から今の場所まで、筆の半径の 3 割ごとに塗る (速く動かしても途切れない)。
                const float dx = at.x - last_.x, dz = at.y - last_.y;
                const float step = std::max(state_.brush * 0.3f, cs * 0.5f);
                const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(dx * dx + dz * dz) / step)), 1, 10000);
                for (int i = 1; i <= n; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(n);
                    PaintCircle(stroke_, cs, {last_.x + dx * t, last_.y + dz * t}, state_.brush, id);
                }
            }
            last_ = at;
            c.RequestRedraw();
        }
        if (stroking_ && !b.held) {  // 離した: 1 回のドラッグが Undo の 1 件
            stroking_ = false;
            const PaintGrid done = std::move(stroke_);
            stroke_ = {};
            const MapLayer l = strokeLayer_;
            const char* label = l == MapLayer::Clouds ? (erase ? "地図の雲を消す" : "地図に雲を塗る")
                                : l == MapLayer::Rain ? (erase ? "地図の雨雲を消す" : "地図に雨雲を塗る")
                                                      : (erase ? "気候の地図を消す" : "気候の地図に塗る");
            EditMap(host, label, 0, [&done, l](ClimateMap& m) { SetPaint(m, done, l); });
        }
    }

    // ペン: クリックで点、押したままのドラッグでなぞる。最初の点・ダブルクリック・Enter で閉じる。Esc でやめる。
    void PenInput(ui::Context& c, SceneViewHost& host, const ClimateMap& map, const MapView& v, const ui::Context::ButtonResult& b) {
        const ui::Input& in = c.In();
        const Vec2 mouse{in.mouseX, in.mouseY};
        auto closeTo = [&](Vec2 world, float dip) {
            const Vec2 s = v.ToScreen(world.x, world.y);
            return std::hypot(s.x - mouse.x, s.y - mouse.y) <= dip;
        };
        if (b.hovered && c.MousePressed()) {
            if (pen_.size() >= 3 && (b.doubleClicked || closeTo(pen_.front(), 9.f))) {
                ClosePen(host, map);
            } else if (pen_.size() < kMaxShapePoints) {
                pen_.push_back(v.ToWorld(mouse.x, mouse.y));
                penDragging_ = true;
            }
            c.RequestRedraw();
        } else if (penDragging_ && b.held && !pen_.empty() && !closeTo(pen_.back(), 10.f) && pen_.size() < kMaxShapePoints) {
            pen_.push_back(v.ToWorld(mouse.x, mouse.y));  // (なぞる)
            c.RequestRedraw();
        }
        if (!b.held) penDragging_ = false;
        if (!pen_.empty() && c.Focused() == 0) {
            if (c.KeyPressed(ui::Key::Enter) && pen_.size() >= 3) ClosePen(host, map);
            if (c.KeyPressed(ui::Key::Escape)) pen_.clear();
        }
        if (!pen_.empty()) c.RequestRedraw();  // (線をカーソルまで伸ばす)
    }

    void ClosePen(SceneViewHost& host, const ClimateMap& map) {
        ClimateShape s;
        s.layer = Layer();
        s.kind = state_.kind;
        s.clouds = std::clamp(state_.clouds, 0.f, 100.f);
        s.rain = std::clamp(state_.rain, 0.f, 100.f);
        s.soft = std::clamp(state_.soft, 0.f, 100.f);
        s.points = std::move(pen_);
        pen_.clear();
        penDragging_ = false;
        if (s.layer == MapLayer::Climate && !FindKind(map, s.kind)) return;
        const size_t index = map.shapes.size();
        const char* label = s.layer == MapLayer::Clouds ? "雲をペンで囲む" : s.layer == MapLayer::Rain ? "雨雲をペンで囲む" : "気候をペンで囲む";
        if (EditMap(host, label, 0, [&s](ClimateMap& m) { m.shapes.push_back(s); })) {
            selected_ = static_cast<int>(index);
        }
    }

    // 選んだ形の点のうち、画面の (x, y) から 7 DIP 以内のもの (-1: 無し)。
    int PointAt(const ClimateShape& s, const MapView& v, float x, float y) const {
        for (size_t i = 0; i < s.points.size(); ++i) {
            const Vec2 p = v.ToScreen(s.points[i].x, s.points[i].y);
            if (std::fabs(p.x - x) <= 7.f && std::fabs(p.y - y) <= 7.f) return static_cast<int>(i);
        }
        return -1;
    }

    bool RemovePointAt(SceneViewHost& host, const ClimateMap& map, const MapView& v, float x, float y) {
        const ClimateShape* s = Selected(map);
        if (!s) return false;
        const int at = PointAt(*s, v, x, y);
        if (at < 0) return false;
        if (s->points.size() <= 3) return true;  // (3 点は残す。右クリックは使った)
        const size_t index = static_cast<size_t>(selected_);
        EditMap(host, "形の点を消す", 0, [index, at](ClimateMap& m) {
            if (index < m.shapes.size() && static_cast<size_t>(at) < m.shapes[index].points.size()) {
                m.shapes[index].points.erase(m.shapes[index].points.begin() + at);
            }
        });
        return true;
    }

    // 選ぶ: クリックで形を選ぶ (上のものから)。点をドラッグで動かす・中をドラッグで形ごと。Delete で消す。
    void SelectInput(ui::Context& c, SceneViewHost& host, const ClimateMap& map, const MapView& v, const ui::Context::ButtonResult& b) {
        const ui::Input& in = c.In();
        const Vec2 world = v.ToWorld(in.mouseX, in.mouseY);
        if (b.hovered && c.MousePressed()) {
            drag_ = Drag::None;
            int point = -1;
            if (const ClimateShape* s = Selected(map)) point = PointAt(*s, v, in.mouseX, in.mouseY);
            if (point < 0) {
                selected_ = -1;
                for (size_t i = map.shapes.size(); i-- > 0;) {
                    const ClimateShape& s = map.shapes[i];
                    if (s.layer == Layer() && InsideShape(s.points, world)) {
                        selected_ = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (const ClimateShape* s = Selected(map)) {
                drag_ = point >= 0 ? Drag::Point : Drag::Move;
                dragPoint_ = point;
                dragFrom_ = world;
                dragOriginal_ = *s;
                dragShape_ = *s;
                if (s->layer == MapLayer::Climate && FindKind(map, s->kind)) state_.kind = s->kind;
            }
            c.RequestRedraw();
        }
        if (drag_ != Drag::None && Selected(map)) {
            if (b.held) {
                dragShape_ = dragOriginal_;
                if (drag_ == Drag::Point && static_cast<size_t>(dragPoint_) < dragShape_.points.size()) {
                    dragShape_.points[static_cast<size_t>(dragPoint_)] = world;
                } else if (drag_ == Drag::Move) {
                    for (Vec2& p : dragShape_.points) p = {p.x + world.x - dragFrom_.x, p.y + world.y - dragFrom_.y};
                }
                c.RequestRedraw();
            } else {
                const Drag was = drag_;
                drag_ = Drag::None;
                if (!(dragShape_ == dragOriginal_)) {
                    const size_t index = static_cast<size_t>(selected_);
                    const ClimateShape moved = dragShape_;
                    EditMap(host, was == Drag::Point ? "形の点を動かす" : "形を動かす", 0, [index, &moved](ClimateMap& m) {
                        if (index < m.shapes.size()) m.shapes[index].points = moved.points;
                    });
                }
            }
        }
        if (selected_ >= 0 && b.hovered && c.Focused() == 0 && c.KeyPressed(ui::Key::Delete)) RemoveSelected(host);
    }

    void RemoveSelected(SceneViewHost& host) {
        if (selected_ < 0) return;
        const size_t index = static_cast<size_t>(selected_);
        EditMap(host, "形を消す", 0, [index](ClimateMap& m) {
            if (index < m.shapes.size()) m.shapes.erase(m.shapes.begin() + static_cast<std::ptrdiff_t>(index));
        });
        selected_ = -1;
        drag_ = Drag::None;
    }

    // 見せる地図 (形を動かしている間は、動かしている形で)。
    const ClimateMap& ShownMap(const ClimateMap& map) {
        if (drag_ == Drag::None || selected_ < 0 || static_cast<size_t>(selected_) >= map.shapes.size()) return map;
        dragMap_ = map;
        dragMap_.shapes[static_cast<size_t>(selected_)] = dragShape_;
        return dragMap_;
    }

    // 今の層の塗り (筆のマスの上に形を焼いたもの: 計算と同じ BakedOf)。種類の色 / 雲の色で半透明に。
    // 行ごとに、同じ値の続きを 1 つの四角にする。小さすぎるマスは何マスかおきに見る (縮めて見たときに
    // 描く数を抑える)。
    void DrawPaint(ui::Context& c, const Rect& r, const ClimateMap& source) {
        const MapLayer layer = Layer();
        const ClimateMap& map = ShownMap(source);
        PaintGrid preview;
        const PaintGrid* shown = nullptr;
        if (stroking_ && strokeLayer_ == layer) {
            preview = stroke_;  // (塗っている途中: その上に形を焼く)
            for (const ClimateShape& s : map.shapes) {
                if (s.layer == layer) BakeShape(preview, map, s);
            }
            shown = &preview;
        } else if (&map != &source) {
            preview = BakedOf(map, layer);
            shown = &preview;
        } else {
            if (!SameShown(map, layer)) {
                shown_ = BakedOf(map, layer);
                shownMap_ = map;
                shownLayer_ = layer;
            }
            shown = &shown_;
        }
        const PaintGrid& g = *shown;
        const float cs = CellSizeOf(map);
        const MapView v = ViewOf(r);
        const float px = cs * v.scale;  // 1 マスの DIP
        if (px >= 10) {
            const Vec2 origin = v.ToScreen(0, 0);
            w::DrawMapGrid(c, r, origin.x, origin.y, px);
        }
        if (g.Empty()) return;
        std::unordered_map<int, Color> colors;
        for (const ClimateKind& k : map.kinds) colors.emplace(k.id, k.color);
        const Vec2 lo = v.ToWorld(r.x, r.Bottom()), hi = v.ToWorld(r.Right(), r.y);
        const int i0 = std::max(g.cellX, FloorCell(lo.x / cs)), i1 = std::min(g.cellX + g.width - 1, FloorCell(hi.x / cs));
        const int j0 = std::max(g.cellZ, FloorCell(lo.y / cs)), j1 = std::min(g.cellZ + g.depth - 1, FloorCell(hi.y / cs));
        if (i0 > i1 || j0 > j1) return;
        const int stride = px >= 2 ? 1 : static_cast<int>(std::ceil(2.f / std::max(px, 1e-3f)));
        for (int j = j0; j <= j1; j += stride) {
            const int jEnd = std::min(j + stride, j1 + 1);
            for (int i = i0; i <= i1;) {
                const int id = g.At(i, j);
                int k = i + stride;
                while (k <= i1 && g.At(k, j) == id) k += stride;
                const int iEnd = std::min(k, i1 + 1);
                if (id != 0) {
                    const Vec2 a = v.ToScreen(static_cast<float>(i) * cs, static_cast<float>(jEnd) * cs);
                    const Vec2 bb = v.ToScreen(static_cast<float>(iEnd) * cs, static_cast<float>(j) * cs);
                    const Rect cell{a.x, a.y, bb.x - a.x, bb.y - a.y};
                    if (layer == MapLayer::Clouds) {
                        w::DrawMapHeat(c, cell, theme::MapClouds(CloudsOfCell(id) / 100.f));
                    } else if (layer == MapLayer::Rain) {
                        w::DrawMapHeat(c, cell, theme::MapRain(CloudsOfCell(id) / 100.f));
                    } else if (const auto it = colors.find(id); it != colors.end()) {
                        w::DrawMapPaint(c, cell, it->second);
                    }
                }
                i = k;
            }
        }
    }

    bool SameShown(const ClimateMap& m, MapLayer layer) const {
        const ClimateMap& s = shownMap_;
        if (layer != shownLayer_ || s.cellSize != m.cellSize || s.shapes != m.shapes || s.kinds.size() != m.kinds.size()) return false;
        for (size_t i = 0; i < s.kinds.size(); ++i) {
            if (s.kinds[i].id != m.kinds[i].id) return false;
        }
        if (layer == MapLayer::Clouds) {
            return s.cloudCells == m.cloudCells && s.cloudX == m.cloudX && s.cloudZ == m.cloudZ && s.cloudWidth == m.cloudWidth && s.cloudDepth == m.cloudDepth;
        }
        if (layer == MapLayer::Rain) {
            return s.rainCells == m.rainCells && s.rainX == m.rainX && s.rainZ == m.rainZ && s.rainWidth == m.rainWidth && s.rainDepth == m.rainDepth;
        }
        return s.cells == m.cells && s.cellX == m.cellX && s.cellZ == m.cellZ && s.width == m.width && s.depth == m.depth;
    }

    // 見る = 気温 / 湿り気 / 雲 / 雨: 地図の上に、8 DIP ごとの値の色 (ClimateField が答えるもの: 塗った気候・形・
    // 気候エリア・境目の幅。気温は基準の高さでの値)。雲と雨は描いた所だけ (描いた割合の濃さで)。
    void DrawHeat(ui::Context& c, const Rect& r, const ClimateField& field) {
        constexpr float kStep = 8.f;
        const MapView v = ViewOf(r);
        const float height = field.Settings().baseHeight;
        for (float y = r.y; y < r.Bottom(); y += kStep) {
            for (float x = r.x; x < r.Right(); x += kStep) {
                const Rect cell{x, y, std::min(kStep, r.Right() - x), std::min(kStep, r.Bottom() - y)};
                const Vec2 at = v.ToWorld(x + cell.w * 0.5f, y + cell.h * 0.5f);
                const ClimateHere h = field.At({at.x, std::isfinite(height) ? height : 0.f, at.y});
                switch (state_.view) {
                    case 1: w::DrawMapHeat(c, cell, theme::MapTemperature((h.temperature - kHeatColdest) / (kHeatHottest - kHeatColdest))); break;
                    case 2: w::DrawMapHeat(c, cell, theme::MapHumidity(h.humidity / 100.f)); break;
                    case 4: w::DrawMapHeat(c, cell, theme::MapRain(h.rainAmount / 100.f), h.rainWeight); break;
                    default: w::DrawMapHeat(c, cell, theme::MapClouds(h.cloudAmount / 100.f), h.cloudWeight); break;
                }
            }
        }
    }

    void DrawLegend(ui::Context& c, const Rect& r) const {
        if (state_.view == 0 || r.w < 260 || r.h < 140) return;
        const Rect box{r.Right() - 196, r.Bottom() - 104, 186, 56};
        switch (state_.view) {
            case 1: w::DrawMapLegend(c, box, Tr(L"気温 (基準の高さ)"), &theme::MapTemperature, std::format(L"{:g} ℃", kHeatColdest), std::format(L"{:g} ℃", kHeatHottest)); break;
            case 2: w::DrawMapLegend(c, box, Tr(L"湿り気"), &theme::MapHumidity, Tr(L"0 % (からから)"), Tr(L"100 % (じめじめ)")); break;
            case 4: w::DrawMapLegend(c, box, Tr(L"雨の濃さ (雨雲の所)"), &theme::MapRain, Tr(L"0 %"), Tr(L"100 % (嵐)")); break;
            default: w::DrawMapLegend(c, box, Tr(L"雲の量 (塗った所)"), &theme::MapClouds, Tr(L"0 % (晴れ)"), Tr(L"100 % (厚い雲)")); break;
        }
    }

    // カーソルの所の値。
    void DrawReadout(ui::Context& c, const Rect& r, const MapView& v, const ClimateField& field, float sx, float sy) const {
        const Vec2 at = v.ToWorld(sx, sy);
        const float height = std::isfinite(field.Settings().baseHeight) ? field.Settings().baseHeight : 0.f;
        const ClimateHere h = field.At({at.x, height, at.y});
        std::wstring text = TrF(L"{:.1f} ℃ ・ 湿り気 {:.0f} %", h.temperature, h.humidity);
        text += h.cloudWeight > 0.005f ? TrF(L" ・ 雲 {:.0f} % ({:.0f} % 塗った所)", h.cloudAmount, h.cloudWeight * 100.f) : Tr(L" ・ 雲: 天気のまま");
        if (h.rainWeight > 0.005f) text += TrF(L" ・ 雨雲 {:.0f} % ({:.0f} % 描いた所)", h.rainAmount, h.rainWeight * 100.f);
        const float x = std::min(sx + 16.f, r.Right() - 320.f), y = std::min(sy + 18.f, r.Bottom() - 64.f);
        w::DrawMapReadout(c, std::max(x, r.x + 4), std::max(y, r.y + 4), text);
    }

    // ペンで囲んだ形 (今の層のもの) の縁。選んだ形は点も。描いている途中のペンの線も。
    void DrawShapes(ui::Context& c, const Rect& r, const ClimateMap& source) {
        const ClimateMap& map = ShownMap(source);
        const MapView v = ViewOf(r);
        const ui::Input& in = c.In();
        std::vector<Vec2> pts;
        auto screen = [&](std::span<const Vec2> world) {
            pts.clear();
            for (const Vec2& p : world) pts.push_back(v.ToScreen(p.x, p.y));
        };
        for (size_t i = 0; i < map.shapes.size(); ++i) {
            const ClimateShape& s = map.shapes[i];
            if (s.layer != Layer() || s.points.empty()) continue;
            screen(s.points);
            const bool sel = static_cast<int>(i) == selected_;
            int hot = -1;
            if (sel && state_.tool == 3) hot = PointAt(s, v, in.mouseX, in.mouseY);
            w::DrawMapShape(c, pts, true, Color{0, 0, 0, 0}, sel, hot);
        }
        if (!pen_.empty()) {
            screen(pen_);
            const Vec2 tail{in.mouseX, in.mouseY};
            const ClimateKind* k = FindKind(map, state_.kind);
            const Color fill = Layer() == MapLayer::Clouds ? theme::MapClouds(state_.clouds / 100.f)
                               : Layer() == MapLayer::Rain ? theme::MapRain(state_.rain / 100.f)
                               : k                         ? k->color
                                                           : Color{0, 0, 0, 0};
            w::DrawMapShape(c, pts, false, fill, false, pen_.size() >= 3 ? 0 : -1, c.IsHovered(r) ? &tail : nullptr);
        }
    }

    // 気候エリア (3D) の縁と、境目の内側の縁 (シーンビューの枠と同じ線を、真上から)。
    void DrawZones(ui::Context& c, const Rect& r, const ClimateField& field) {
        if (field.Zones3D().empty()) return;
        game_production::DebugDraw lines;
        for (const ClimateField::Zone3D& z : field.Zones3D()) {
            ZoneOutline3D(z, 0, theme::SceneArea(), lines);
            if (z.zone.border > 0) ZoneOutline3D(z, z.zone.border, theme::SceneAreaInner(), lines);
        }
        const MapView v = ViewOf(r);
        for (const game_production::DebugLine& l : lines.Lines()) {
            const Vec2 a = v.ToScreen(l.a.x, l.a.z), b = v.ToScreen(l.b.x, l.b.z);
            if (std::fabs(a.x - b.x) + std::fabs(a.y - b.y) < 0.5f) continue;  // (真上から見ると点になる縦の線)
            w::DrawMapLine(c, a.x, a.y, b.x, b.y, l.color);
        }
    }

    // 筆の半径 (筆と消しゴムのとき)。下の y を返す。
    float DrawBrushRow(ui::Context& c, const Rect& r) {
        if (state_.tool > 1) return r.y;
        const float labelW = std::floor(r.w * 0.42f);
        Label(c, {r.x, r.y, labelW, kRow}, Tr(L"筆の半径 (m)"));
        w::NumberField(c, "climate.brush", {r.x + labelW, r.y, r.w - labelW, kRow}, state_.brush, 0.5f, 10000.f, brushText_);
        return r.y + kRow + 6;
    }

    // --- 「気候」の側: 筆・マスの大きさ・気候の種類・選んだ形・選んだ種類の値 (道具は地図の左上) ---------
    void DrawClimateSide(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateMap& map) {
        const bool canEdit = CanEdit(host);
        float y = DrawBrushRow(c, r);
        const float labelW = std::floor(r.w * 0.42f);
        Label(c, {r.x, y, labelW, kRow}, Tr(L"マスの大きさ (m)"));
        {
            std::vector<std::wstring> texts;
            int sel = -1;
            for (size_t i = 0; i < std::size(kCellChoices); ++i) {
                texts.push_back(std::format(L"{:g}", kCellChoices[i]));
                if (kCellChoices[i] == map.cellSize) sel = static_cast<int>(i);
            }
            const std::vector<std::wstring_view> labels(texts.begin(), texts.end());
            const std::wstring tip = Tr(L"細かいほど形をきれいに塗れます。変えると、塗った所はそのままの場所で塗り直されます");
            const std::vector<std::wstring_view> tips(labels.size(), tip);
            const int pick = w::Segmented(c, "climate.cell", {r.x + labelW, y, r.w - labelW, kRow}, labels, sel, tips);
            if (pick >= 0 && canEdit && kCellChoices[pick] != map.cellSize) {
                const float to = kCellChoices[pick];
                EditMap(host, std::format("マスの大きさを {:g} m に", to), 0, [to](ClimateMap& m) {
                    const PaintGrid g = Resample(PaintOf(m), m.cellSize, to);
                    const PaintGrid clouds = Resample(PaintOf(m, MapLayer::Clouds), m.cellSize, to);
                    const PaintGrid rain = Resample(PaintOf(m, MapLayer::Rain), m.cellSize, to);
                    m.cellSize = to;
                    SetPaint(m, g);
                    SetPaint(m, clouds, MapLayer::Clouds);
                    SetPaint(m, rain, MapLayer::Rain);
                });
            }
        }
        y += kRow + 12;

        y = DrawKindList(c, {r.x, y, r.w, r.Bottom() - y}, host, map, Selected(map));
        if (r.Bottom() - y < 40) return;

        // 選んだ種類の値 (名前・色・気温・湿り気) と、地図の境目の幅: インスペクターと同じ描き方。
        const ClimateKind* chosen = FindKind(map, state_.kind);
        std::vector<inspector::Block> blocks;
        if (chosen) {
            kindCopy_ = *chosen;
            inspector::Block kb;
            kb.type = &reflect::TypeOf<ClimateKind>();
            kb.object = &kindCopy_;
            kb.title = TrF(L"気候「{}」", Utf8ToWide(TrU8(chosen->name)));
            kb.readOnly = !canEdit;
            blocks.push_back(std::move(kb));
        }
        inspector::Block mb;
        mb.type = &reflect::TypeOf<ClimateMap>();
        mb.object = const_cast<ClimateMap*>(&map);  // (インスペクターは書かない: 変更は Edit で返る)
        mb.title = Tr(L"地図");
        mb.readOnly = !canEdit;
        mb.fields = {"border"};
        blocks.push_back(std::move(mb));
        const auto edit = inspector::Draw(c, inspector_, {r.x, y, r.w, r.Bottom() - y}, blocks);
        if (!edit || !canEdit || edit->block >= blocks.size()) return;
        if (blocks[edit->block].type == &reflect::TypeOf<ClimateKind>()) {
            const int kindId = kindCopy_.id;
            EditMap(host, edit->label, edit->merge, [&](ClimateMap& m) {
                for (ClimateKind& k : m.kinds) {
                    if (k.id != kindId) continue;
                    const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<ClimateKind>(), &k, nullptr, edit->path);
                    if (pt.field) reflect::LoadField(*pt.field, pt.owner, edit->after, nullptr, nullptr);
                    break;
                }
            });
        } else {
            host.host.History().Push(std::make_unique<reflect::SetFieldCommand>(reflect::TypeOf<ClimateMap>(), edit->path, edit->before, edit->after,
                                                                               MapTarget(host), edit->label, edit->merge),
                                     false);
        }
        c.RequestRedraw();
    }

    // 選んだ形の行 (見出しと「形を消す」)。下の y を返す。
    float DrawShapeRow(ui::Context& c, const Rect& r, SceneViewHost& host, const std::wstring& text) {
        if (r.h < kRow) return r.y;
        Label(c, {r.x, r.y, r.w - 100, kRow}, text, true);
        if (w::Button(c, "climate.shape.remove", {r.Right() - 92, r.y, 92, kRow}, Tr(L"形を消す"), w::ButtonKind::Secondary, CanEdit(host))) RemoveSelected(host);
        return r.y + kRow + 8;
    }

    // 雲 / 雨雲の層: 塗る量 (選んだ形があれば、その形の量)。下の y を返す。
    float DrawAmountSide(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateShape* shape) {
        if (Layer() == MapLayer::Rain) return DrawRainSide(c, r, host, shape);
        float y = r.y;
        const float labelW = std::floor(r.w * 0.42f);
        if (shape) y = DrawShapeRow(c, {r.x, y, r.w, r.Bottom() - y}, host, Tr(L"選んだ形 (雲)"));
        Label(c, {r.x, y, labelW, kRow}, shape ? Tr(L"形の雲の量 (%)") : Tr(L"雲の量 (%)"));
        const Rect field{r.x + labelW, y, r.w - labelW, kRow};
        if (shape) {
            float value = shape->clouds;
            if (w::NumberField(c, "climate.shape.clouds", field, value, 0.f, 100.f, shapeAmountText_) && CanEdit(host)) {
                const size_t index = static_cast<size_t>(selected_);
                EditMap(host, "形の雲の量", 0xC10D5u + index, [index, value](ClimateMap& m) {
                    if (index < m.shapes.size()) m.shapes[index].clouds = std::clamp(value, 0.f, 100.f);
                });
            }
        } else {
            w::NumberField(c, "climate.clouds", field, state_.clouds, 0.f, 100.f, cloudText_);
        }
        y += kRow + 6;
        const float amount = shape ? shape->clouds : state_.clouds;
        w::DrawSwatch(c, {r.x, y + 3, 16, 16}, theme::MapClouds(amount / 100.f));
        Note(c, {r.x + 24, y, r.w - 24, 92}, Tr(L"塗った所は、空の雲がこの量になります。塗っていない所は、シーン全体の天気の雲のままです。雨を降らせるのは雨雲の層です。"));
        return y + 98;
    }

    // 雨雲の層: 雨の濃さ (目安を選ぶか数で) と、ペンの形のぼかし (選んだ形があれば、その形の値)。下の y を返す。
    float DrawRainSide(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateShape* shape) {
        static constexpr float kSteps[] = {10, 30, 60, 85, 100};  // (降水量の目盛りと同じ)
        float y = r.y;
        const float labelW = std::floor(r.w * 0.42f);
        if (shape) y = DrawShapeRow(c, {r.x, y, r.w, r.Bottom() - y}, host, Tr(L"選んだ形 (雨雲)"));
        float amount = shape ? shape->rain : state_.rain;
        float soft = shape ? shape->soft : state_.soft;
        bool amountChanged = false, softChanged = false;

        Label(c, {r.x, y, labelW, kRow}, shape ? Tr(L"形の雨の濃さ (%)") : Tr(L"雨の濃さ (%)"));
        amountChanged = w::NumberField(c, shape ? "climate.shape.rain" : "climate.rain", {r.x + labelW, y, r.w - labelW, kRow}, amount, 0.f, 100.f,
                                       shape ? shapeAmountText_ : rainText_);
        y += kRow + 4;
        {
            const std::wstring names[] = {Tr(L"ぽつぽつ"), Tr(L"小雨"), Tr(L"雨"), Tr(L"大雨"), Tr(L"嵐")};
            std::wstring tips[std::size(kSteps)];
            std::wstring_view labels[std::size(kSteps)], tipViews[std::size(kSteps)];
            int sel = -1;
            for (size_t i = 0; i < std::size(kSteps); ++i) {
                tips[i] = TrF(L"雨の濃さを {:.0f} % に", kSteps[i]);
                labels[i] = names[i], tipViews[i] = tips[i];
                if (std::fabs(amount - kSteps[i]) < 0.5f) sel = static_cast<int>(i);
            }
            const int pick = w::Segmented(c, shape ? "climate.shape.rainStep" : "climate.rainStep", {r.x, y, r.w, kRow}, labels, sel, tipViews);
            if (pick >= 0) amount = kSteps[pick], amountChanged = true;
        }
        y += kRow + 6;
        if (shape || state_.tool == 2) {  // (ぼかしはペンで囲んだ形だけ)
            Label(c, {r.x, y, labelW, kRow}, shape ? Tr(L"形のぼかし (%)") : Tr(L"ぼかし (%)"));
            softChanged = w::NumberField(c, shape ? "climate.shape.soft" : "climate.soft", {r.x + labelW, y, r.w - labelW, kRow}, soft, 0.f, 100.f,
                                         shape ? shapeSoftText_ : softText_);
            y += kRow + 6;
        }
        if (shape && CanEdit(host) && (amountChanged || softChanged)) {
            const size_t index = static_cast<size_t>(selected_);
            if (amountChanged) {
                EditMap(host, "形の雨の濃さ", 0x5A1Au + index, [index, amount](ClimateMap& m) {
                    if (index < m.shapes.size()) m.shapes[index].rain = std::clamp(amount, 0.f, 100.f);
                });
            }
            if (softChanged) {
                EditMap(host, "形のぼかし", 0x50F7u + index, [index, soft](ClimateMap& m) {
                    if (index < m.shapes.size()) m.shapes[index].soft = std::clamp(soft, 0.f, 100.f);
                });
            }
        } else if (!shape) {
            state_.rain = std::clamp(amount, 0.f, 100.f);
            state_.soft = std::clamp(soft, 0.f, 100.f);
        }
        w::DrawSwatch(c, {r.x, y + 3, 16, 16}, theme::MapRain(amount / 100.f));
        Note(c, {r.x + 24, y, r.w - 24, 132},
             Tr(L"雨雲を描いた所だけ、この濃さで降ります (シーン全体の降水量が 0 でも)。目盛りは降水量と同じです。ペンで囲んだ形は、真ん中がこの濃さで、縁に向かってだんだん弱くなります (ぼかし: 縁から真ん中までのうち、弱くなる幅。0 % で縁まで同じ濃さ)。筆はぼかしません。雨か雪かはその場所の気温で決まり、空もその上だけ厚い雲になります。"));
        return y + 138;
    }

    // 気候の層: 気候の種類の一覧と ＋ / 消す (選んだ形があれば、種類を選ぶとその形の種類が変わる)。下の y を返す。
    float DrawKindList(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateMap& map, const ClimateShape* shape) {
        const bool canEdit = CanEdit(host);
        float y = r.y;
        if (shape) {
            const ClimateKind* k = FindKind(map, shape->kind);
            y = DrawShapeRow(c, {r.x, y, r.w, r.Bottom() - y}, host, TrF(L"選んだ形: {}", k ? Utf8ToWide(TrU8(k->name)) : Tr(L"(無い種類)")));
        }
        Label(c, {r.x, y, r.w - 150, kRow}, Tr(L"気候の種類"), true);
        const ClimateKind* chosen = FindKind(map, state_.kind);
        if (w::Button(c, "climate.add", {r.Right() - 144, y, 76, kRow}, Tr(L"＋ 足す"), w::ButtonKind::Secondary, canEdit)) AddKind(host, map);
        if (w::Button(c, "climate.remove", {r.Right() - 64, y, 64, kRow}, Tr(L"消す"), w::ButtonKind::Secondary, canEdit && chosen)) RemoveChosen(host, map);
        y += kRow + 4;
        const float rowH = kRow + 2;
        const float content = rowH * static_cast<float>(map.kinds.size());
        const float listH = std::min(content, std::max(rowH * 3, (r.Bottom() - y) * 0.4f));
        const Rect list{r.x, y, r.w, listH};
        const float scroll = w::BeginScroll(c, "climate.kinds", list, content);
        ui::TextStyle ts;
        ts.color = theme::Text;
        ui::TextStyle dim;
        dim.color = theme::TextDim;
        dim.h = ui::HAlign::Right;
        for (size_t i = 0; i < map.kinds.size(); ++i) {
            const ClimateKind& k = map.kinds[i];
            const Rect row{list.x, list.y - scroll + rowH * static_cast<float>(i), list.w, kRow};
            if (w::Selectable(c, std::format("climate.k{}", k.id), row, k.id == state_.kind).clicked) {
                state_.kind = k.id;
                if (shape && shape->kind != k.id && canEdit) {  // 選んだ形の種類を変える
                    const size_t index = static_cast<size_t>(selected_);
                    const int id = k.id;
                    EditMap(host, "形の気候を変える", 0, [index, id](ClimateMap& m) {
                        if (index < m.shapes.size()) m.shapes[index].kind = id;
                    });
                }
            }
            w::DrawSwatch(c, {row.x + 6, row.y + 5, 16, 16}, k.color);
            c.P().Text({row.x + 30, row.y, row.w - 150, row.h}, Utf8ToWide(TrU8(k.name)), ts);
            c.P().Text({row.Right() - 124, row.y, 116, row.h}, TrF(L"{} ℃ ・ {} %", k.temperature, k.humidity), dim);
        }
        w::EndScroll(c, "climate.kinds", list, content);
        return list.Bottom() + 10;
    }

    // 種類を足す (選んでいた種類の気温と湿り気から始める)。足したものを選ぶ。
    void AddKind(SceneViewHost& host, const ClimateMap& map) {
        ClimateKind k;
        k.id = NewKindId(map);
        k.name = TrU8("新しい気候");
        k.color = ClimateKindColor(map.kinds.size());
        if (const ClimateKind* from = FindKind(map, state_.kind)) {
            k.temperature = from->temperature;
            k.humidity = from->humidity;
        }
        if (EditMap(host, "気候の種類を足す", 0, [&k](ClimateMap& m) {
                k.id = NewKindId(m);
                m.kinds.push_back(k);
            })) {
            state_.kind = k.id;
        }
    }

    // 選んだ種類と、それで塗った所を消す。
    void RemoveChosen(SceneViewHost& host, const ClimateMap& map) {
        const ClimateKind* k = FindKind(map, state_.kind);
        if (!k) return;
        const int id = k->id;
        EditMap(host, std::format("気候の種類「{}」を消す", k->name), 0, [id](ClimateMap& m) { RemoveKind(m, id); });
        state_.kind = 0;
        for (const ClimateKind& other : map.kinds) {
            if (other.id != id) {
                state_.kind = other.id;
                break;
            }
        }
    }

    // --- 「天候」の側: 描く層 (雲 / 雨雲)・筆・量・選んだ形と、シーン全体の天気 (ClimatePages.h の口に
    // 差し出された設定: インスペクターのシーンと同じもの) ---------------------------------------------
    void DrawWeatherSide(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateMap& map) {
        const bool canEdit = CanEdit(host);
        float y = r.y;
        const float labelW = std::floor(r.w * 0.42f);
        {
            Label(c, {r.x, y, labelW, kRow}, Tr(L"描く層"));
            const std::wstring layers[] = {Tr(L"雲"), Tr(L"雨雲")};
            const std::wstring tips[] = {Tr(L"雲の量を描きます (空の雲だけ。雨は降らせません)"), Tr(L"雨雲を描きます。描いた所だけ雨 (寒い所では雪) が降ります")};
            const std::wstring_view labels[] = {layers[0], layers[1]};
            const std::wstring_view tipViews[] = {tips[0], tips[1]};
            if (const int l = w::Segmented(c, "climate.weather.layer", {r.x + labelW, y, r.w - labelW, kRow}, labels, state_.weatherLayer == 0 ? 0 : 1, tipViews);
                l >= 0 && l != state_.weatherLayer) {
                state_.weatherLayer = l;
                selected_ = -1;
                pen_.clear();
            }
        }
        y += kRow + 6;
        y = DrawBrushRow(c, {r.x, y, r.w, r.Bottom() - y});
        y = DrawAmountSide(c, {r.x, y, r.w, r.Bottom() - y}, host, Selected(map));
        if (r.Bottom() - y < 60) return;
        Label(c, {r.x, y, r.w, kRow}, Tr(L"シーン全体の天気"), true);
        y += kRow;
        Note(c, {r.x, y, r.w, 58},
             host.playing ? Tr(L"Play 中は変えられません (■ で止めてから)")
                          : Tr(L"インスペクターのシーンと同じものです。地図に描いていない所全部に効きます。シーン全体に雨や雪を降らせるときは「降水量」を上げます (雨か雪かはその場所の気温)。"));
        y += 62;
        std::vector<inspector::Block> blocks;
        std::vector<const reflect::TypeDesc*> types;
        for (const auto& p : host.host.Registry().Contributions<ClimatePageInfo>(kClimatePagePoint)) {
            if (!p.item->type) continue;
            const reflect::TypeDesc& type = p.item->type();
            const game_production::Component* s = host.scene.FindSetting(type.name);
            if (!s || !s->Known()) continue;
            inspector::Block b;
            b.type = &type;
            b.object = const_cast<void*>(s->value.Get());  // (インスペクターは書かない)
            b.unknowns = &s->unknowns;
            b.title = p.item->label ? Tr(p.item->label) : Utf8ToWide(TrU8(type.label));
            b.readOnly = !canEdit;
            b.hidden = game_production::HiddenSettingFields(host.host.Registry(), type);  // (天候 × 気候 が隠す「雪にする」)
            // テンプレート (晴れ・雨・嵐 …): インスペクターと同じ行 (設定を出すプラグインの SceneSettingLook)。
            for (const auto& l : host.host.Registry().Contributions<game_production::SceneSettingLook>(game_production::kSceneSettingLookPoint)) {
                if (&l.item->type() != &type || !l.item->presets || !canEdit) continue;
                game_production::SceneSettingMenuHost mh;
                mh.host = &host.host;
                mh.play = host.playing;
                editor::SoftwareHost* soft = &host.host;
                const reflect::TypeDesc* tp = &type;
                const std::function<reflect::EditTarget()> resolve = host.setting(type.name);
                mh.edit = [soft, tp, resolve](std::string label, std::function<void(void*)> change) { EditSetting(*soft, *tp, resolve, label, 0, change); };
                mh.apply = mh.edit;
                b.presets = l.item->presets(mh);
                b.presetLabel = l.item->presetLabel ? l.item->presetLabel() : std::wstring();
                break;
            }
            blocks.push_back(std::move(b));
            types.push_back(&type);
        }
        if (blocks.empty()) {
            Note(c, {r.x, y, r.w, r.Bottom() - y}, Tr(L"プラグイン「天候」(とつなぎ「天候 × 気候」) が入ると、ここでシーンの天気を変えられます。"));
            return;
        }
        const auto edit = inspector::Draw(c, weatherInspector_, {r.x, y, r.w, r.Bottom() - y}, blocks);
        if (!edit || !canEdit || edit->block >= types.size()) return;
        const reflect::TypeDesc& type = *types[edit->block];
        host.host.History().Push(
            std::make_unique<reflect::SetFieldCommand>(type, edit->path, edit->before, edit->after, host.setting(type.name), edit->label, edit->merge), false);
        c.RequestRedraw();
    }

    ClimateTabState state_;
    reflect::Unknowns unknowns_;
    std::unique_ptr<game_production::SceneView> view_;  // 3D のシーンビュー (真上から描かせる)
    bool madeView_ = false;
    bool panning_ = false;
    bool stroking_ = false;  // 塗っている (左ボタンを離すと Undo の 1 件)
    MapLayer strokeLayer_ = MapLayer::Climate;
    PaintGrid stroke_;       // ... その途中の塗り
    Vec2 last_{0, 0};        // ... 前のフレームの筆の場所
    PaintGrid shown_;        // 描いている層の塗り (形を焼いたもの。地図の中身が変わったときだけ作り直す)
    ClimateMap shownMap_;
    MapLayer shownLayer_ = MapLayer::Climate;
    std::vector<Vec2> pen_;  // ペンで置いている点 (ワールドの x, z)。閉じると形になる
    bool penDragging_ = false;
    enum class Drag { None, Point, Move };
    int selected_ = -1;  // 選んだ形 (ClimateMap::shapes の番号)
    Drag drag_ = Drag::None;
    int dragPoint_ = -1;
    Vec2 dragFrom_{0, 0};
    ClimateShape dragOriginal_, dragShape_;  // 動かしている形の、始めと今
    ClimateMap dragMap_;                     // 動かしている形を入れた地図 (見せるため)
    ClimateKind kindCopy_;  // インスペクターに見せる、選んだ種類の写し
    std::string brushText_, cloudText_, rainText_, softText_, shapeAmountText_, shapeSoftText_;
    inspector::State inspector_, weatherInspector_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateClimateTab() { return std::make_unique<ClimateTab>(); }

}  // namespace rm::climate
