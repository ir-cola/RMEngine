#include "rm_engine/game_x_3d/editor/SceneView3D.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_x_3d/editor/SceneViewState.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/space3d/editor/Gizmo.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/View.h"
#include "ui/Path.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <memory>
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
constexpr float kToolW = 196.f, kSpaceW = 168.f, kPivotW = 150.f, kSnapW = 64.f, kShadowW = 56.f, kBarGap = 8.f;
constexpr float kSnapBoxW = 260.f, kSnapRowH = 30.f;  // the box of the steps (under 刻み)
constexpr double kModelCheckSeconds = 1.0;    // how often a model file is looked at again (changed on disk?)
constexpr double kModelForgetSeconds = 10.0;  // a model no entity uses for this long leaves the GPU

// The tool strip's parts: 移動 / 回転 / 拡大縮小, ローカル / ワールド, それぞれ / 中心, 刻み, 影; they go on a
// second row when the view is too narrow for one. `all` = round all of them.
struct BarLayout {
    Rect tool, space, pivot, snap, shadow, all;
};

struct Placed {
    int id;
    Mat4 world;
    Mat4 parentWorld;
    bool active;
    // What is drawn: the model (null: the unit cube) and its bounds in its own space - what a click
    // hits and what F frames.
    const space3d::GpuMesh* mesh = nullptr;
    bool castShadow = true;
    Vec3 min{-0.5f, -0.5f, -0.5f}, max{0.5f, 0.5f, 0.5f};
};

// A model file read once and kept on the GPU, by the path written in the MeshRenderer (several
// entities with the same model share it). Looked at again now and then: a file changed on disk
// (exported again from a 3D tool) is read again; a file that cannot be read is told once.
struct ModelEntry {
    std::shared_ptr<const space3d::GpuMesh> gpu;
    bool looked = false;  // the file has been looked for at least once
    bool loaded = false;  // ... and read (or tried to be) as it is at `time`
    bool told = false;    // that it is missing / not inside the project was written to the console
    std::filesystem::file_time_type time{};
    double checkedAt = 0, usedAt = 0;
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
        meshType_ = reflect::TypeOf<MeshRenderer>().name;
        now_ = c.Time();
        Place(host);
        ForgetUnusedModels();
        if (host.game) {  // the game view: only the picture
            Render(c, r, host);
            return;
        }
        const std::optional<BarLayout> bar = Bar(r);
        if (!bar) snapOpen_ = false;
        if (HandleInput(c, r, host, bar)) Place(host);  // a gizmo drag moved things this frame
        Render(c, r, host);
        DrawGizmo(c, r, host);
        DrawHints(c, r, bar);
        if (bar) DrawBar(c, *bar);
        if (bar && snapOpen_) DrawSnapBox(c, SnapBox(r, *bar));
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

    // Where every entity with a Transform is in the world (its parents' Transforms included), and
    // what it looks like (its model, or a box).
    void Place(game_production::SceneViewHost& host) {
        const game_production::Scene& scene = host.scene;
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
                Placed p{e.data.id, w, parent, t.activeInHierarchy};
                p.castShadow = t.activeInHierarchy;  // a thing switched off shows faintly, without a shadow
                const game_production::Component* mc = e.FindComponent(meshType_);
                if (mc && mc->Known()) {
                    const auto& mr = *static_cast<const MeshRenderer*>(mc->value.Get());
                    p.castShadow = p.castShadow && mr.castShadows;
                    if (const space3d::GpuMesh* g = Model(mr.model, host)) {
                        p.mesh = g;
                        p.min = g->min;
                        p.max = g->max;
                    }
                }
                placed_.push_back(p);
            } else {
                world[e.data.id] = parent;  // no place of its own: its children follow the parent
            }
        }
    }

    // The model at `path` (inside the project), read the first time it is asked for; null: draw a box.
    const space3d::GpuMesh* Model(const std::string& path, game_production::SceneViewHost& host) {
        if (path.empty()) return nullptr;
        ModelEntry& m = models_[path];
        m.usedAt = now_;
        if (m.looked && now_ - m.checkedAt < kModelCheckSeconds) return m.gpu.get();
        const bool firstLook = !m.looked;
        m.looked = true;
        m.checkedAt = now_;
        const std::filesystem::path rel = PathFromUtf8(path);
        if (rel.has_root_name() || rel.has_root_directory()) {
            if (!m.told) log::Warn("シーンビュー: モデル「{}」は絶対パスなので読みません。プロジェクトのフォルダからの相対パスにしてください (箱で描きます)", path);
            m.told = true;
            return nullptr;
        }
        const std::filesystem::path root = host.host.GetProject().Root();
        const std::filesystem::path file = root / rel;
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file, ec);
        if (ec) {
            // A path still being typed in the inspector is not there either: told only when it is
            // still missing at the next look (a second later), once.
            m.gpu.reset();
            m.loaded = false;
            if (!firstLook && !m.told) {
                log::Warn("シーンビュー: モデル「{}」が見つからないので箱で描きます (プロジェクトのフォルダ「{}」からの相対パスです)", path, PathToUtf8(root));
                m.told = true;
            }
            return nullptr;
        }
        m.told = false;
        if (m.loaded && time == m.time) return m.gpu.get();  // not changed
        const bool again = m.gpu != nullptr;
        m.loaded = true;
        m.time = time;
        m.gpu.reset();
        std::string error;
        if (auto mesh = space3d::LoadMeshFile(file, &error)) {
            m.gpu = renderer_.Upload(*mesh, &error);
            if (m.gpu) {
                log::Info("シーンビュー: モデル「{}」を{}読みました (頂点 {}・三角形 {})", path, again ? "もう一度" : "", mesh->vertices.size(),
                          mesh->indices.size() / 3);
            } else {
                log::Warn("シーンビュー: モデル「{}」を GPU に載せられないので箱で描きます: {}", path, error);
            }
        } else {
            log::Warn("シーンビュー: モデル「{}」を読めないので箱で描きます: {}", path, error);
        }
        return m.gpu.get();
    }

    // Models no entity has used for a while leave the GPU (read again if they come back).
    void ForgetUnusedModels() {
        std::erase_if(models_, [&](const auto& kv) { return now_ - kv.second.usedAt > kModelForgetSeconds; });
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

    // The gizmo: on the one selected last (それぞれ), or in the middle of every selected thing (中心) -
    // the things that move, a child of a selected parent not counted - with the last one's axes.
    std::optional<gizmo::Frame> GizmoFrame(const Rect& r, const game_production::SceneViewHost& host) const {
        const Placed* p = Primary(host);
        if (!p) return std::nullopt;
        if (state_.pivot == gizmo::Pivot::Each) return gizmo::MakeFrame(p->world, state_.tool, state_.space, state_.camera, r.h);
        Vec3 sum{0, 0, 0};
        int n = 0;
        for (const int id : host.scene.Roots(host.selection)) {
            if (const Placed* q = FindPlaced(id)) {
                sum = sum + space3d::TransformPoint(q->world, {0, 0, 0});
                ++n;
            }
        }
        const Vec3 center = n > 0 ? sum * (1.f / static_cast<float>(n)) : space3d::TransformPoint(p->world, {0, 0, 0});
        return gizmo::MakeFrame(center, gizmo::AxesOf(p->world, state_.tool, state_.space), state_.camera, r.h);
    }

    Mat4 ViewProjection(const Rect& r) const { return state_.camera.ProjectionMatrix(r.w / r.h) * state_.camera.ViewMatrix(); }

    // The tool strip (top left): its parts left to right, onto a new row where the view ends; none
    // when the view is too small even for the first part.
    static std::optional<BarLayout> Bar(const Rect& r) {
        if (r.w < kToolW + 16 || r.h < kBarH + 60) return std::nullopt;
        BarLayout b;
        float x = r.x + 8, y = r.y + 8;
        auto place = [&](float w) {
            if (x > r.x + 8 && x + w > r.Right() - 8) {
                x = r.x + 8;
                y += kBarH + 6;
            }
            const Rect at{x, y, w, kBarH};
            x += w + kBarGap;
            return at;
        };
        b.tool = place(kToolW);
        b.space = place(kSpaceW);
        b.pivot = place(kPivotW);
        b.snap = place(kSnapW);
        b.shadow = place(kShadowW);
        const float right = std::max({b.tool.Right(), b.space.Right(), b.pivot.Right(), b.snap.Right(), b.shadow.Right()});
        b.all = {r.x + 8, r.y + 8, right - (r.x + 8), b.shadow.Bottom() - (r.y + 8)};
        return b;
    }

    // The box of the steps, under 刻み (kept inside the view).
    static Rect SnapBox(const Rect& r, const BarLayout& b) {
        const float h = 10 + 3 * kSnapRowH + 30 + 8;
        return {std::max(r.x + 8, std::min(b.snap.x, r.Right() - 8 - kSnapBoxW)), b.snap.Bottom() + 4, kSnapBoxW, h};
    }

    bool OverBar(const Rect& r, const std::optional<BarLayout>& bar, float x, float y) const {
        if (!bar) return false;
        for (const Rect& part : {bar->tool, bar->space, bar->pivot, bar->snap, bar->shadow}) {
            if (part.Contains(x, y)) return true;
        }
        return snapOpen_ && SnapBox(r, *bar).Contains(x, y);
    }

    // Returns true when a gizmo drag changed the scene this frame.
    bool HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const std::optional<BarLayout>& bar) {
        const ui::Input& in = c.In();
        const double now = c.Time();
        const float dt = static_cast<float>(std::clamp(now - lastTime_, 0.0, 0.1));
        lastTime_ = now;
        const bool overBar = OverBar(r, bar, in.mouseX, in.mouseY);
        bool hovered = c.IsHovered(r) && !overBar;
        // The box of the steps closes on a click outside it (the click does nothing else) or Esc.
        if (snapOpen_ && drag_ == Drag::None) {
            const bool pressed = c.MousePressed() || c.MousePressed(ui::Mouse::Right) || c.MousePressed(ui::Mouse::Middle);
            if ((pressed && !overBar) || (c.Focused() == 0 && c.KeyPressed(ui::Key::Escape))) {
                snapOpen_ = false;
                if (pressed) hovered = false;
            }
        }
        // How far the mouse moved (still counted while the cursor is held at the drag's start).
        const float dx = in.mouseDX, dy = in.mouseDY;
        const Vec2 mouse{in.mouseX, in.mouseY};
        bool changed = false;

        const std::optional<gizmo::Screen> screen = gizmo::MakeScreen(ViewProjection(r), r.x, r.y, r.w, r.h);
        // In Play the scene is the copy that plays: shown, not edited (no gizmo).
        const std::optional<gizmo::Frame> frame = host.playing ? std::nullopt : GizmoFrame(r, host);
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
                    // Ctrl: in steps - or smoothly, when it is always in steps.
                    gizmo::Snap snap;
                    snap.on = in.ctrl != state_.snapAlways;
                    snap.move = state_.snapMove;
                    snap.degrees = state_.snapDegrees;
                    snap.scale = state_.snapScale;
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
            if (c.KeyPressed(ui::Key::Z)) TogglePivot();
        }
        return changed;
    }

    void ToggleSpace() { state_.space = state_.space == gizmo::Space::Local ? gizmo::Space::World : gizmo::Space::Local; }
    void TogglePivot() { state_.pivot = state_.pivot == gizmo::Pivot::Each ? gizmo::Pivot::Center : gizmo::Pivot::Each; }

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
        // 中心: everything turns / scales about the middle where the gizmo is.
        pivot_ = state_.pivot == gizmo::Pivot::Center ? std::optional<Vec3>(frame.origin) : std::nullopt;
        return true;
    }

    // While dragging, the Transforms are changed directly (the inspector follows); the history gets
    // one entry when the button is let go.
    void ApplyGizmo(const gizmo::Change& change) {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (!t.object) continue;  // gone (undone while dragging)
            auto* tr = static_cast<Transform*>(t.object);
            *tr = gizmo::Apply(e.start, e.parentWorld, e.startWorld, change, dragSpace_, tr->rotation, pivot_);
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
            const auto t = space3d::IntersectPlacedBox(ray, p.world, p.min, p.max);
            if (t && (!best || *t < *best)) {
                best = t;
                hit = p.id;
            }
        }
        host.select(best ? std::vector<int>{hit} : std::vector<int>{});
    }

    void FrameSelection(game_production::SceneViewHost& host) {
        if (const Placed* p = Primary(host)) {
            // The middle of its bounds (a model's origin may be at its feet), as far as they reach.
            const Vec3 center = space3d::TransformPoint(p->world, (p->min + p->max) * 0.5f);
            const float size = space3d::Length(space3d::TransformDirection(p->world, p->max - p->min));
            space3d::Frame(state_.camera, center, std::max(size * 0.5f, 0.5f));
        }
    }

    void Render(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) {
        // The grid on the floor (y = 0) and the axes through the middle (not in the game view).
        lines_.clear();
        const bool game = host.game;
        auto line = [&](Vec3 a, Vec3 b, ui::Color col) {
            lines_.push_back({a, col});
            lines_.push_back({b, col});
        };
        const float h = static_cast<float>(kGridHalf);
        for (int i = -kGridHalf; i <= kGridHalf && !game; ++i) {
            const float v = static_cast<float>(i);
            const ui::Color minor = i % 10 == 0 ? theme::SceneGridMajor() : theme::SceneGrid();
            line({v, 0, -h}, {v, 0, h}, i == 0 ? theme::AxisZ() : minor);  // along Z (the line x = 0 is the Z axis)
            line({-h, 0, v}, {h, 0, v}, i == 0 ? theme::AxisX() : minor);  // along X
        }
        if (!game) line({0, 0, 0}, {0, 1.5f, 0}, theme::AxisY());

        boxes_.clear();
        meshes_.clear();
        for (const Placed& p : placed_) {
            if (game && !p.active) continue;  // switched off: not in the game
            const bool selected = !game && std::find(host.selection.begin(), host.selection.end(), p.id) != host.selection.end();
            const ui::Color col = selected ? theme::SceneObjectSelected() : p.active ? theme::SceneObject() : theme::SceneObjectInactive();
            if (p.mesh) {
                meshes_.push_back({p.mesh, p.world, col, p.castShadow});
            } else {
                boxes_.push_back({p.world, col, p.castShadow});
            }
            if (selected) Outline(p.world, p.min, p.max, theme::Accent);
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
        f.clear = game ? host.background : theme::SceneBackground();
        f.lines = lines_;
        f.boxes = boxes_;
        f.meshes = meshes_;
        f.shadows = state_.shadows;
        f.shadowColor = theme::SceneShadow();

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

    // The 12 edges of a box of the thing's own space (the unit cube, or a model's bounds), placed.
    void Outline(const Mat4& w, Vec3 mn, Vec3 mx, ui::Color col) {
        Vec3 k[8];
        for (int i = 0; i < 8; ++i) {
            k[i] = space3d::TransformPoint(w, {(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z});
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
        if (host.playing) return;  // Play: not edited
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

        auto polyline = [&](const std::vector<Vec3>& pts, ui::Color col, float width) {
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                const auto a0 = s.ToScreen(pts[i]), a1 = s.ToScreen(pts[i + 1]);
                if (a0 && a1) p.Line(a0->x, a0->y, a1->x, a1->y, col, width);
            }
        };
        const ui::Color viewColor = theme::Text.Alpha(0.85f);

        p.PushClip(r);
        if (tool == gizmo::Tool::Rotate) {
            // The ball's outline (a drag inside it turns freely: filled faintly when it would).
            const std::vector<Vec3> ball = gizmo::FacingCircle(f, state_.camera.Eye(), 1.f);
            if (active == gizmo::Handle::Free) {
                ui::Path disc;
                for (size_t i = 0; i < ball.size(); ++i) {
                    const auto q = s.ToScreen(ball[i]);
                    if (!q) continue;
                    if (disc.steps.empty()) {
                        disc.Begin(*q);
                    } else {
                        disc.Line(*q);
                    }
                }
                p.FillPath(disc, theme::GizmoHot().Alpha(0.16f));
            }
            polyline(ball, viewColor.Alpha(active == gizmo::Handle::Free ? 0.6f : 0.25f), 1.f);
            // The halves behind first and faint (each ring reads as a whole circle), then the front.
            for (const bool front : {false, true}) {
                for (int a = 0; a < 3; ++a) {
                    const bool hot = handleOf(a) == active;
                    const ui::Color col = colorOf(handleOf(a), a).Alpha(front ? 1.f : hot ? 0.6f : 0.3f);
                    const float width = front ? (hot ? 3.f : 2.f) : 1.5f;
                    for (const auto& arc : gizmo::RingArcs(f, a, state_.camera.Eye(), front)) polyline(arc, col, width);
                }
            }
            // The outer ring facing the eye: turns about the line of sight.
            const bool viewHot = active == gizmo::Handle::View;
            polyline(gizmo::FacingCircle(f, state_.camera.Eye(), gizmo::kViewRing), viewHot ? theme::GizmoHot() : viewColor,
                     viewHot ? 3.f : 2.f);
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

    // 移動 / 回転 / 拡大縮小, ローカル / ワールド, それぞれ / 中心 and 刻み, floating at the top left of the view.
    void DrawBar(ui::Context& c, const BarLayout& bar) {
        const std::wstring move = Tr(L"移動"), rotate = Tr(L"回転"), scale = Tr(L"拡大縮小");
        const std::wstring moveTip = Tr(L"矢印で動かす (W)"), rotateTip = Tr(L"輪で回す (E)"), scaleTip = Tr(L"箱で大きさを変える (R)");
        const std::array<std::wstring_view, 3> tools{move, rotate, scale};
        const std::array<std::wstring_view, 3> toolTips{moveTip, rotateTip, scaleTip};
        if (const int i = w::Segmented(c, "gizmo_tool", bar.tool, tools, static_cast<int>(state_.tool), toolTips); i >= 0) {
            state_.tool = static_cast<gizmo::Tool>(i);
        }
        const std::wstring local = Tr(L"ローカル"), world = Tr(L"ワールド");
        const std::wstring localTip = Tr(L"選んだ物の向きに合わせる (X で切り替え)"), worldTip = Tr(L"世界の向き (X で切り替え)。大きさは常に物の向き");
        const std::array<std::wstring_view, 2> spaces{local, world};
        const std::array<std::wstring_view, 2> spaceTips{localTip, worldTip};
        if (const int i = w::Segmented(c, "gizmo_space", bar.space, spaces, static_cast<int>(state_.space), spaceTips); i >= 0) {
            state_.space = static_cast<gizmo::Space>(i);
        }
        const std::wstring each = Tr(L"それぞれ"), center = Tr(L"中心");
        const std::wstring eachTip = Tr(L"選んだ物をそれぞれの場所で回す・大きさを変える。ギズモは最後に選んだ物に (Z で切り替え)");
        const std::wstring centerTip = Tr(L"選んだ物全部の真ん中にギズモを出し、そこを中心に回す・大きさを変える (Z で切り替え)");
        const std::array<std::wstring_view, 2> pivots{each, center};
        const std::array<std::wstring_view, 2> pivotTips{eachTip, centerTip};
        if (const int i = w::Segmented(c, "gizmo_pivot", bar.pivot, pivots, static_cast<int>(state_.pivot), pivotTips); i >= 0) {
            state_.pivot = static_cast<gizmo::Pivot>(i);
        }
        // 刻み: opens the box of the steps (in the accent while the gizmo always moves in steps).
        if (w::Button(c, "gizmo_snap", bar.snap, Tr(L"刻み"), state_.snapAlways ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            snapOpen_ = !snapOpen_;
        }
        if (!snapOpen_ && c.IsHovered(bar.snap)) w::Tooltip(c, Tr(L"ギズモの刻む幅 (Ctrl を押しながらドラッグすると刻んで動く)"));
        // 影: the light's shadows on the floor and on things (in the accent while on).
        if (w::Button(c, "scene_shadows", bar.shadow, Tr(L"影"), state_.shadows ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.shadows = !state_.shadows;
        }
        if (c.IsHovered(bar.shadow)) w::Tooltip(c, Tr(L"上からの光の影を、床と物に落とす (押すと切り替え)"));
    }

    // The steps of the gizmo: how far a move, a turn and a scale go at a time; and whether always.
    // View state like the tool (kept in UserSettings, not an edit of the project).
    void DrawSnapBox(ui::Context& c, const Rect& box) {
        w::DrawSurface(c, box, w::Surface::Popup);
        ui::TextStyle label;
        label.size = theme::FontSize;
        label.color = theme::TextDim;
        label.v = ui::VAlign::Center;
        struct Row {
            const char* id;
            std::wstring name;
            float* value;
            float min, max;
        };
        Row rows[] = {{"snap_move", Tr(L"移動"), &state_.snapMove, 0.001f, 1000.f},
                      {"snap_degrees", Tr(L"回転 (度)"), &state_.snapDegrees, 0.1f, 180.f},
                      {"snap_scale", Tr(L"大きさ"), &state_.snapScale, 0.001f, 10.f}};
        float y = box.y + 10;
        for (size_t i = 0; i < std::size(rows); ++i, y += kSnapRowH) {
            Row& row = rows[i];
            c.P().Text({box.x + 12, y, 120, kSnapRowH - 6}, row.name, label);
            // The text as typed while it is being typed; the value otherwise.
            std::string& text = snapText_[i];
            if (c.Focused() != c.MakeId(row.id)) text = std::format("{:g}", *row.value);
            if (w::TextField(c, row.id, {box.Right() - 12 - 110, y, 110, kSnapRowH - 6}, text, {}, true, true)) {
                float v = 0;
                const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
                if (ec == std::errc{} && end == text.data() + text.size() && v > 0) *row.value = std::clamp(v, row.min, row.max);
            }
        }
        w::Checkbox(c, "snap_always", {box.x + 12, y + 2, box.w - 24, 24}, state_.snapAlways, Tr(L"いつも刻む (Ctrl を押すとなめらか)"));
    }

    void DrawHints(ui::Context& c, const Rect& r, const std::optional<BarLayout>& bar) {
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Bottom;
        ts.wrap = true;
        c.P().Text(r.Inset(10, 8, 10, 8),
                   Tr(L"右ドラッグ: 見回す (W A S D / Q E で移動)   中ドラッグ: 平行移動   ホイール: 近づく   Alt+左ドラッグ: 回る   F: 選んだ物へ   クリック: 選ぶ   W / E / R: 移動・回転・拡大縮小   X: ローカル / ワールド   Z: それぞれ / 中心   回転の球の中をドラッグ: 自由に回す   Ctrl+ギズモ: 刻んで動かす   Esc: ドラッグをやめる"),
                   ts);
        if (placed_.empty()) {
            ui::TextStyle hint = ts;
            hint.size = 12;
            hint.v = ui::VAlign::Top;
            hint.color = theme::TextFaint;
            c.P().Text(r.Inset(10, bar ? bar->all.Bottom() - r.y + 10 : 8, 10, 8), Tr(L"Transform の付いたエンティティが、ここに箱で出ます。メッシュ (3D モデル) の部品を付けると、そのモデルで描きます。"), hint);
        }
    }

    enum class Drag { None, Look, Pan, Orbit, Click, Gizmo, Cancelled };

    SceneViewState state_;       // the camera and the gizmo chosen (kept in UserSettings)
    reflect::Unknowns unknowns_;  // keys of a newer version: written back unchanged
    space3d::Renderer renderer_;
    bool tried_ = false;
    std::string transformType_, meshType_;
    std::vector<Placed> placed_;
    std::vector<space3d::LineVertex> lines_;
    std::vector<space3d::Box> boxes_;
    std::vector<space3d::MeshDraw> meshes_;
    std::unordered_map<std::string, ModelEntry> models_;  // by the path written in MeshRenderer
    double now_ = 0;

    Drag drag_ = Drag::None;
    float pressX_ = 0, pressY_ = 0;
    double lastTime_ = 0;

    gizmo::Handle hot_ = gizmo::Handle::None;  // under the mouse
    std::optional<gizmo::Drag> gizmo_;          // the gizmo drag going on
    gizmo::Tool dragTool_ = gizmo::Tool::Move;  // as they were when it began (the keys may change them)
    gizmo::Space dragSpace_ = gizmo::Space::Local;
    std::optional<Vec3> pivot_;                 // 中心: what the drag turns / scales about
    std::vector<Edited> edited_;
    bool snapOpen_ = false;                     // the box of the steps is open
    std::array<std::string, 3> snapText_;       // its fields' text

    // The picture as a Direct2D bitmap, made again when the picture or the window's context changes.
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    ID3D11Texture2D* bitmapTexture_ = nullptr;
    ID2D1DeviceContext* bitmapDc_ = nullptr;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateSceneView3D() { return std::make_unique<SceneView3D>(); }

}  // namespace rm::game_x_3d
