#include "rm_engine/climate/editor/Register.h"

#include "rm_engine/climate/editor/ClimateTab.h"
#include "rm_engine/climate/editor/ZoneLines.h"

#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "rm_engine/climate/runtime/Climate.h"
#include "rm_engine/game_production/editor/SceneView.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::climate {

namespace {

namespace theme = editor::theme;
using game_production::DebugDraw;
using game_production::DebugSpace;

constexpr int kCircle = 48;  // 円の線の分割数

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// 単位の長さの軸 (エンティティの向き)。
Vec3 UnitAxis(const space3d::Mat4& m, int c) {
    const Vec3 v{m.m[c][0], m.m[c][1], m.m[c][2]};
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 1e-12f) return Mul(v, 1 / len);
    return c == 0 ? Vec3{1, 0, 0} : c == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}

}  // namespace

// 3D のエリアの形を `inset` だけ内側に縮めたもの (縮めきったら描かない)。
void ZoneOutline3D(const ClimateField::Zone3D& z, float inset, Color color, DebugDraw& out) {
    ZoneBody b = BodyOf(z.zone, z.world);
    const Vec3 center{z.world.m[3][0], z.world.m[3][1], z.world.m[3][2]};
    const Vec3 ax = UnitAxis(z.world, 0), ay = UnitAxis(z.world, 1), az = UnitAxis(z.world, 2);
    auto at = [&](float x, float y, float zz) { return Add(center, Add(Mul(ax, x), Add(Mul(ay, y), Mul(az, zz)))); };
    auto line = [&](Vec3 a, Vec3 c) { out.Add(DebugSpace::Space3D, a, c, color, 0); };
    b.half = {b.half.x - inset, b.half.y - inset, b.half.z - inset};
    if (b.half.x <= 0 || b.half.y <= 0 || (b.kind == ZoneBody::Kind::Box && b.half.z <= 0)) return;
    const Vec3 h = b.half;
    if (b.kind == ZoneBody::Kind::Box) {
        for (const float y : {-h.y, h.y}) {  // 上と下の四角
            line(at(-h.x, y, -h.z), at(h.x, y, -h.z));
            line(at(h.x, y, -h.z), at(h.x, y, h.z));
            line(at(h.x, y, h.z), at(-h.x, y, h.z));
            line(at(-h.x, y, h.z), at(-h.x, y, -h.z));
        }
        for (const float x : {-h.x, h.x}) {
            for (const float zz : {-h.z, h.z}) line(at(x, -h.y, zz), at(x, h.y, zz));
        }
        return;
    }
    constexpr float kTurn = 2 * std::numbers::pi_v<float> / kCircle;
    for (const float y : {-h.y, h.y}) {  // 上と下の丸
        for (int i = 0; i < kCircle; ++i) {
            const float a0 = kTurn * static_cast<float>(i), a1 = kTurn * static_cast<float>(i + 1);
            line(at(std::cos(a0) * h.x, y, std::sin(a0) * h.x), at(std::cos(a1) * h.x, y, std::sin(a1) * h.x));
        }
    }
    for (int i = 0; i < 4; ++i) {
        const float a = kTurn * static_cast<float>(i * kCircle / 4);
        line(at(std::cos(a) * h.x, -h.y, std::sin(a) * h.x), at(std::cos(a) * h.x, h.y, std::sin(a) * h.x));
    }
}

namespace {

void Outline2D(const ClimateField::Zone2D& z, float inset, Color color, DebugDraw& out) {
    ZoneBody b = BodyOf(z.zone, z.world);
    const space2d::Mat3& m = z.world;
    const float lx = std::sqrt(m.a * m.a + m.b * m.b), ly = std::sqrt(m.c * m.c + m.d * m.d);
    const Vec3 ax = lx > 1e-12f ? Vec3{m.a / lx, m.b / lx, 0} : Vec3{1, 0, 0};
    const Vec3 ay = ly > 1e-12f ? Vec3{m.c / ly, m.d / ly, 0} : Vec3{0, 1, 0};
    const Vec3 center{m.tx, m.ty, 0};
    auto at = [&](float x, float y) { return Add(center, Add(Mul(ax, x), Mul(ay, y))); };
    auto line = [&](Vec3 a, Vec3 c) { out.Add(DebugSpace::Space2D, a, c, color, 0); };
    b.half = {b.half.x - inset, b.half.y - inset, 0};
    if (b.half.x <= 0 || (b.kind == ZoneBody::Kind::Rect && b.half.y <= 0)) return;
    const Vec3 h = b.half;
    if (b.kind == ZoneBody::Kind::Rect) {
        line(at(-h.x, -h.y), at(h.x, -h.y));
        line(at(h.x, -h.y), at(h.x, h.y));
        line(at(h.x, h.y), at(-h.x, h.y));
        line(at(-h.x, h.y), at(-h.x, -h.y));
        return;
    }
    constexpr float kTurn = 2 * std::numbers::pi_v<float> / kCircle;
    for (int i = 0; i < kCircle; ++i) {
        const float a0 = kTurn * static_cast<float>(i), a1 = kTurn * static_cast<float>(i + 1);
        line(at(std::cos(a0) * h.x, std::sin(a0) * h.x), at(std::cos(a1) * h.x, std::sin(a1) * h.x));
    }
}

// 選んだエリア (`all`: すべて) の縁と、境目の内側の縁。切ってあるエリアは描かない (効かないため)。
void ZoneLines(const game_production::Scene& scene, std::span<const int> selection, bool all, DebugDraw& out) {
    if (!all && selection.empty()) return;
    auto chosen = [&](int id) { return all || std::find(selection.begin(), selection.end(), id) != selection.end(); };
    const ClimateField field(scene);
    for (const ClimateField::Zone3D& z : field.Zones3D()) {
        if (!chosen(z.entity)) continue;
        ZoneOutline3D(z, 0, theme::SceneArea(), out);
        if (z.zone.border > 0) ZoneOutline3D(z, z.zone.border, theme::SceneAreaInner(), out);
    }
    for (const ClimateField::Zone2D& z : field.Zones2D()) {
        if (!chosen(z.entity)) continue;
        Outline2D(z, 0, theme::SceneArea(), out);
        if (z.zone.border > 0) Outline2D(z, z.zone.border, theme::SceneAreaInner(), out);
    }
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const game_production::SceneLinesInfo kLines{&ZoneLines};
    registry.Contribute(game_production::kSceneLinesPoint, kLines);
    // 「気候」タブ: シーンを真上から見た地図に気候を塗る・天候を変える (ClimateTab.h)。
    static const game_production::ViewPanelInfo kTab{"climate", editor::TrKey(L"気候"), &CreateClimateTab};
    registry.Contribute(game_production::kViewPanelPoint, kTab);
}

}  // namespace rm::climate
