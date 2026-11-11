#include "rm_engine/game_x_2d/editor/SceneView2D.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_x_2d/editor/SceneViewState2D.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/editor/Gizmo2D.h"
#include "rm_engine/space2d/runtime/Renderer2D.h"
#include "rm_engine/space2d/runtime/View2D.h"
#include "ui/Path.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>

namespace rm::game_x_2d {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
namespace gizmo = space2d::gizmo2d;
using editor::Tr;
using space2d::Mat3;
using space2d::Transform2D;
using space2d::operator+;
using space2d::operator-;
using space2d::operator*;
using ui::Rect;

constexpr float kClickSlop = 4.f;     // a press that moves less than this is a click
constexpr float kMarkRadius = 7.f;    // the ring of an entity without a sprite (DIPs)
constexpr float kBarH = 28.f;         // the tool strip at the top left
constexpr float kToolW = 196.f, kSpaceW = 168.f, kGridW = 56.f, kBarGap = 8.f;
constexpr float kSnapDegrees = 15.f, kSnapScale = 0.1f;
constexpr double kImageCheckSeconds = 1.0;    // how often an image file is looked at again (changed on disk?)
constexpr double kImageForgetSeconds = 10.0;  // an image no entity uses for this long is let go

struct BarLayout {
    Rect tool, space, grid, all;
};

// An entity with a Transform 2D, as it is this frame.
struct Placed {
    int id = 0;
    Mat3 world, parentWorld;
    int order = 0;        // its world order (parents' added)
    size_t tree = 0;      // where it is in the 階層 (later = in front)
    bool active = true;
    bool sprite = false;  // has a sprite (else drawn as a small ring, not in the game)
    space2d::SpriteDraw draw;
    int subOrder = 0;     // the sprite's 並び順
};

// An image file read once by the asset's id (several entities with the same image share it). Looked at
// again now and then: a file changed on disk is read again; a file that cannot be read is told once.
struct ImageEntry {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    bool looked = false, loaded = false, told = false;
    std::filesystem::file_time_type time{};
    double checkedAt = 0, usedAt = 0;
};

// One thing being dragged: as it was when the drag began.
struct Edited {
    Transform2D start;
    Mat3 parentWorld, startWorld;
    std::function<reflect::EditTarget()> resolve;
};

space2d::ScreenRect ToScreenRect(const Rect& r) { return {r.x, r.y, r.w, r.h}; }

std::optional<size_t> FieldIndex(std::string_view name) {
    const auto fields = reflect::TypeOf<Transform2D>().fields;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == name) return i;
    }
    return std::nullopt;
}

class SceneView2D final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 8 || r.h < 8) return;
        transformType_ = reflect::TypeOf<Transform2D>().name;
        spriteType_ = reflect::TypeOf<Sprite>().name;
        now_ = c.Time();
        Place(c, host);
        ForgetUnusedImages();
        c.P().PushClip(r);
        if (host.game) {  // the game view: only the picture
            Render(c, r, host);
            c.P().PopClip();
            return;
        }
        const std::optional<BarLayout> bar = Bar(r);
        if (HandleInput(c, r, host, bar)) Place(c, host);  // a drag moved things this frame
        Render(c, r, host);
        DrawGizmo(c, r, host);
        DrawHints(c, r, bar);
        c.P().PopClip();
        if (bar) DrawBar(c, *bar);
    }

    void LoadState(const json::Value& s) override {
        state_ = {};
        unknowns_ = {};
        reflect::Warnings warnings;
        if (s.IsObject()) reflect::Load(state_, s, &unknowns_, &warnings);
        for (const auto& w : warnings) log::Warn("2D のシーンビューの見え方: {}", w);
    }
    json::Value SaveState() const override { return reflect::Save(state_, &unknowns_); }

private:
    // --- Where everything is ---------------------------------------------------------------------
    void Place(ui::Context& c, game_production::SceneViewHost& host) {
        placed_.clear();
        struct Parent {
            Mat3 world;
            int order = 0;
        };
        std::unordered_map<int, Parent> parents;  // parents come before their children in TreeOrder
        size_t index = 0;
        for (const auto& t : host.scene.TreeOrder()) {
            const auto& e = *t.entity;
            const auto it = parents.find(e.data.parent);
            const Parent parent = it != parents.end() ? it->second : Parent{};
            const game_production::Component* comp = e.FindComponent(transformType_);
            if (!comp || !comp->Known()) {
                parents[e.data.id] = parent;  // no place of its own: its children follow the parent
                ++index;
                continue;
            }
            const auto& tr = *static_cast<const Transform2D*>(comp->value.Get());
            Placed p;
            p.id = e.data.id;
            p.parentWorld = parent.world;
            p.world = space2d::WorldMatrix(parent.world, tr);
            p.order = parent.order + tr.order;
            p.tree = index++;
            p.active = t.activeInHierarchy;
            parents[e.data.id] = {p.world, p.order};
            if (const game_production::Component* sc = e.FindComponent(spriteType_); sc && sc->Known()) {
                const auto& sp = *static_cast<const Sprite*>(sc->value.Get());
                p.sprite = true;
                p.subOrder = sp.order;
                p.draw.world = p.world;
                p.draw.color = sp.color;
                p.draw.flipX = sp.flipX;
                p.draw.flipY = sp.flipY;
                p.draw.crisp = sp.crisp;
                p.draw.image = Image(c, sp.image, host);
                if (p.draw.image) {
                    const D2D1_SIZE_U px = p.draw.image->GetPixelSize();
                    const float ppu = std::max(sp.pixelsPerUnit, 0.01f);
                    p.draw.size = {static_cast<float>(px.width) / ppu, static_cast<float>(px.height) / ppu};
                }
            }
            placed_.push_back(p);
        }
    }

    // The image `ref` names (wherever it is now), read the first time it is asked for; null: a square.
    ID2D1Bitmap* Image(ui::Context& c, const AssetRef& ref, game_production::SceneViewHost& host) {
        if (ref.Empty()) return nullptr;
        ImageEntry& m = images_[ref.guid];
        m.usedAt = now_;
        if (m.looked && now_ - m.checkedAt < kImageCheckSeconds) return m.bitmap.Get();
        m.looked = true;
        m.checkedAt = now_;
        const std::filesystem::path file = host.host.AssetFile(ref);
        const std::string name = file.empty() ? ref.guid : PathToUtf8(file.filename());
        std::error_code ec;
        const auto time = file.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(file, ec);
        if (file.empty() || ec) {
            m.bitmap.Reset();
            m.loaded = false;
            if (!m.told) {
                log::Warn("2D のシーンビュー: 画像「{}」がプロジェクトに見つからないので四角で描きます (消されたか、アセットの外のファイル)", name);
                m.told = true;
            }
            return nullptr;
        }
        m.told = false;
        if (m.loaded && time == m.time) return m.bitmap.Get();  // not changed
        const bool again = m.bitmap != nullptr;
        m.loaded = true;
        m.time = time;
        std::string error;
        m.bitmap = space2d::LoadImageFile(host.host.Graphics(), c.P().DC(), file, &error);
        if (m.bitmap) {
            const D2D1_SIZE_U px = m.bitmap->GetPixelSize();
            log::Info("2D のシーンビュー: 画像「{}」を{}読みました ({} x {} ピクセル)", name, again ? "もう一度" : "", px.width, px.height);
        } else {
            log::Warn("2D のシーンビュー: 画像「{}」を読めないので四角で描きます: {}", name, error);
        }
        return m.bitmap.Get();
    }

    void ForgetUnusedImages() {
        std::erase_if(images_, [&](const auto& kv) { return now_ - kv.second.usedAt > kImageForgetSeconds; });
    }

    const Placed* FindPlaced(int id) const {
        for (const Placed& p : placed_) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    // The gizmo sits on the last selected entity that has a place (the one clicked last).
    const Placed* Primary(std::span<const int> selection) const {
        for (auto it = selection.rbegin(); it != selection.rend(); ++it) {
            if (const Placed* p = FindPlaced(*it)) return p;
        }
        return nullptr;
    }

    // What a click hits: a sprite's rectangle, or the ring of a thing without one.
    bool Hits(const Placed& p, const Rect& r, Vec2 mouse) const {
        const space2d::ScreenRect sr = ToScreenRect(r);
        if (p.sprite) {
            const Vec2 half = p.draw.size * 0.5f;
            return space2d::InsidePlacedBox(space2d::ToWorld(state_.camera, sr, mouse), p.world, {-half.x, -half.y}, half);
        }
        const Vec2 at = space2d::ToScreen(state_.camera, sr, space2d::TransformPoint(p.world, {0, 0}));
        return space2d::Length(mouse - at) <= kMarkRadius + 3.f;
    }

    // The front one under the mouse (0: none).
    int PickAt(const Rect& r, Vec2 mouse) const {
        const auto order = DrawOrder();
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const Placed& p = placed_[*it];
            if (Hits(p, r, mouse)) return p.id;
        }
        return 0;
    }

    // Back to front.
    std::vector<size_t> DrawOrder() const {
        std::vector<space2d::DrawKey> keys;
        keys.reserve(placed_.size());
        for (const Placed& p : placed_) keys.push_back({p.order, p.subOrder, p.tree});
        return space2d::DrawOrder(keys);
    }

    // --- The tool strip --------------------------------------------------------------------------
    static std::optional<BarLayout> Bar(const Rect& r) {
        if (r.w < kToolW + 16 || r.h < kBarH + 60) return std::nullopt;
        BarLayout b;
        float x = r.x + 8, y = r.y + 8;
        auto place = [&](float wd) {
            if (x > r.x + 8 && x + wd > r.Right() - 8) {
                x = r.x + 8;
                y += kBarH + 6;
            }
            const Rect at{x, y, wd, kBarH};
            x += wd + kBarGap;
            return at;
        };
        b.tool = place(kToolW);
        b.space = place(kSpaceW);
        b.grid = place(kGridW);
        const float right = std::max({b.tool.Right(), b.space.Right(), b.grid.Right()});
        b.all = {r.x + 8, r.y + 8, right - (r.x + 8), b.grid.Bottom() - (r.y + 8)};
        return b;
    }

    static bool OverBar(const std::optional<BarLayout>& bar, float x, float y) {
        if (!bar) return false;
        for (const Rect& part : {bar->tool, bar->space, bar->grid}) {
            if (part.Contains(x, y)) return true;
        }
        return false;
    }

    void DrawBar(ui::Context& c, const BarLayout& bar) {
        const std::wstring move = Tr(L"移動"), rotate = Tr(L"回転"), scale = Tr(L"拡大縮小");
        const std::wstring moveTip = Tr(L"矢印で動かす (W)"), rotateTip = Tr(L"輪で回す (E)"), scaleTip = Tr(L"箱で大きさを変える (R)");
        const std::array<std::wstring_view, 3> tools{move, rotate, scale};
        const std::array<std::wstring_view, 3> toolTips{moveTip, rotateTip, scaleTip};
        if (const int i = w::Segmented(c, "gizmo2d_tool", bar.tool, tools, static_cast<int>(state_.tool), toolTips); i >= 0) {
            state_.tool = static_cast<gizmo::Tool>(i);
        }
        const std::wstring local = Tr(L"ローカル"), world = Tr(L"ワールド");
        const std::wstring localTip = Tr(L"選んだ物の向きに合わせる (X で切り替え)"), worldTip = Tr(L"世界の向き (X で切り替え)。大きさは常に物の向き");
        const std::array<std::wstring_view, 2> spaces{local, world};
        const std::array<std::wstring_view, 2> spaceTips{localTip, worldTip};
        if (const int i = w::Segmented(c, "gizmo2d_space", bar.space, spaces, static_cast<int>(state_.space), spaceTips); i >= 0) {
            state_.space = static_cast<gizmo::Space>(i);
        }
        if (w::Button(c, "scene2d_grid", bar.grid, Tr(L"格子"), state_.grid ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.grid = !state_.grid;
        }
        if (c.IsHovered(bar.grid)) w::Tooltip(c, Tr(L"格子を出す・消す (Ctrl でドラッグすると格子の目に合わせて動く)"));
    }

    // --- Input ---------------------------------------------------------------------------------
    // Returns true when a drag changed the scene this frame.
    bool HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const std::optional<BarLayout>& bar) {
        const ui::Input& in = c.In();
        const bool hovered = c.IsHovered(r) && !OverBar(bar, in.mouseX, in.mouseY);
        const Vec2 mouse{in.mouseX, in.mouseY};
        const space2d::ScreenRect sr = ToScreenRect(r);
        bool changed = false;

        // In Play the scene is the copy that plays: shown, not edited (no gizmo, no dragging things).
        const Placed* primary = host.playing ? nullptr : Primary(host.selection);
        const std::optional<gizmo::Frame> frame =
            primary ? std::optional<gizmo::Frame>(gizmo::MakeFrame(primary->world, state_.tool, state_.space)) : std::nullopt;
        if (drag_ == Drag::None) {
            hot_ = hovered && frame ? gizmo::HitTest(state_.tool, *frame, state_.camera, sr, mouse) : gizmo::Handle::None;
        }

        if (drag_ == Drag::None && hovered) {
            if (c.MousePressed(ui::Mouse::Right)) {
                drag_ = Drag::PanRight;
            } else if (c.MousePressed(ui::Mouse::Middle)) {
                drag_ = Drag::PanMiddle;
            } else if (c.MousePressed() && hot_ != gizmo::Handle::None && frame &&
                       BeginEdit(host, host.scene.Roots(host.selection))) {
                gizmo_ = gizmo::BeginDrag(state_.tool, hot_, *frame, state_.camera, sr, mouse);
                drag_ = Drag::Gizmo;
            } else if (c.MousePressed()) {
                drag_ = Drag::Press;
                press_ = mouse;
                pressed_ = host.playing ? 0 : PickAt(r, mouse);
            }
        } else if (drag_ != Drag::None) {
            const bool held = drag_ == Drag::PanRight ? in.down[1] : drag_ == Drag::PanMiddle ? in.down[2] : in.down[0];
            if (!held) {
                if (drag_ == Drag::Press) Click(host, r, in.ctrl);
                if (drag_ == Drag::Gizmo || drag_ == Drag::Body) EndEdit(host);
                drag_ = Drag::None;
            } else if (drag_ == Drag::PanRight || drag_ == Drag::PanMiddle) {
                space2d::Pan(state_.camera, in.mouseDX, in.mouseDY);
            } else if (drag_ == Drag::Press) {
                // Far enough: a drag that started on a thing moves it (with the rest of the selection,
                // when it is selected; alone - and selected now - when it is not).
                if (pressed_ != 0 && space2d::Length(mouse - press_) >= kClickSlop) {
                    const bool inSelection = std::find(host.selection.begin(), host.selection.end(), pressed_) != host.selection.end();
                    std::vector<int> ids = inSelection ? host.scene.Roots(host.selection) : std::vector<int>{pressed_};
                    if (!inSelection) host.select({pressed_});
                    if (BeginEdit(host, ids)) {
                        gizmo::Frame f;
                        f.origin = space2d::TransformPoint(FindPlaced(pressed_)->world, {0, 0});
                        gizmo_ = gizmo::BeginDrag(gizmo::Tool::Move, gizmo::Handle::Free, f, state_.camera, sr, press_);
                        drag_ = Drag::Body;
                    } else {
                        drag_ = Drag::Cancelled;
                    }
                }
            }
            if ((drag_ == Drag::Gizmo || drag_ == Drag::Body) && gizmo_) {
                if (c.KeyPressed(ui::Key::Escape)) {
                    CancelEdit();  // everything back where it was; the button still has to be let go
                    drag_ = Drag::Cancelled;
                } else {
                    gizmo::Snap snap;
                    snap.on = in.ctrl;
                    snap.move = space2d::GridFor(state_.camera.zoom).step;
                    snap.degrees = kSnapDegrees;
                    snap.scale = kSnapScale;
                    ApplyEdit(gizmo::Update(*gizmo_, state_.camera, sr, mouse, snap));
                }
                changed = true;
            }
        }
        if (hovered && in.wheel != 0.f) space2d::ZoomAt(state_.camera, sr, mouse, in.wheel);
        if (hovered && c.Focused() == 0 && drag_ != Drag::Gizmo && drag_ != Drag::Body) {
            if (c.KeyPressed(ui::Key::F)) FrameSelection(host, r);
            if (c.KeyPressed(ui::Key::W)) state_.tool = gizmo::Tool::Move;
            if (c.KeyPressed(ui::Key::E)) state_.tool = gizmo::Tool::Rotate;
            if (c.KeyPressed(ui::Key::R)) state_.tool = gizmo::Tool::Scale;
            if (c.KeyPressed(ui::Key::X)) state_.space = state_.space == gizmo::Space::Local ? gizmo::Space::World : gizmo::Space::Local;
        }
        return changed;
    }

    // A click (the button let go where it was pressed): select what is under it; Ctrl adds / takes away.
    void Click(game_production::SceneViewHost& host, const Rect& r, bool ctrl) {
        const int hit = PickAt(r, press_);
        if (!ctrl) {
            host.select(hit ? std::vector<int>{hit} : std::vector<int>{});
            return;
        }
        if (!hit) return;
        std::vector<int> sel(host.selection.begin(), host.selection.end());
        if (const auto it = std::find(sel.begin(), sel.end(), hit); it != sel.end()) {
            sel.erase(it);
        } else {
            sel.push_back(hit);
        }
        host.select(std::move(sel));
    }

    void FrameSelection(game_production::SceneViewHost& host, const Rect& r) {
        const Placed* p = Primary(host.selection);
        if (!p) return;
        const Vec2 half = p->sprite ? p->draw.size * 0.5f : Vec2{0.5f, 0.5f};
        Vec2 corners[4];
        space2d::PlacedBoxCorners(p->world, {-half.x, -half.y}, half, corners);
        Vec2 mn = corners[0], mx = corners[0];
        for (const Vec2& k : corners) {
            mn = {std::min(mn.x, k.x), std::min(mn.y, k.y)};
            mx = {std::max(mx.x, k.x), std::max(mx.y, k.y)};
        }
        space2d::Frame(state_.camera, ToScreenRect(r), (mn + mx) * 0.5f, (mx - mn) * 0.5f);
    }

    // --- Editing by dragging -------------------------------------------------------------------
    bool BeginEdit(game_production::SceneViewHost& host, const std::vector<int>& ids) {
        if (!host.component) return false;
        edited_.clear();
        for (const int id : ids) {
            const Placed* p = FindPlaced(id);
            if (!p) continue;
            auto resolve = host.component(id, transformType_);
            const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
            if (!t.object) continue;
            edited_.push_back({*static_cast<const Transform2D*>(t.object), p->parentWorld, p->world, std::move(resolve)});
        }
        return !edited_.empty();
    }

    // While dragging, the Transforms are changed directly (the inspector follows); the history gets
    // one entry when the button is let go.
    void ApplyEdit(const gizmo::Change& change) {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (t.object) *static_cast<Transform2D*>(t.object) = gizmo::Apply(e.start, e.parentWorld, e.startWorld, change);
        }
    }

    void CancelEdit() {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (t.object) *static_cast<Transform2D*>(t.object) = e.start;
        }
        edited_.clear();
        gizmo_.reset();
    }

    // Edit = undo entry = unsaved (rule 7): one entry for the whole drag, whatever was changed.
    void EndEdit(game_production::SceneViewHost& host) {
        const gizmo::Tool tool = gizmo_ ? gizmo_->tool : gizmo::Tool::Move;
        const char* label = tool == gizmo::Tool::Move ? "移動" : tool == gizmo::Tool::Rotate ? "回転" : "拡大縮小";
        auto group = std::make_unique<CommandGroup>(label);
        const reflect::TypeDesc& type = reflect::TypeOf<Transform2D>();
        for (const char* field : {"position", "rotation", "scale"}) {
            const auto index = FieldIndex(field);
            if (!index) continue;
            const reflect::FieldDesc& fd = type.fields[*index];
            for (const Edited& e : edited_) {
                const reflect::EditTarget t = e.resolve();
                if (!t.object) continue;
                json::Value before = reflect::SaveField(fd, &e.start, nullptr);
                json::Value after = reflect::SaveField(fd, t.object, nullptr);
                if (before == after) continue;
                group->Add(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{*index}, std::move(before), std::move(after),
                                                                      e.resolve, label));
            }
        }
        if (!group->Empty()) host.host.History().Push(std::move(group));  // already applied while dragging
        edited_.clear();
        gizmo_.reset();
    }

    // --- Drawing -------------------------------------------------------------------------------
    void Render(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) {
        const bool game = host.game;
        const space2d::ScreenRect sr = ToScreenRect(r);
        lines_.clear();
        overlay_.clear();
        sprites_.clear();
        if (!game && state_.grid) Grid(sr);
        if (!game) {
            // The axes through the origin: X red, Y green (as the gizmo's arrows).
            const Vec2 a = space2d::ToWorld(state_.camera, sr, {r.x, r.Bottom()}), b = space2d::ToWorld(state_.camera, sr, {r.Right(), r.y});
            lines_.push_back({{a.x, 0}, {b.x, 0}, theme::AxisX().Alpha(0.8f), 1.f});
            lines_.push_back({{0, a.y}, {0, b.y}, theme::AxisY().Alpha(0.8f), 1.f});
        }
        for (const size_t i : DrawOrder()) {
            const Placed& p = placed_[i];
            if (!p.sprite || (game && !p.active)) continue;  // switched off: not in the game
            space2d::SpriteDraw d = p.draw;
            if (!p.active) d.color = d.color.Alpha(d.color.a * 0.35f);  // switched off: faint in the scene view
            sprites_.push_back(d);
        }
        if (!game) {
            for (const Placed& p : placed_) {
                const bool selected = std::find(host.selection.begin(), host.selection.end(), p.id) != host.selection.end();
                if (p.sprite) {
                    if (selected) Outline(p, theme::Accent);
                } else {
                    Mark(p, sr, selected ? theme::Accent : p.active ? theme::SceneObject() : theme::SceneObjectInactive());
                }
            }
        }
        space2d::RenderFrame2D f;
        f.viewport = sr;
        f.camera = state_.camera;
        f.clear = game ? host.background : theme::SceneBackground();
        f.lines = lines_;
        f.sprites = sprites_;
        f.overlay = overlay_;
        renderer_.Render(c.P().DC(), f);
    }

    // The grid over the part of the plane that is seen: a step that keeps lines apart, every 5th / 10th darker.
    void Grid(const space2d::ScreenRect& sr) {
        const space2d::GridStep g = space2d::GridFor(state_.camera.zoom);
        const Vec2 mn = space2d::ToWorld(state_.camera, sr, {sr.x, sr.y + sr.h});
        const Vec2 mx = space2d::ToWorld(state_.camera, sr, {sr.x + sr.w, sr.y});
        const long long x0 = static_cast<long long>(std::floor(mn.x / g.step)), x1 = static_cast<long long>(std::ceil(mx.x / g.step));
        const long long y0 = static_cast<long long>(std::floor(mn.y / g.step)), y1 = static_cast<long long>(std::ceil(mx.y / g.step));
        if (x1 - x0 > 2000 || y1 - y0 > 2000) return;  // (never with GridFor's step; a guard)
        for (long long i = x0; i <= x1; ++i) {
            const float x = static_cast<float>(i) * g.step;
            lines_.push_back({{x, mn.y}, {x, mx.y}, i % g.major == 0 ? theme::SceneGridMajor() : theme::SceneGrid(), 1.f});
        }
        for (long long i = y0; i <= y1; ++i) {
            const float y = static_cast<float>(i) * g.step;
            lines_.push_back({{mn.x, y}, {mx.x, y}, i % g.major == 0 ? theme::SceneGridMajor() : theme::SceneGrid(), 1.f});
        }
    }

    void Outline(const Placed& p, ui::Color col) {
        const Vec2 half = p.draw.size * 0.5f;
        Vec2 k[4];
        space2d::PlacedBoxCorners(p.world, {-half.x, -half.y}, half, k);
        for (int i = 0; i < 4; ++i) overlay_.push_back({k[i], k[(i + 1) % 4], col, 1.5f});
    }

    // A thing without a sprite: a small ring (the same size on the screen) with a tick along its X axis.
    void Mark(const Placed& p, const space2d::ScreenRect& sr, ui::Color col) {
        (void)sr;
        const float rad = kMarkRadius / std::max(state_.camera.zoom, 1e-6f);
        const Vec2 o = space2d::TransformPoint(p.world, {0, 0});
        constexpr int kSegments = 20;
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = 6.2831853f * static_cast<float>(i) / kSegments, a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
            overlay_.push_back({o + Vec2{std::cos(a0), std::sin(a0)} * rad, o + Vec2{std::cos(a1), std::sin(a1)} * rad, col, 1.5f});
        }
        const Vec2 x = space2d::Normalize(space2d::TransformDirection(p.world, {1, 0}));
        overlay_.push_back({o + x * rad, o + x * (rad * 1.8f), col, 1.5f});
    }

    // The gizmo over the picture (drawn on the screen from gizmo2d's shapes: what is drawn is what is grabbed).
    void DrawGizmo(ui::Context& c, const Rect& r, const game_production::SceneViewHost& host) {
        if (host.playing || drag_ == Drag::Body) return;  // Play: not edited / dragging the thing itself
        const Placed* primary = Primary(host.selection);
        if (!primary) return;
        const gizmo::Tool tool = drag_ == Drag::Gizmo && gizmo_ ? gizmo_->tool : state_.tool;
        const gizmo::Frame f = drag_ == Drag::Gizmo && gizmo_ ? gizmo_->frame : gizmo::MakeFrame(primary->world, tool, state_.space);
        // While dragging, the gizmo follows the thing (its frame as it is now).
        const gizmo::Frame shown = drag_ == Drag::Gizmo ? gizmo::MakeFrame(primary->world, tool, state_.space) : f;
        const gizmo::ScreenShape s = gizmo::ShapeOf(shown, state_.camera, ToScreenRect(r));
        const gizmo::Handle active = drag_ == Drag::Gizmo && gizmo_ ? gizmo_->handle : hot_;
        auto& p = c.P();
        auto colorOf = [&](gizmo::Handle h, ui::Color base) { return h == active ? theme::GizmoHot() : base; };
        auto square = [&](Vec2 m, float half, ui::Color col) {
            ui::Path sq;
            sq.Begin({m.x - half, m.y - half});
            sq.Line({m.x + half, m.y - half});
            sq.Line({m.x + half, m.y + half});
            sq.Line({m.x - half, m.y + half});
            p.FillPath(sq, col);
        };
        if (tool == gizmo::Tool::Rotate) {
            const ui::Color col = colorOf(gizmo::Handle::Ring, theme::AxisZ());
            constexpr int kSegments = 64;
            for (int i = 0; i < kSegments; ++i) {
                const float a0 = 6.2831853f * static_cast<float>(i) / kSegments, a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
                p.Line(s.origin.x + std::cos(a0) * gizmo::kRingRadius, s.origin.y + std::sin(a0) * gizmo::kRingRadius,
                       s.origin.x + std::cos(a1) * gizmo::kRingRadius, s.origin.y + std::sin(a1) * gizmo::kRingRadius, col,
                       active == gizmo::Handle::Ring ? 3.f : 2.f);
            }
            // A spoke along the thing's X: shows how far it has turned.
            p.Line(s.origin.x, s.origin.y, s.tips[0].x, s.tips[0].y, theme::AxisX().Alpha(0.7f), 1.5f);
            p.FillCircle(s.origin.x, s.origin.y, 3.f, theme::Text);
            return;
        }
        const ui::Color axisCol[2] = {theme::AxisX(), theme::AxisY()};
        for (int a = 0; a < 2; ++a) {
            const gizmo::Handle h = a == 0 ? gizmo::Handle::X : gizmo::Handle::Y;
            const ui::Color col = colorOf(h, axisCol[a]);
            const Vec2 tip = s.tips[static_cast<size_t>(a)];
            const Vec2 dir = space2d::Normalize(tip - s.origin);
            if (tool == gizmo::Tool::Move) {
                const Vec2 base = tip - dir * 14.f;
                p.Line(s.origin.x, s.origin.y, base.x, base.y, col, 2.f);
                const Vec2 across{-dir.y * 6.f, dir.x * 6.f};
                ui::Path head;
                head.Begin(tip);
                head.Line(base + across);
                head.Line(base - across);
                p.FillPath(head, col);
            } else {
                p.Line(s.origin.x, s.origin.y, tip.x, tip.y, col, 2.f);
                square(tip, 5.f, col);
            }
        }
        square(s.origin, gizmo::kCenterHalf - 2.f, colorOf(gizmo::Handle::Free, theme::Text.Alpha(0.85f)));
    }

    void DrawHints(ui::Context& c, const Rect& r, const std::optional<BarLayout>& bar) {
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Bottom;
        ts.wrap = true;
        c.P().Text(r.Inset(10, 8, 10, 8),
                   Tr(L"右 / 中ドラッグ: 平行移動   ホイール: 近づく   クリック: 選ぶ (Ctrl: 足す・外す)   物をドラッグ: 動かす   F: 選んだ物へ   W / E / R: 移動・回転・拡大縮小   X: ローカル / ワールド   Ctrl+ドラッグ: 刻んで動かす   Esc: ドラッグをやめる"),
                   ts);
        if (placed_.empty()) {
            ui::TextStyle hint = ts;
            hint.size = 12;
            hint.v = ui::VAlign::Top;
            hint.color = theme::TextFaint;
            c.P().Text(r.Inset(10, bar ? bar->all.Bottom() - r.y + 10 : 8, 10, 8),
                       Tr(L"Transform 2D の付いたエンティティが、ここに小さな輪で出ます。スプライトの部品を付けると、その絵 (画像が無いときは白い四角) で描きます。"),
                       hint);
        }
    }

    enum class Drag { None, PanRight, PanMiddle, Press, Body, Gizmo, Cancelled };

    SceneViewState2D state_;      // the camera, the tool, the grid (kept in UserSettings)
    reflect::Unknowns unknowns_;  // keys of a newer version: written back unchanged
    space2d::Renderer2D renderer_;
    std::string transformType_, spriteType_;
    std::vector<Placed> placed_;
    std::vector<space2d::Line2D> lines_, overlay_;
    std::vector<space2d::SpriteDraw> sprites_;
    std::unordered_map<std::string, ImageEntry> images_;  // by Sprite::image (the asset's id)
    double now_ = 0;

    Drag drag_ = Drag::None;
    Vec2 press_{0, 0};
    int pressed_ = 0;  // the entity under the mouse when the left button went down (0: none)
    gizmo::Handle hot_ = gizmo::Handle::None;
    std::optional<gizmo::Drag> gizmo_;
    std::vector<Edited> edited_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateSceneView2D() { return std::make_unique<SceneView2D>(); }

}  // namespace rm::game_x_2d
