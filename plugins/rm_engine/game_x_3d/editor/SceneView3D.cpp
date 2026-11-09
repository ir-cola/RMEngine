#include "rm_engine/game_x_3d/editor/SceneView3D.h"

#include "base/Log.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_x_3d/editor/SceneViewState.h"
#include "rm_engine/space3d/editor/Gizmo.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/View.h"
#include "ui/Path.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace rm::game_x_3d {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
namespace gizmo = space3d::gizmo;
using editor::Tr;
using space3d::Mat4;
using space3d::Transform;
using space3d::operator*;  // Vec3 is rm::Vec3: its 3D operators live in space3d
using space3d::operator+;
using space3d::operator-;
using ui::Rect;

constexpr float kLookDegreesPerDip = 0.25f;
constexpr float kOrbitDegreesPerDip = 0.4f;
constexpr float kClickSlop = 4.f;  // a press that moves less than this is a click
constexpr int kGridHalf = 20;      // the grid goes from -20 to 20 (1 per line, darker every 10)
constexpr float kBarH = 28.f;      // the tool strip at the top left
constexpr float kToolW = 196.f, kSpaceW = 168.f, kBarGap = 8.f;

struct Placed {
    int id;
    Mat4 world;
    Mat4 parentWorld;
    bool active;
};

// One thing being dragged by the gizmo: as it was when the drag began, and where it is now.
struct Edited {
    Transform start;
    Mat4 parentWorld, startWorld;
    std::function<reflect::EditTarget()> resolve;
};

const char* FieldOf(gizmo::Tool t) {
    switch (t) {
        case gizmo::Tool::Move: return "position";
        case gizmo::Tool::Rotate: return "rotation";
        case gizmo::Tool::Scale: return "scale";
    }
    return "position";
}

std::optional<size_t> FieldIndex(const char* name) {
    const auto fields = reflect::TypeOf<Transform>().fields;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (std::string_view(fields[i].name) == name) return i;
    }
    return std::nullopt;
}

class SceneView3D final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 8 || r.h < 8) return;
        if (!Ready(host)) {
            ui::TextStyle ts;
            ts.size = 13;
            ts.wrap = true;
            ts.color = theme::Danger;
            ts.h = ui::HAlign::Center;
            c.P().Text(r.Inset(24), Tr(L"3D の描画を始められませんでした。くわしくはコンソールを見てください。"), ts);
            return;
        }
        transformType_ = reflect::TypeOf<Transform>().name;
        Place(host.scene);
        const std::optional<Rect> bar = BarRect(r);
        if (HandleInput(c, r, host, bar)) Place(host.scene);  // a gizmo drag moved things this frame
        Render(c, r, host);
        DrawGizmo(c, r, host);
        if (bar) DrawBar(c, *bar);
        DrawHints(c, r);
    }

    void LoadState(const json::Value& s) override {
        state_ = {};
        unknowns_ = {};
        reflect::Warnings warnings;
        if (s.IsObject()) reflect::Load(state_, s, &unknowns_, &warnings);
        for (const auto& w : warnings) log::Warn("シーンビューの見え方: {}", w);
    }
    json::Value SaveState() const override { return reflect::Save(state_, &unknowns_); }

private:
    bool Ready(game_production::SceneViewHost& host) {
        if (!tried_) {
            tried_ = true;
            std::string err;
            if (!renderer_.Init(host.host.Graphics().D3D(), &err)) log::Error("シーンビュー: {}", err);
        }
        return renderer_.Ready();
    }

    // Where every entity with a Transform is in the world (its parents' Transforms included).
    void Place(const game_production::Scene& scene) {
        placed_.clear();
        std::unordered_map<int, Mat4> world;  // parents come before their children in TreeOrder
        for (const auto& t : scene.TreeOrder()) {
            const auto& e = *t.entity;
            const auto it = world.find(e.data.parent);
            const Mat4 parent = it != world.end() ? it->second : Mat4{};
            const game_production::Component* comp = e.FindComponent(transformType_);
            if (comp && comp->Known()) {
                const Mat4 w = space3d::WorldMatrix(parent, *static_cast<const Transform*>(comp->value.Get()));
                world[e.data.id] = w;
                placed_.push_back({e.data.id, w, parent, t.activeInHierarchy});
            } else {
                world[e.data.id] = parent;  // no place of its own: its children follow the parent
            }
        }
    }

    const Placed* FindPlaced(int id) const {
        for (const Placed& p : placed_) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    // The gizmo sits on the last selected entity that has a place (the one clicked last).
    const Placed* Primary(const game_production::SceneViewHost& host) const {
        for (auto it = host.selection.rbegin(); it != host.selection.rend(); ++it) {
            if (const Placed* p = FindPlaced(*it)) return p;
        }
        return nullptr;
    }

    std::optional<gizmo::Frame> GizmoFrame(const Rect& r, const game_production::SceneViewHost& host) const {
        const Placed* p = Primary(host);
        if (!p) return std::nullopt;
        return gizmo::MakeFrame(p->world, state_.tool, state_.space, state_.camera, r.h);
    }

    Mat4 ViewProjection(const Rect& r) const { return state_.camera.ProjectionMatrix(r.w / r.h) * state_.camera.ViewMatrix(); }

    // The tool strip (top left): shown when the view is wide enough for it.
    static std::optional<Rect> BarRect(const Rect& r) {
        const float wNeeded = kToolW + kBarGap + kSpaceW;
        if (r.w < wNeeded + 24 || r.h < kBarH + 60) return std::nullopt;
        return Rect{r.x + 8, r.y + 8, wNeeded, kBarH};
    }

    // Returns true when a gizmo drag changed the scene this frame.
    bool HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const std::optional<Rect>& bar) {
        const ui::Input& in = c.In();
        const double now = c.Time();
        const float dt = static_cast<float>(std::clamp(now - lastTime_, 0.0, 0.1));
        lastTime_ = now;
        const bool overBar = bar && bar->Contains(in.mouseX, in.mouseY);
        const bool hovered = c.IsHovered(r) && !overBar;
        // How far the mouse moved (still counted while the cursor is held at the drag's start).
        const float dx = in.mouseDX, dy = in.mouseDY;
        const Vec2 mouse{in.mouseX, in.mouseY};
        bool changed = false;

        const std::optional<gizmo::Screen> screen = gizmo::MakeScreen(ViewProjection(r), r.x, r.y, r.w, r.h);
        const std::optional<gizmo::Frame> frame = GizmoFrame(r, host);
        // The handle under the mouse (yellow), when nothing else is going on.
        if (drag_ == Drag::None) {
            hot_ = hovered && !in.alt && frame && screen
                       ? gizmo::HitTest(state_.tool, *frame, *screen, state_.camera.Eye(), mouse)
                       : gizmo::Handle::None;
        } else if (drag_ != Drag::Gizmo) {
            hot_ = gizmo::Handle::None;  // looking around: no handle lights up
        }

        // Start a drag (only inside the view); it goes on outside until the button is let go.
        if (drag_ == Drag::None && hovered) {
            if (c.MousePressed(ui::Mouse::Right)) {
                drag_ = Drag::Look;
            } else if (c.MousePressed(ui::Mouse::Middle)) {
                drag_ = Drag::Pan;
            } else if (c.MousePressed() && in.alt) {
                drag_ = Drag::Orbit;
            } else if (c.MousePressed() && hot_ != gizmo::Handle::None && BeginGizmo(host, *frame, *screen, mouse)) {
                drag_ = Drag::Gizmo;
            } else if (c.MousePressed()) {
                drag_ = Drag::Click;
                pressX_ = in.mouseX;
                pressY_ = in.mouseY;
            }
        } else if (drag_ != Drag::None) {
            const bool held = drag_ == Drag::Look ? in.down[1] : drag_ == Drag::Pan ? in.down[2] : in.down[0];
            if (!held) {
                if (drag_ == Drag::Click && std::hypot(in.mouseX - pressX_, in.mouseY - pressY_) < kClickSlop) Pick(r, host);
                if (drag_ == Drag::Gizmo) EndGizmo(host);
                drag_ = Drag::None;
            } else if (drag_ == Drag::Look) {
                space3d::Look(state_.camera, -dx * kLookDegreesPerDip, dy * kLookDegreesPerDip);
                Fly(c, dt);
            } else if (drag_ == Drag::Pan) {
                space3d::Pan(state_.camera, dx, dy, r.h);
            } else if (drag_ == Drag::Orbit) {
                space3d::Orbit(state_.camera, -dx * kOrbitDegreesPerDip, dy * kOrbitDegreesPerDip);
            } else if (drag_ == Drag::Gizmo) {
                if (c.KeyPressed(ui::Key::Escape)) {
                    CancelGizmo();  // everything back where it was; the button still has to be let go
                    drag_ = Drag::Cancelled;
                } else if (screen) {
                    gizmo::Snap snap;
                    snap.on = in.ctrl;
                    if (const auto change = gizmo::Update(*gizmo_, *screen, mouse, snap)) ApplyGizmo(*change);
                }
                changed = true;
            }
        }
        // Turning / sliding never stops at the edge of the screen: the cursor hides and stays where the
        // drag started (from its first frame), and comes back there when the button is let go.
        if (drag_ == Drag::Look || drag_ == Drag::Pan || drag_ == Drag::Orbit) c.LockMouse();
        if (hovered && in.wheel != 0.f) space3d::Dolly(state_.camera, in.wheel);
        if (hovered && c.Focused() == 0 && drag_ != Drag::Look && drag_ != Drag::Gizmo) {
            if (c.KeyPressed(ui::Key::F)) FrameSelection(host);
            if (c.KeyPressed(ui::Key::W)) state_.tool = gizmo::Tool::Move;
            if (c.KeyPressed(ui::Key::E)) state_.tool = gizmo::Tool::Rotate;
            if (c.KeyPressed(ui::Key::R)) state_.tool = gizmo::Tool::Scale;
            if (c.KeyPressed(ui::Key::X)) ToggleSpace();
        }
        return changed;
    }

    void ToggleSpace() { state_.space = state_.space == gizmo::Space::Local ? gizmo::Space::World : gizmo::Space::Local; }

    // While looking around (right button held): W A S D forward / left / back / right, E Q up / down.
    void Fly(ui::Context& c, float dt) {
        auto axis = [&](ui::Key plus, ui::Key minus) { return (c.KeyDown(plus) ? 1.f : 0.f) - (c.KeyDown(minus) ? 1.f : 0.f); };
        const Vec3 dir{axis(ui::Key::D, ui::Key::A), axis(ui::Key::E, ui::Key::Q), axis(ui::Key::W, ui::Key::S)};
        if (dir.x == 0 && dir.y == 0 && dir.z == 0) return;
        const float speed = std::max(2.f, state_.camera.distance * 0.8f) * (c.In().shift ? 4.f : 1.f);
        space3d::Fly(state_.camera, dir * (speed * dt));
        c.RequestAnimation();  // keeps flying while the keys are held, without moving the mouse
    }

    // --- The gizmo ------------------------------------------------------------------------------
    // Every selected thing with a place moves with the gizmo - but not one whose parent is selected
    // too (it moves with its parent already).
    bool BeginGizmo(game_production::SceneViewHost& host, const gizmo::Frame& frame, const gizmo::Screen& screen, Vec2 mouse) {
        auto drag = gizmo::BeginDrag(state_.tool, hot_, frame, screen, state_.camera.Eye(), mouse);
        if (!drag || !host.component) return false;
        edited_.clear();
        for (const int id : host.scene.Roots(host.selection)) {
            const Placed* p = FindPlaced(id);
            if (!p) continue;
            auto resolve = host.component(id, transformType_);
            const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
            if (!t.object) continue;
            edited_.push_back({*static_cast<const Transform*>(t.object), p->parentWorld, p->world, std::move(resolve)});
        }
        if (edited_.empty()) return false;
        gizmo_ = std::move(drag);
        dragTool_ = state_.tool;
        dragSpace_ = state_.space;
        return true;
    }

    // While dragging, the Transforms are changed directly (the inspector follows); the history gets
    // one entry when the button is let go.
    void ApplyGizmo(const gizmo::Change& change) {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (!t.object) continue;  // gone (undone while dragging)
            auto* tr = static_cast<Transform*>(t.object);
            *tr = gizmo::Apply(e.start, e.parentWorld, e.startWorld, change, dragSpace_, tr->rotation);
        }
    }

    void CancelGizmo() {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (t.object) *static_cast<Transform*>(t.object) = e.start;
        }
        edited_.clear();
        gizmo_.reset();
    }

    // Edit = undo entry = unsaved (rule 7): one entry for the whole drag, whatever was moved.
    void EndGizmo(game_production::SceneViewHost& host) {
        const auto index = FieldIndex(FieldOf(dragTool_));
        const char* label = dragTool_ == gizmo::Tool::Move ? "移動" : dragTool_ == gizmo::Tool::Rotate ? "回転" : "拡大縮小";
        auto group = std::make_unique<CommandGroup>(label);
        if (index) {
            const reflect::TypeDesc& type = reflect::TypeOf<Transform>();
            const reflect::FieldDesc& field = type.fields[*index];
            for (const Edited& e : edited_) {
                const reflect::EditTarget t = e.resolve();
                if (!t.object) continue;
                json::Value before = reflect::SaveField(field, &e.start, nullptr);
                json::Value after = reflect::SaveField(field, t.object, nullptr);
                if (before == after) continue;
                group->Add(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{*index}, std::move(before), std::move(after),
                                                                      e.resolve, label));
            }
        }
        if (!group->Empty()) host.host.History().Push(std::move(group));  // already applied while dragging
        edited_.clear();
        gizmo_.reset();
    }

    void Pick(const Rect& r, game_production::SceneViewHost& host) {
        const auto inv = space3d::Inverse(ViewProjection(r));
        if (!inv) return;
        const float ndcX = (pressX_ - r.x) / r.w * 2.f - 1.f;
        const float ndcY = 1.f - (pressY_ - r.y) / r.h * 2.f;
        const space3d::Ray ray = space3d::ScreenRay(*inv, ndcX, ndcY);
        std::optional<float> best;
        int hit = 0;
        for (const Placed& p : placed_) {
            const auto t = space3d::IntersectPlacedCube(ray, p.world);
            if (t && (!best || *t < *best)) {
                best = t;
                hit = p.id;
            }
        }
        host.select(best ? std::vector<int>{hit} : std::vector<int>{});
    }

    void FrameSelection(game_production::SceneViewHost& host) {
        if (const Placed* p = Primary(host)) {
            const Vec3 center = space3d::TransformPoint(p->world, {0, 0, 0});
            const float size = space3d::Length(space3d::TransformDirection(p->world, {1, 1, 1}));
            space3d::Frame(state_.camera, center, std::max(size * 0.5f, 0.5f));
        }
    }

    void Render(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) {
        // The grid on the floor (y = 0) and the axes through the middle.
        lines_.clear();
        auto line = [&](Vec3 a, Vec3 b, ui::Color col) {
            lines_.push_back({a, col});
            lines_.push_back({b, col});
        };
        const float h = static_cast<float>(kGridHalf);
        for (int i = -kGridHalf; i <= kGridHalf; ++i) {
            const float v = static_cast<float>(i);
            const ui::Color minor = i % 10 == 0 ? theme::SceneGridMajor() : theme::SceneGrid();
            line({v, 0, -h}, {v, 0, h}, i == 0 ? theme::AxisZ() : minor);  // along Z (the line x = 0 is the Z axis)
            line({-h, 0, v}, {h, 0, v}, i == 0 ? theme::AxisX() : minor);  // along X
        }
        line({0, 0, 0}, {0, 1.5f, 0}, theme::AxisY());

        boxes_.clear();
        for (const Placed& p : placed_) {
            const bool selected = std::find(host.selection.begin(), host.selection.end(), p.id) != host.selection.end();
            const ui::Color col = selected ? theme::SceneObjectSelected() : p.active ? theme::SceneObject() : theme::SceneObjectInactive();
            boxes_.push_back({p.world, col});
            if (selected) Outline(p.world, theme::Accent);
        }

        // The picture in pixels of this window (sharp on high-DPI screens).
        ID2D1DeviceContext* dc = c.P().DC();
        float dpiX = 96, dpiY = 96;
        dc->GetDpi(&dpiX, &dpiY);
        const float scale = dpiX / 96.f;
        space3d::RenderFrame f;
        f.width = static_cast<uint32_t>(std::max(1.f, std::round(r.w * scale)));
        f.height = static_cast<uint32_t>(std::max(1.f, std::round(r.h * scale)));
        f.view = state_.camera.ViewMatrix();
        f.projection = state_.camera.ProjectionMatrix(r.w / r.h);
        f.clear = theme::SceneBackground();
        f.lines = lines_;
        f.boxes = boxes_;

        // Direct2D's drawing so far goes to the GPU first; then Direct3D draws into its own picture.
        dc->Flush();
        ID3D11Texture2D* picture = renderer_.Render(host.host.Graphics().D3DContext(), f);
        if (!picture) return;
        if (picture != bitmapTexture_ || dc != bitmapDc_ || !bitmap_) {
            bitmap_.Reset();
            Microsoft::WRL::ComPtr<IDXGISurface> surface;
            if (FAILED(picture->QueryInterface(IID_PPV_ARGS(&surface)))) return;
            const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpiX, dpiY);
            if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap_))) return;
            bitmapTexture_ = picture;
            bitmapDc_ = dc;
        }
        c.P().Image(bitmap_.Get(), r);
    }

    // The 12 edges of the placed unit cube.
    void Outline(const Mat4& w, ui::Color col) {
        Vec3 k[8];
        for (int i = 0; i < 8; ++i) {
            k[i] = space3d::TransformPoint(w, {(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f});
        }
        const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges) {
            lines_.push_back({k[e[0]], col});
            lines_.push_back({k[e[1]], col});
        }
    }

    // The gizmo over the picture, always in front (drawn on the screen from gizmo::'s shapes: what is
    // drawn is what is grabbed).
    void DrawGizmo(ui::Context& c, const Rect& r, const game_production::SceneViewHost& host) {
        const auto frame = GizmoFrame(r, host);
        const auto screen = gizmo::MakeScreen(ViewProjection(r), r.x, r.y, r.w, r.h);
        if (!frame || !screen) return;
        const gizmo::Frame& f = *frame;
        const gizmo::Screen& s = *screen;
        const auto o = s.ToScreen(f.origin);
        if (!o) return;
        const gizmo::Handle active = drag_ == Drag::Gizmo && gizmo_ ? gizmo_->handle : hot_;
        const gizmo::Tool tool = drag_ == Drag::Gizmo ? dragTool_ : state_.tool;
        auto& p = c.P();
        auto axisColor = [&](int a) { return a == 0 ? theme::AxisX() : a == 1 ? theme::AxisY() : theme::AxisZ(); };
        auto handleOf = [](int a) { return static_cast<gizmo::Handle>(static_cast<int>(gizmo::Handle::X) + a); };
        auto colorOf = [&](gizmo::Handle h, int a) { return h == active ? theme::GizmoHot() : axisColor(a); };
        auto square = [&](Vec2 m, float half, ui::Color col) {
            ui::Path sq;
            sq.Begin({m.x - half, m.y - half});
            sq.Line({m.x + half, m.y - half});
            sq.Line({m.x + half, m.y + half});
            sq.Line({m.x - half, m.y + half});
            p.FillPath(sq, col);
        };

        p.PushClip(r);
        if (tool == gizmo::Tool::Rotate) {
            // The halves behind first and faint (each ring reads as a whole circle), then the front.
            for (const bool front : {false, true}) {
                for (int a = 0; a < 3; ++a) {
                    const bool hot = handleOf(a) == active;
                    const ui::Color col = colorOf(handleOf(a), a).Alpha(front ? 1.f : hot ? 0.6f : 0.3f);
                    const float width = front ? (hot ? 3.f : 2.f) : 1.5f;
                    for (const auto& arc : gizmo::RingArcs(f, a, state_.camera.Eye(), front)) {
                        for (size_t i = 0; i + 1 < arc.size(); ++i) {
                            const auto a0 = s.ToScreen(arc[i]), a1 = s.ToScreen(arc[i + 1]);
                            if (a0 && a1) p.Line(a0->x, a0->y, a1->x, a1->y, col, width);
                        }
                    }
                }
            }
        } else {
            if (tool == gizmo::Tool::Move) {
                for (gizmo::Handle plane : {gizmo::Handle::YZ, gizmo::Handle::ZX, gizmo::Handle::XY}) {
                    std::array<Vec2, 4> q;
                    if (!gizmo::PlaneShown(f, s, plane, &q)) continue;
                    const ui::Color col = colorOf(plane, gizmo::AxisOf(plane));
                    ui::Path quad;
                    quad.Begin(q[0]);
                    for (size_t i = 1; i < 4; ++i) quad.Line(q[i]);
                    p.FillPath(quad, col.Alpha(plane == active ? 0.55f : 0.28f));
                    for (size_t i = 0; i < 4; ++i) p.Line(q[i].x, q[i].y, q[(i + 1) % 4].x, q[(i + 1) % 4].y, col.Alpha(0.9f), 1.f);
                }
            }
            for (int a = 0; a < 3; ++a) {
                if (!gizmo::AxisShown(f, s, a)) continue;
                const ui::Color col = colorOf(handleOf(a), a);
                const auto tip = s.ToScreen(gizmo::AxisEnd(f, a));
                if (!tip) continue;
                if (tool == gizmo::Tool::Move) {
                    // A shaft and an arrow head (a triangle on the screen, from 0.8 to the end).
                    const auto base = s.ToScreen(f.origin + f.axes[a] * (f.size * gizmo::kHeadStart));
                    if (!base) continue;
                    p.Line(o->x, o->y, base->x, base->y, col, 2.f);
                    const float dxh = tip->x - base->x, dyh = tip->y - base->y;
                    const float len = std::max(std::hypot(dxh, dyh), 1e-3f);
                    const Vec2 across{-dyh / len * 5.5f, dxh / len * 5.5f};
                    ui::Path head;
                    head.Begin(*tip);
                    head.Line({base->x + across.x, base->y + across.y});
                    head.Line({base->x - across.x, base->y - across.y});
                    p.FillPath(head, col);
                } else {
                    p.Line(o->x, o->y, tip->x, tip->y, col, 2.f);
                    square(*tip, 4.5f, col);
                }
            }
            if (tool == gizmo::Tool::Scale) square(*o, 6.f, active == gizmo::Handle::Uniform ? theme::GizmoHot() : theme::Text);
        }
        p.PopClip();
    }

    // 移動 / 回転 / 拡大縮小 and ローカル / ワールド, floating at the top left of the view.
    void DrawBar(ui::Context& c, const Rect& bar) {
        const std::wstring move = Tr(L"移動"), rotate = Tr(L"回転"), scale = Tr(L"拡大縮小");
        const std::wstring moveTip = Tr(L"矢印で動かす (W)"), rotateTip = Tr(L"輪で回す (E)"), scaleTip = Tr(L"箱で大きさを変える (R)");
        const std::array<std::wstring_view, 3> tools{move, rotate, scale};
        const std::array<std::wstring_view, 3> toolTips{moveTip, rotateTip, scaleTip};
        if (const int i = w::Segmented(c, "gizmo_tool", {bar.x, bar.y, kToolW, bar.h}, tools, static_cast<int>(state_.tool), toolTips); i >= 0) {
            state_.tool = static_cast<gizmo::Tool>(i);
        }
        const std::wstring local = Tr(L"ローカル"), world = Tr(L"ワールド");
        const std::wstring localTip = Tr(L"選んだ物の向きに合わせる (X で切り替え)"), worldTip = Tr(L"世界の向き (X で切り替え)。大きさは常に物の向き");
        const std::array<std::wstring_view, 2> spaces{local, world};
        const std::array<std::wstring_view, 2> spaceTips{localTip, worldTip};
        if (const int i = w::Segmented(c, "gizmo_space", {bar.x + kToolW + kBarGap, bar.y, kSpaceW, bar.h}, spaces,
                                       static_cast<int>(state_.space), spaceTips);
            i >= 0) {
            state_.space = static_cast<gizmo::Space>(i);
        }
    }

    void DrawHints(ui::Context& c, const Rect& r) {
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Bottom;
        c.P().Text(r.Inset(10, 8, 10, 8),
                   Tr(L"右ドラッグ: 見回す (W A S D / Q E で移動)   中ドラッグ: 平行移動   ホイール: 近づく   Alt+左ドラッグ: 回る   F: 選んだ物へ   クリック: 選ぶ   Ctrl+ギズモ: 刻んで動かす   Esc: ドラッグをやめる"),
                   ts);
        if (placed_.empty()) {
            ui::TextStyle hint = ts;
            hint.size = 12;
            hint.v = ui::VAlign::Top;
            hint.color = theme::TextFaint;
            c.P().Text(r.Inset(10, BarRect(r) ? 8 + kBarH + 10 : 8, 10, 8), Tr(L"Transform の付いたエンティティが、ここに箱で出ます。"), hint);
        }
    }

    enum class Drag { None, Look, Pan, Orbit, Click, Gizmo, Cancelled };

    SceneViewState state_;       // the camera and the gizmo chosen (kept in UserSettings)
    reflect::Unknowns unknowns_;  // keys of a newer version: written back unchanged
    space3d::Renderer renderer_;
    bool tried_ = false;
    std::string transformType_;
    std::vector<Placed> placed_;
    std::vector<space3d::LineVertex> lines_;
    std::vector<space3d::Box> boxes_;

    Drag drag_ = Drag::None;
    float pressX_ = 0, pressY_ = 0;
    double lastTime_ = 0;

    gizmo::Handle hot_ = gizmo::Handle::None;  // under the mouse
    std::optional<gizmo::Drag> gizmo_;          // the gizmo drag going on
    gizmo::Tool dragTool_ = gizmo::Tool::Move;  // as they were when it began (the keys may change them)
    gizmo::Space dragSpace_ = gizmo::Space::Local;
    std::vector<Edited> edited_;

    // The picture as a Direct2D bitmap, made again when the picture or the window's context changes.
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    ID3D11Texture2D* bitmapTexture_ = nullptr;
    ID2D1DeviceContext* bitmapDc_ = nullptr;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateSceneView3D() { return std::make_unique<SceneView3D>(); }

}  // namespace rm::game_x_3d
