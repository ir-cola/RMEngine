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
        const std::wstring tips[] = {Tr(L"地図に気候 (気温と湿り気) を塗ります"), Tr(L"シーンの天気 (雲・雨や雪の量・霧・風) を変えます")};
        const std::wstring_view modeLabels[] = {modes[0], modes[1]};
        const std::wstring_view modeTips[] = {tips[0], tips[1]};
        if (const int picked = w::Segmented(c, "climate.mode", {bar.x, bar.y, std::min(220.f, bar.w * 0.5f), bar.h}, modeLabels, state_.mode == 1 ? 1 : 0, modeTips);
            picked >= 0) {
            state_.mode = picked;
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

        DrawMap(c, mapR, host, *map);
        if (side.w >= 60 && side.h >= 60) {
            if (state_.mode == 1) {
                DrawWeatherSide(c, side, host);
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
        const ClimateField field(host.scene);
        for (const ClimateField::Zone3D& z : field.Zones3D()) {
            const ZoneBody b = BodyOf(z.zone, z.world);
            const float reach = std::sqrt(b.half.x * b.half.x + b.half.z * b.half.z);
            add(z.world.m[3][0] - reach, z.world.m[3][2] - reach);
            add(z.world.m[3][0] + reach, z.world.m[3][2] + reach);
        }
        const PaintGrid g = PaintOf(map);
        if (!g.Empty()) {
            const float cs = CellSizeOf(map);
            add(static_cast<float>(g.cellX) * cs, static_cast<float>(g.cellZ) * cs);
            add(static_cast<float>(g.cellX + g.width) * cs, static_cast<float>(g.cellZ + g.depth) * cs);
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
    void DrawMap(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateMap& map) {
        if (r.w < 20 || r.h < 20) return;
        HandleInput(c, r, host, map);
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
        DrawPaint(c, r, map);
        DrawZones(c, r, host);
        const MapView v = ViewOf(r);
        const ui::Input& in = c.In();
        if (state_.mode == 0 && CanEdit(host) && c.IsHovered(r) && !panning_) DrawBrushAt(c, v, in.mouseX, in.mouseY);
        // 下に、縮尺と使い方。上に、描けないわけ。
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        const float cs = CellSizeOf(map);
        c.P().Text({r.x + 8, r.Bottom() - 38, r.w - 16, 16}, TrF(L"1 マス {} m ・ 地図の幅 {} m", cs, static_cast<int>(std::round(state_.halfWidth * 2))), ts);
        c.P().Text({r.x + 8, r.Bottom() - 20, r.w - 16, 16},
                   state_.mode == 0 ? Tr(L"左ドラッグ: 塗る ・ ホイール: 拡大縮小 ・ 右 / 中ドラッグ: 動かす") : Tr(L"ホイール: 拡大縮小 ・ 右 / 中ドラッグ: 動かす"), ts);
        if (!view_) Note(c, r.Inset(16), Tr(L"3D 空間のプラグイン (ゲーム制作 × 3D) が入ると、ここにシーンを真上から見た絵が出ます。塗ることはできます。"));
        if (host.playing) Note(c, {r.x + 8, r.y + 8, r.w - 16, 40}, Tr(L"Play 中は塗れません (■ で止めてから)"));
        c.PopClip();
    }

    void DrawBrushAt(ui::Context& c, const MapView& v, float sx, float sy) const { w::DrawMapBrush(c, sx, sy, state_.brush * v.scale, state_.erase); }

    // ホイール: カーソルの所を中心に拡大縮小。右 / 中ドラッグ: 動かす。左ドラッグ: 塗る (離したら Undo の 1 件)。
    void HandleInput(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateMap& map) {
        const ui::Input& in = c.In();
        const bool hovered = c.IsHovered(r);
        if (hovered && in.wheel != 0) {
            const MapView v = ViewOf(r);
            const Vec2 at = v.ToWorld(in.mouseX, in.mouseY);
            const float hw = std::clamp(state_.halfWidth * std::pow(0.85f, in.wheel), kMinHalfWidth, kMaxHalfWidth);
            const float scale = r.w / (2.f * hw);
            state_.center = {at.x - (in.mouseX - v.CX()) / scale, at.y + (in.mouseY - v.CY()) / scale};
            state_.halfWidth = hw;
            c.RequestRedraw();
        }
        if (hovered && (c.MousePressed(ui::Mouse::Right) || c.MousePressed(ui::Mouse::Middle))) panning_ = true;
        if (panning_) {
            if (in.down[1] || in.down[2]) {
                const float scale = ViewOf(r).scale;
                state_.center.x -= in.mouseDX / scale;
                state_.center.y += in.mouseDY / scale;
                if (in.mouseDX != 0 || in.mouseDY != 0) c.RequestRedraw();
            } else {
                panning_ = false;
            }
        }
        const auto b = c.ButtonBehavior(c.MakeId("climate.map"), r);
        const int id = state_.erase ? 0 : state_.kind;
        // (押して離したのが同じフレームに届いたクリックも、1 回塗る)
        const bool down = b.held || (b.clicked && c.MousePressed());
        if (down && state_.mode == 0 && CanEdit(host) && (state_.erase || FindKind(map, id))) {
            const Vec2 at = ViewOf(r).ToWorld(in.mouseX, in.mouseY);
            const float cs = CellSizeOf(map);
            if (!stroking_) {
                stroking_ = true;
                stroke_ = PaintOf(map);
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
            EditMap(host, state_.erase ? "気候の地図を消す" : "気候の地図に塗る", 0, [&done](ClimateMap& m) { SetPaint(m, done); });
        }
    }

    // 塗った所: 種類の色で半透明に。行ごとに、同じ種類の続きを 1 つの四角にする。小さすぎるマスは
    // 何マスかおきに見る (縮めて見たときに描く数を抑える)。
    void DrawPaint(ui::Context& c, const Rect& r, const ClimateMap& map) {
        if (!stroking_ && (map.cells != shownCells_ || map.cellX != shown_.cellX || map.cellZ != shown_.cellZ || map.width != shown_.width ||
                           map.depth != shown_.depth)) {
            shown_ = PaintOf(map);
            shownCells_ = map.cells;
        }
        const PaintGrid& g = stroking_ ? stroke_ : shown_;
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
                    if (const auto it = colors.find(id); it != colors.end()) {
                        const Vec2 a = v.ToScreen(static_cast<float>(i) * cs, static_cast<float>(jEnd) * cs);
                        const Vec2 b = v.ToScreen(static_cast<float>(iEnd) * cs, static_cast<float>(j) * cs);
                        w::DrawMapPaint(c, {a.x, a.y, b.x - a.x, b.y - a.y}, it->second);
                    }
                }
                i = k;
            }
        }
    }

    // 気候エリア (3D) の縁と、境目の内側の縁 (シーンビューの枠と同じ線を、真上から)。
    void DrawZones(ui::Context& c, const Rect& r, const SceneViewHost& host) {
        const ClimateField field(host.scene);
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

    // --- 「気候」の側: 道具・筆・マスの大きさ・気候の種類・選んだ種類の値 ------------------------------
    void DrawClimateSide(ui::Context& c, const Rect& r, SceneViewHost& host, const ClimateMap& map) {
        const bool canEdit = CanEdit(host);
        float y = r.y;
        const std::wstring tools[] = {Tr(L"ペン"), Tr(L"消しゴム")};
        const std::wstring_view toolLabels[] = {tools[0], tools[1]};
        if (const int t = w::Segmented(c, "climate.tool", {r.x, y, r.w, kRow}, toolLabels, state_.erase ? 1 : 0); t >= 0) state_.erase = t == 1;
        y += kRow + 8;
        const float labelW = std::floor(r.w * 0.5f);
        Label(c, {r.x, y, labelW, kRow}, Tr(L"筆の半径 (m)"));
        w::NumberField(c, "climate.brush", {r.x + labelW, y, r.w - labelW, kRow}, state_.brush, 0.5f, 10000.f, brushText_);
        y += kRow + 6;
        Label(c, {r.x, y, labelW, kRow}, Tr(L"マスの大きさ (m)"));
        {
            std::vector<std::wstring> texts;
            int selected = -1;
            for (size_t i = 0; i < std::size(kCellChoices); ++i) {
                texts.push_back(std::format(L"{:g}", kCellChoices[i]));
                if (kCellChoices[i] == map.cellSize) selected = static_cast<int>(i);
            }
            const std::vector<std::wstring_view> labels(texts.begin(), texts.end());
            const std::wstring tip = Tr(L"細かいほど形をきれいに塗れます。変えると、塗った所はそのままの場所で塗り直されます");
            const std::vector<std::wstring_view> tips(labels.size(), tip);
            const int pick = w::Segmented(c, "climate.cell", {r.x + labelW, y, r.w - labelW, kRow}, labels, selected, tips);
            if (pick >= 0 && canEdit && kCellChoices[pick] != map.cellSize) {
                const float to = kCellChoices[pick];
                EditMap(host, std::format("マスの大きさを {:g} m に", to), 0, [to](ClimateMap& m) {
                    const PaintGrid g = Resample(PaintOf(m), m.cellSize, to);
                    m.cellSize = to;
                    SetPaint(m, g);
                });
            }
        }
        y += kRow + 12;

        // 気候の種類: 見出しと ＋ / 消す、その下に一覧。
        Label(c, {r.x, y, r.w - 150, kRow}, Tr(L"気候の種類"), true);
        const ClimateKind* chosen = FindKind(map, state_.kind);
        if (w::Button(c, "climate.add", {r.Right() - 144, y, 76, kRow}, Tr(L"＋ 足す"), w::ButtonKind::Secondary, canEdit)) AddKind(host, map);
        if (w::Button(c, "climate.remove", {r.Right() - 64, y, 64, kRow}, Tr(L"消す"), w::ButtonKind::Secondary, canEdit && chosen)) RemoveChosen(host, map);
        chosen = FindKind(map, state_.kind);  // (足す・消すで一覧が変わったかもしれない)
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
            if (w::Selectable(c, std::format("climate.k{}", k.id), row, k.id == state_.kind).clicked) state_.kind = k.id;
            w::DrawSwatch(c, {row.x + 6, row.y + 5, 16, 16}, k.color);
            c.P().Text({row.x + 30, row.y, row.w - 150, row.h}, Utf8ToWide(TrU8(k.name)), ts);
            c.P().Text({row.Right() - 124, row.y, 116, row.h}, TrF(L"{} ℃ ・ {} %", k.temperature, k.humidity), dim);
        }
        w::EndScroll(c, "climate.kinds", list, content);
        y = list.Bottom() + 10;
        if (r.Bottom() - y < 40) return;

        // 選んだ種類の値 (名前・色・気温・湿り気) と、地図の境目の幅: インスペクターと同じ描き方。
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

    // --- 「天候」の側: シーンの天気 (ClimatePages.h の口に差し出された設定) ------------------------
    // 段階 3 (docs/CLIMATE.md): エリアごとの天気をこの地図に塗れるようにする。今はシーン全体で 1 つ。
    void DrawWeatherSide(ui::Context& c, const Rect& r, SceneViewHost& host) {
        const bool canEdit = CanEdit(host);
        float y = r.y;
        Note(c, {r.x, y, r.w, 58},
             host.playing ? Tr(L"Play 中は変えられません (■ で止めてから)")
                          : Tr(L"天気はシーン全体で 1 つです。雨か雪かは、その場所の気温 (塗った気候と気候エリア) で決まります。"));
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
    PaintGrid stroke_;       // ... その途中の塗り
    Vec2 last_{0, 0};        // ... 前のフレームの筆の場所
    PaintGrid shown_;        // 描いている地図の塗り (cells が変わったときだけ読み直す)
    std::string shownCells_;
    ClimateKind kindCopy_;  // インスペクターに見せる、選んだ種類の写し
    std::string brushText_;
    inspector::State inspector_, weatherInspector_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateClimateTab() { return std::make_unique<ClimateTab>(); }

}  // namespace rm::climate
