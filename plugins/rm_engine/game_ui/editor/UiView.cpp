#include "rm_engine/game_ui/editor/UiView.h"

#include "base/Log.h"
#include "core/UndoHistory.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Shell.h"
#include "rm_engine/game_ui/editor/UiViewState.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"
#include "rm_engine/game_ui/runtime/UiRenderer.h"
#include "rm_engine/game_ui/runtime/UiService.h"
#include "ui/Path.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace rm::game_ui {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
using editor::Tr;
using editor::TrF;
using ui::Rect;

constexpr float kMargin = 16.f;      // around the game screen in the panel
constexpr float kBarH = 28.f;        // the strip at the top left
constexpr float kHandle = 4.5f;      // half a handle's square (DIPs)
constexpr float kHandleSlop = 8.f;   // how near the mouse must be to a handle
constexpr float kKnob = 26.f;        // the turning knob: this far above the box's top middle
constexpr float kKnobRadius = 5.f;
constexpr float kClickSlop = 4.f;    // a press that moves less than this is a click
constexpr float kMinZoom = 0.1f, kMaxZoom = 10.f;
constexpr float kSnapReach = 6.f;    // DIPs on the panel: how near a side must come to snap
constexpr double kForgetSeconds = 2.0;  // pictures and fonts read again this often (their files may change)

UiBox ToBox(const Rect& r) { return {r.x, r.y, r.w, r.h}; }

Vec2 Linear(const Affine& m, Vec2 v) { return {m.a * v.x + m.c * v.y, m.b * v.x + m.d * v.y}; }
float Distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::optional<size_t> FieldIndex(std::string_view name) {
    const auto fields = reflect::TypeOf<UiRect>().fields;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == name) return i;
    }
    return std::nullopt;
}

// The handles in UiHandle's order, as points of the box (0..1: x, y).
constexpr std::array<std::pair<float, float>, 8> kHandleAt{
    {{0.f, 0.f}, {0.5f, 0.f}, {1.f, 0.f}, {1.f, 0.5f}, {1.f, 1.f}, {0.5f, 1.f}, {0.f, 1.f}, {0.f, 0.5f}}};

// The box around corners on the panel (a turned thing's: the box that holds it).
UiBox PanelBox(const std::array<Vec2, 4>& k) {
    float x0 = k[0].x, y0 = k[0].y, x1 = k[0].x, y1 = k[0].y;
    for (const Vec2& v : k) {
        x0 = std::min(x0, v.x);
        y0 = std::min(y0, v.y);
        x1 = std::max(x1, v.x);
        y1 = std::max(y1, v.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

UiBox Union(const UiBox& a, const UiBox& b) {
    const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    return {x0, y0, std::max(a.Right(), b.Right()) - x0, std::max(a.Bottom(), b.Bottom()) - y0};
}

bool Overlap(const UiBox& a, const UiBox& b) { return a.x <= b.Right() && b.x <= a.Right() && a.y <= b.Bottom() && b.y <= a.Bottom(); }

class UiView final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 40 || r.h < 40 || !host.settings) return;
        const game_production::GameSettings& gs = *host.settings;
        const Vec2 screen{static_cast<float>(std::max(1, gs.display.width)), static_cast<float>(std::max(1, gs.display.height))};
        Rect area = r.Inset(kMargin, kMargin + kBarH, kMargin, kMargin + 18);
        if (area.w < 10 || area.h < 10) area = r;
        // The game's screen fitted into the panel, then the view's zoom (about the panel's middle) and its move.
        const Rect fit = game_production::GameScreenRect(area, screen.x / screen.y);
        const float zoom = std::clamp(state_.zoom, kMinZoom, kMaxZoom);
        const float scale = fit.w / screen.x * zoom;
        const Rect game{fit.x + fit.w * 0.5f + state_.pan.x - screen.x * scale * 0.5f, fit.y + fit.h * 0.5f + state_.pan.y - screen.y * scale * 0.5f,
                        screen.x * scale, screen.y * scale};
        const Affine toDc = Translate({game.x, game.y}) * Scaling(scale, scale);
        if (c.Time() - forgotAt_ > kForgetSeconds) {
            renderer_.Forget();
            forgotAt_ = c.Time();
        }

        c.P().PushClip(r);
        ID2D1DeviceContext* dc = c.P().DC();
        renderer_.Fill(dc, ToBox(r), theme::SceneBackground());
        renderer_.Fill(dc, ToBox(game), Color{gs.clearColor.r, gs.clearColor.g, gs.clearColor.b, 1});
        tree_ = BuildUi(host.scene, ScreenOf(gs, screen), host.selection);
        renderer_.Draw(dc, host.host.Graphics(), tree_, toDc, [&host](const AssetRef& ref) { return host.host.AssetContent(ref); });
        const std::optional<Rect> strip = Strip(r);
        const std::optional<Rect> alignBar = AlignBar(r, host);
        lastToDc_ = toDc;
        fit_ = fit;
        if (!host.playing) HandleInput(c, r, host, toDc, strip, alignBar);
        DrawGuides(c, game, toDc, gs, screen);
        DrawSnapping(c, r, toDc);
        DrawSelection(c, host, toDc);
        DrawHints(c, r);
        c.P().PopClip();
        if (strip) DrawStrip(c, *strip);
        if (alignBar && !host.playing) DrawAlignBar(c, *alignBar, host);
    }

    void LoadState(const json::Value& s) override {
        state_ = {};
        unknowns_ = {};
        reflect::Warnings warnings;
        if (s.IsObject()) reflect::Load(state_, s, &unknowns_, &warnings);
        for (const auto& wn : warnings) log::Warn("UI のビューの見え方: {}", wn);
    }
    json::Value SaveState() const override { return reflect::Save(state_, &unknowns_); }

private:
    // --- The strip: セーフエリア / アンカー on or off, the zoom, snapping ------------------------------
    static std::optional<Rect> Strip(const Rect& r) {
        if (r.w < 640 || r.h < kBarH + 40) return std::nullopt;
        return Rect{r.x + 8, r.y + 6, 624, kBarH - 4};
    }

    void DrawStrip(ui::Context& c, const Rect& s) {
        const Rect safe{s.x, s.y, 120, s.h}, anchors{s.x + 128, s.y, 108, s.h}, whole{s.x + 244, s.y, 96, s.h}, zoom{s.x + 346, s.y, 58, s.h};
        if (w::Button(c, "ui_safe", safe, Tr(L"セーフエリア"), state_.safeArea ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.safeArea = !state_.safeArea;
        }
        if (c.IsHovered(safe)) w::Tooltip(c, Tr(L"ゲームの設定の「画面」のセーフエリアを補助線で出す・消す"));
        if (w::Button(c, "ui_anchors", anchors, Tr(L"アンカー"), state_.anchors ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.anchors = !state_.anchors;
        }
        if (c.IsHovered(anchors)) w::Tooltip(c, Tr(L"選んだ物が親のどこに固定されているかの印を出す・消す"));
        if (w::Button(c, "ui_whole", whole, Tr(L"全体を見る"))) {
            state_.zoom = 1;
            state_.pan = {0, 0};
        }
        if (c.IsHovered(whole)) w::Tooltip(c, Tr(L"ゲームの画面全体が入る大きさに戻す (ホイールで拡大・縮小、右か中のドラッグで動かす)"));
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::TextDim;
        ts.h = ui::HAlign::Center;
        c.P().Text(zoom, std::format(L"{}%", static_cast<int>(std::round(std::clamp(state_.zoom, kMinZoom, kMaxZoom) * 100))), ts);
        // Snapping: to other things (補助線) and to the grid (格子, with its step).
        const Rect guides{s.x + 412, s.y, 84, s.h}, grid{s.x + 502, s.y, 60, s.h}, step{s.x + 566, s.y, 58, s.h};
        if (w::Button(c, "ui_snap_guides", guides, Tr(L"補助線"), state_.snapGuides ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.snapGuides = !state_.snapGuides;
        }
        if (c.IsHovered(guides)) w::Tooltip(c, Tr(L"動かす・大きさを変える物を、ほかの物や親の端と真ん中に吸い付ける (Alt を押している間は吸い付かない)"));
        if (w::Button(c, "ui_snap_grid", grid, Tr(L"格子"), state_.snapGrid ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.snapGrid = !state_.snapGrid;
        }
        if (c.IsHovered(grid)) w::Tooltip(c, Tr(L"格子に吸い付ける。右の数が格子の間隔 (キャンバスの基準の解像度の単位)"));
        w::NumberField(c, "ui_grid_step", step, state_.gridStep, 1, 1000, gridText_);
    }

    // --- The align bar (two or more selected): line them up, spread them ------------------------------
    std::optional<Rect> AlignBar(const Rect& r, const game_production::SceneViewHost& host) const {
        if (r.w < 640 || r.h < 2 * kBarH + 60 || SelectedNodes(host).size() < 2) return std::nullopt;
        return Rect{r.x + 8, r.y + 6 + kBarH, 624, kBarH - 4};
    }

    void DrawAlignBar(ui::Context& c, const Rect& s, game_production::SceneViewHost& host) {
        struct Item {
            const wchar_t* label;
            const wchar_t* tip;
            std::optional<UiAlign> align;
            bool across = true;
        };
        const Item items[] = {
            {L"左揃え", L"選んだ物の左の端を、一番左の物にそろえる", UiAlign::Left},
            {L"左右の真ん中", L"選んだ物の左右の真ん中を、全体の真ん中にそろえる", UiAlign::CenterX},
            {L"右揃え", L"選んだ物の右の端を、一番右の物にそろえる", UiAlign::Right},
            {L"上揃え", L"選んだ物の上の端を、一番上の物にそろえる", UiAlign::Top},
            {L"上下の真ん中", L"選んだ物の上下の真ん中を、全体の真ん中にそろえる", UiAlign::CenterY},
            {L"下揃え", L"選んだ物の下の端を、一番下の物にそろえる", UiAlign::Bottom},
            {L"横に等間隔", L"選んだ物 (3 つ以上) の横の間を同じにする (両端の物は動かない)", std::nullopt, true},
            {L"縦に等間隔", L"選んだ物 (3 つ以上) の縦の間を同じにする (両端の物は動かない)", std::nullopt, false},
        };
        const float bw = (s.w - 4 * 7) / 8;
        for (size_t i = 0; i < std::size(items); ++i) {
            const Rect b{s.x + (bw + 4) * static_cast<float>(i), s.y, bw, s.h};
            if (w::Button(c, std::format("ui_align_{}", i), b, Tr(items[i].label))) LineUp(host, items[i].align, items[i].across);
            if (c.IsHovered(b)) w::Tooltip(c, Tr(items[i].tip));
        }
    }

    // Moves the selected things (their 位置) so they line up / spread out: one undo entry.
    void LineUp(game_production::SceneViewHost& host, std::optional<UiAlign> align, bool across) {
        edits_.clear();
        for (const UiNode* n : SelectedNodes(host)) Begin(host, *n);
        if (edits_.size() < 2) {
            edits_.clear();
            return;
        }
        std::vector<UiBox> boxes;
        for (const Edited& e : edits_) boxes.push_back(PanelBoxOf(e, e.start));
        const std::vector<Vec2> shifts = align ? AlignShifts(boxes, *align) : DistributeShifts(boxes, across);
        for (size_t i = 0; i < edits_.size(); ++i) {
            const auto inv = Invert(edits_[i].parentToDc);
            UiRect* t = Target(edits_[i]);
            if (!inv || !t) continue;
            const Vec2 m = Linear(*inv, shifts[i]);
            t->position = {edits_[i].start.position.x + m.x, edits_[i].start.position.y + m.y};
        }
        Commit(host, align ? "UI をそろえる" : "UI を等間隔にする");
    }

    // --- Where things are on the panel ---------------------------------------------------------------
    // A point of a node's box (fx, fy: 0..1) on the panel, turned with it.
    static Vec2 BoxPoint(const UiNode& n, const Affine& toDc, float fx, float fy) {
        return Apply(toDc * n.self, {n.box.x + n.box.w * fx, n.box.y + n.box.h * fy});
    }
    // The corners of a node's box on the panel (top left, top right, bottom right, bottom left).
    static std::array<Vec2, 4> Corners(const UiNode& n, const Affine& toDc) {
        return {BoxPoint(n, toDc, 0, 0), BoxPoint(n, toDc, 1, 0), BoxPoint(n, toDc, 1, 1), BoxPoint(n, toDc, 0, 1)};
    }
    // The turning knob: above the top middle (along the box's own up).
    static Vec2 Knob(const UiNode& n, const Affine& toDc) {
        const Vec2 top = BoxPoint(n, toDc, 0.5f, 0), bottom = BoxPoint(n, toDc, 0.5f, 1);
        const float len = std::max(Distance(top, bottom), 1e-3f);
        return {top.x + (top.x - bottom.x) / len * kKnob, top.y + (top.y - bottom.y) / len * kKnob};
    }
    static Vec2 PivotOnPanel(const UiNode& n, const Affine& toDc) {
        const Vec2 p = PivotPoint(*n.place, n.box);
        return Apply(toDc * n.self, p);
    }

    // The frontmost UI thing under the panel's point (texts too: anything can be selected here).
    int PickAt(const Affine& toDc, Vec2 p) const {
        const auto inv = Invert(toDc);
        if (!inv) return 0;
        const Vec2 s = Apply(*inv, p);
        for (auto it = tree_.nodes.rbegin(); it != tree_.nodes.rend(); ++it) {
            if (it->parent < 0) continue;
            if (Hits(*it, s)) return it->entity;
        }
        return 0;
    }

    // The selected UI things (not canvases), in the tree's order; and the one the gizmo is for (only one selected).
    std::vector<const UiNode*> SelectedNodes(const game_production::SceneViewHost& host) const {
        std::vector<const UiNode*> out;
        for (const UiNode& n : tree_.nodes) {
            if (n.place && n.parent >= 0 && std::find(host.selection.begin(), host.selection.end(), n.entity) != host.selection.end()) out.push_back(&n);
        }
        return out;
    }
    const UiNode* Single(const game_production::SceneViewHost& host) const {
        const auto nodes = SelectedNodes(host);
        return nodes.size() == 1 ? nodes.front() : nullptr;
    }
    // Whether node i is under (or is) one of `entities`.
    bool Under(size_t i, const std::vector<int>& entities) const {
        for (int k = static_cast<int>(i); k >= 0; k = tree_.nodes[static_cast<size_t>(k)].parent) {
            if (std::find(entities.begin(), entities.end(), tree_.nodes[static_cast<size_t>(k)].entity) != entities.end()) return true;
        }
        return false;
    }

    // What of the selected thing's gizmo is under the mouse.
    enum class Part { None, Handle, Knob, Pivot };
    struct Hot {
        Part part = Part::None;
        UiHandle handle = UiHandle::TopLeft;
    };
    Hot HotAt(const UiNode& n, const Affine& toDc, Vec2 mouse) const {
        if (Distance(mouse, Knob(n, toDc)) <= kKnobRadius + 4) return {Part::Knob};
        if (Distance(mouse, PivotOnPanel(n, toDc)) <= 6) return {Part::Pivot};
        for (size_t i = 0; i < kHandleAt.size(); ++i) {
            const Vec2 h = BoxPoint(n, toDc, kHandleAt[i].first, kHandleAt[i].second);
            if (std::abs(mouse.x - h.x) <= kHandleSlop && std::abs(mouse.y - h.y) <= kHandleSlop) return {Part::Handle, static_cast<UiHandle>(i)};
        }
        return {};
    }

    // --- Input ---------------------------------------------------------------------------------
    void HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const Affine& toDc, const std::optional<Rect>& strip,
                     const std::optional<Rect>& alignBar) {
        const ui::Input& in = c.In();
        const Vec2 mouse{in.mouseX, in.mouseY};
        const bool overBars = (strip && strip->Contains(in.mouseX, in.mouseY)) || (alignBar && alignBar->Contains(in.mouseX, in.mouseY));
        const bool hovered = c.IsHovered(r) && !overBars;
        // The view: the wheel zooms about the mouse, a right or middle drag moves it.
        if (hovered && in.wheel != 0 && drag_ == Drag::None) ZoomAt(mouse, std::pow(1.15f, in.wheel));
        if (drag_ == Drag::None && hovered && (c.MousePressed(ui::Mouse::Right) || c.MousePressed(ui::Mouse::Middle))) {
            drag_ = Drag::Pan;
            panFrom_ = state_.pan;
            press_ = mouse;
            return;
        }
        if (drag_ == Drag::Pan) {
            if (!in.down[1] && !in.down[2]) {
                drag_ = Drag::None;
                return;
            }
            state_.pan = {panFrom_.x + mouse.x - press_.x, panFrom_.y + mouse.y - press_.y};
            return;
        }
        const UiNode* single = Single(host);
        if (drag_ == Drag::None) {
            hot_ = hovered && single ? HotAt(*single, toDc, mouse) : Hot{};
            if (hot_.part != Part::None) c.SetCursor(ui::Cursor::Hand);
        }
        if (drag_ == Drag::None && hovered && c.MousePressed()) {
            press_ = mouse;
            edits_.clear();
            if (hot_.part != Part::None && single && Begin(host, *single)) {
                FindTargets();
                drag_ = hot_.part == Part::Handle ? Drag::Resize : hot_.part == Part::Knob ? Drag::Rotate : Drag::Pivot;
                handle_ = hot_.handle;
                return;
            }
            pressed_ = PickAt(toDc, mouse);
            const bool adding = in.shift || in.ctrl;
            std::vector<int> sel(host.selection.begin(), host.selection.end());
            if (adding && pressed_) {
                // Shift / Ctrl: add it, or take it out.
                const auto it = std::find(sel.begin(), sel.end(), pressed_);
                if (it != sel.end()) sel.erase(it);
                else sel.push_back(pressed_);
                host.select(sel);
                drag_ = Drag::Cancelled;
            } else if (pressed_) {
                // On one of the selected: keep them (a drag moves them all); else that one alone.
                if (std::find(sel.begin(), sel.end(), pressed_) == sel.end()) host.select({pressed_});
                drag_ = Drag::Press;
            } else {
                // On nothing: a box to select by (Shift / Ctrl: adding to what is selected).
                marqueeFrom_ = adding ? sel : std::vector<int>{};
                if (!adding) host.select({});
                drag_ = Drag::Marquee;
            }
            return;
        }
        if (drag_ == Drag::None) return;
        const bool editing = drag_ == Drag::Move || drag_ == Drag::Resize || drag_ == Drag::Rotate || drag_ == Drag::Pivot;
        if (editing && HistoryMoved(host)) {
            edits_.clear();  // undo / redo while dragging: the drag stops (nothing pushed)
            drag_ = Drag::Cancelled;
        }
        if (drag_ == Drag::Marquee) {
            // Everything (not a canvas) whose box touches the dragged box.
            const UiBox box{std::min(press_.x, mouse.x), std::min(press_.y, mouse.y), std::abs(mouse.x - press_.x), std::abs(mouse.y - press_.y)};
            marquee_ = box;
            if (!in.down[0]) {
                std::vector<int> sel = marqueeFrom_;
                if (box.w >= kClickSlop || box.h >= kClickSlop) {
                    for (const UiNode& n : tree_.nodes) {
                        if (n.parent < 0 || !n.place || !Overlap(PanelBox(Corners(n, toDc)), box)) continue;
                        if (std::find(sel.begin(), sel.end(), n.entity) == sel.end()) sel.push_back(n.entity);
                    }
                }
                host.select(sel);
                marquee_.reset();
                drag_ = Drag::None;
            }
            return;
        }
        if (!in.down[0]) {
            if (editing) End(host);
            drag_ = Drag::None;
            snapX_.reset();
            snapY_.reset();
            return;
        }
        if (drag_ == Drag::Press && Distance(mouse, press_) >= kClickSlop) {
            // Every selected thing moves (not one under another that moves: it goes with its parent).
            const std::vector<int> sel(host.selection.begin(), host.selection.end());
            for (const UiNode* n : SelectedNodes(host)) {
                bool parentMoves = false;
                for (int k = n->parent; k >= 0 && !parentMoves; k = tree_.nodes[static_cast<size_t>(k)].parent) {
                    parentMoves = std::find(sel.begin(), sel.end(), tree_.nodes[static_cast<size_t>(k)].entity) != sel.end();
                }
                if (!parentMoves) Begin(host, *n);
            }
            if (edits_.empty()) {
                drag_ = Drag::Cancelled;
            } else {
                FindTargets();
                drag_ = Drag::Move;
            }
        }
        if (editing && !edits_.empty()) {
            if (c.KeyPressed(ui::Key::Escape)) {
                for (const Edited& e : edits_) {
                    if (UiRect* t = Target(e)) *t = e.start;
                }
                edits_.clear();
                drag_ = Drag::Cancelled;
                return;
            }
            ApplyDrag(mouse, in.shift, in.alt);
        }
    }

    // Zooms by `factor` keeping the panel's point `at` over the same place of the game.
    void ZoomAt(Vec2 at, float factor) {
        const float before = std::clamp(state_.zoom, kMinZoom, kMaxZoom);
        const float after = std::clamp(before * factor, kMinZoom, kMaxZoom);
        const float k = after / before;
        const Vec2 mid{fit_.x + fit_.w * 0.5f + state_.pan.x, fit_.y + fit_.h * 0.5f + state_.pan.y};
        const Vec2 midAfter{at.x + (mid.x - at.x) * k, at.y + (mid.y - at.y) * k};
        state_.pan = {midAfter.x - fit_.x - fit_.w * 0.5f, midAfter.y - fit_.y - fit_.h * 0.5f};
        state_.zoom = after;
    }

    // --- Editing -------------------------------------------------------------------------------
    struct Edited {
        int entity = 0;
        UiRect start;
        UiBox parentBox;    // the parent's box (in its units: where the anchors are)
        Affine parentToDc;  // the parent's units -> the panel (to turn the mouse's move into 位置)
        Affine selfToDc;    // its own units (turned) -> the panel (handles, the 中心)
        UiBox box;          // its box at the start (in its parent's units)
        Vec2 pivotOnPanel;  // where it turns, on the panel
        std::function<reflect::EditTarget()> resolve;
    };

    bool Begin(game_production::SceneViewHost& host, const UiNode& n) {
        if (!host.component || n.parent < 0) return false;
        auto resolve = host.component(n.entity, reflect::TypeOf<UiRect>().name);
        const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
        if (!t.object) return false;
        edits_.push_back(Edited{n.entity, *static_cast<const UiRect*>(t.object), tree_.nodes[static_cast<size_t>(n.parent)].box, lastToDc_ * n.toScreen,
                                lastToDc_ * n.self, n.box, PivotOnPanel(n, lastToDc_), std::move(resolve)});
        history_ = {host.host.History().UndoCount(), host.host.History().RedoCount()};
        return true;
    }

    // What the edited things may snap to: the sides and middles of everything else (not what moves, not
    // what is under it) - its parents and canvases too - on the panel.
    void FindTargets() {
        targetsX_.clear();
        targetsY_.clear();
        std::vector<int> moving;
        for (const Edited& e : edits_) moving.push_back(e.entity);
        for (size_t i = 0; i < tree_.nodes.size(); ++i) {
            if (Under(i, moving)) continue;
            const UiBox b = PanelBox(Corners(tree_.nodes[i], lastToDc_));
            targetsX_.insert(targetsX_.end(), {b.x, b.x + b.w * 0.5f, b.Right()});
            targetsY_.insert(targetsY_.end(), {b.y, b.y + b.h * 0.5f, b.Bottom()});
        }
    }

    // Where a UI の配置 would be on the panel (the box that holds it, turned), with an edit's parent.
    static UiBox PanelBoxOf(const Edited& e, const UiRect& r) {
        const UiBox b = PlaceBox(r, e.parentBox);
        const Affine m = e.parentToDc * About(PivotPoint(r, b), r.rotation, 1);
        return PanelBox({Apply(m, {b.x, b.y}), Apply(m, {b.Right(), b.y}), Apply(m, {b.Right(), b.Bottom()}), Apply(m, {b.x, b.Bottom()})});
    }
    // The parent's units are not turned on the panel (the snapping of a resize works then).
    static bool ParentUpright(const Edited& e) { return std::abs(e.parentToDc.b) < 1e-5f && std::abs(e.parentToDc.c) < 1e-5f; }

    static UiRect* Target(const Edited& e) {
        const reflect::EditTarget t = e.resolve();
        return static_cast<UiRect*>(t.object);
    }

    bool HistoryMoved(game_production::SceneViewHost& host) const {
        return history_ != std::pair{host.host.History().UndoCount(), host.host.History().RedoCount()};
    }

    // While dragging, UI の配置 is changed directly (the inspector follows); one undo entry at the end.
    // Shift: a corner keeps the shape; the turn goes in steps of 15 degrees. Alt: no snapping.
    void ApplyDrag(Vec2 mouse, bool shift, bool noSnap) {
        const Vec2 d{mouse.x - press_.x, mouse.y - press_.y};
        snapX_.reset();
        snapY_.reset();
        if (drag_ == Drag::Move) {
            // All of them by the same move on the panel; snapping looks at the box around them all.
            Vec2 move = d;
            if (!noSnap && state_.snapGuides) {
                std::optional<UiBox> all;
                for (const Edited& e : edits_) {
                    const UiBox b = PanelBoxOf(e, e.start);
                    all = all ? Union(*all, b) : b;
                }
                if (all) {
                    const UiBox pb{all->x + d.x, all->y + d.y, all->w, all->h};
                    const float ex[] = {pb.x, pb.x + pb.w * 0.5f, pb.Right()}, ey[] = {pb.y, pb.y + pb.h * 0.5f, pb.Bottom()};
                    move.x += SnapShift(ex, targetsX_, kSnapReach, &snapX_);
                    move.y += SnapShift(ey, targetsY_, kSnapReach, &snapY_);
                }
            }
            for (const Edited& e : edits_) {
                UiRect* t = Target(e);
                const auto inv = Invert(e.parentToDc);
                if (!t || !inv) continue;
                const Vec2 m = Linear(*inv, move);
                UiRect next = e.start;
                next.position = {e.start.position.x + m.x, e.start.position.y + m.y};
                // The grid (one thing moved: its top left, in its parent's units).
                if (!noSnap && state_.snapGrid && edits_.size() == 1) {
                    const UiBox box = PlaceBox(next, e.parentBox);
                    if (!snapX_) next.position.x += GridShift(box.x, state_.gridStep);
                    if (!snapY_) next.position.y += GridShift(box.y, state_.gridStep);
                }
                *t = next;
            }
            return;
        }
        if (edits_.empty()) return;
        const Edited& e = edits_.front();
        UiRect* t = Target(e);
        if (!t) return;
        UiRect next = e.start;
        if (drag_ == Drag::Resize) {
            // The move in its own (turned) units: what the handle pulls.
            const auto inv = Invert(e.selfToDc);
            if (!inv) return;
            Vec2 along = Linear(*inv, d);
            next = ResizeByHandle(e.start, e.parentBox, handle_, along, shift);
            // The sides it moves snap (an upright thing, its shape free): to another thing, else to the grid.
            if (!noSnap && !shift && std::abs(e.start.rotation) < 1e-4f && ParentUpright(e)) {
                const Vec2 sides = HandleSides(handle_);
                const UiBox pb = PanelBoxOf(e, next);
                const float ex[] = {sides.x > 0 ? pb.Right() : pb.x}, ey[] = {sides.y > 0 ? pb.Bottom() : pb.y};
                const float sx = sides.x != 0 && state_.snapGuides ? SnapShift(ex, targetsX_, kSnapReach, &snapX_) : 0.f;
                const float sy = sides.y != 0 && state_.snapGuides ? SnapShift(ey, targetsY_, kSnapReach, &snapY_) : 0.f;
                if (!sides.x) snapX_.reset();
                if (!sides.y) snapY_.reset();
                const Vec2 own = Linear(*inv, {sx, sy});
                along = {along.x + own.x, along.y + own.y};
                if (state_.snapGrid) {
                    const UiBox box = PlaceBox(ResizeByHandle(e.start, e.parentBox, handle_, along, false), e.parentBox);
                    if (sides.x != 0 && !snapX_) along.x += GridShift(sides.x > 0 ? box.Right() : box.x, state_.gridStep);
                    if (sides.y != 0 && !snapY_) along.y += GridShift(sides.y > 0 ? box.Bottom() : box.y, state_.gridStep);
                }
                next = ResizeByHandle(e.start, e.parentBox, handle_, along, false);
            }
        } else if (drag_ == Drag::Rotate) {
            next.rotation = RotateByDrag(e.start.rotation, e.pivotOnPanel, press_, mouse, shift ? 15.f : 0.f);
        } else if (drag_ == Drag::Pivot) {
            // The mouse in its own (unturned) box: the new 中心 as 0..1 of it (inside the box).
            const auto inv = Invert(e.selfToDc);
            if (!inv || e.box.w <= 1e-4f || e.box.h <= 1e-4f) return;
            const Vec2 q = Apply(*inv, mouse);
            const Vec2 pivot{std::clamp((q.x - e.box.x) / e.box.w, 0.f, 1.f), std::clamp((q.y - e.box.y) / e.box.h, 0.f, 1.f)};
            next = MovePivot(e.start, e.parentBox, pivot);
        }
        *t = next;
    }

    void End(game_production::SceneViewHost& host) {
        const char* label = drag_ == Drag::Resize   ? "UI の大きさを変更"
                            : drag_ == Drag::Rotate ? "UI を回転"
                            : drag_ == Drag::Pivot  ? "UI の回す中心を移動"
                                                    : "UI を移動";
        Commit(host, label);
    }

    // What the edits changed (already applied) goes into the history as one entry. Edit = undo entry =
    // unsaved (rule 7).
    void Commit(game_production::SceneViewHost& host, const char* label) {
        auto group = std::make_unique<CommandGroup>(label);
        const reflect::TypeDesc& type = reflect::TypeOf<UiRect>();
        for (const Edited& e : edits_) {
            const UiRect* t = Target(e);
            if (!t) continue;
            for (const char* field : {"position", "size", "rotation", "pivot"}) {
                const auto index = FieldIndex(field);
                if (!index) continue;
                const reflect::FieldDesc& fd = type.fields[*index];
                json::Value before = reflect::SaveField(fd, &e.start, nullptr);
                json::Value after = reflect::SaveField(fd, t, nullptr);
                if (before == after) continue;
                group->Add(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{*index}, std::move(before), std::move(after), e.resolve, label));
            }
        }
        if (!group->Empty()) host.host.History().Push(std::move(group));  // already applied
        edits_.clear();
    }

    // --- Drawing over the UI ---------------------------------------------------------------------
    void DrawGuides(ui::Context& c, const Rect& game, const Affine& toDc, const game_production::GameSettings& gs, Vec2 screen) {
        auto& p = c.P();
        p.StrokeRect(game, theme::SceneCamera(), 1.f);
        if (state_.safeArea) {
            const UiBox safe = SafeBox(screen, gs.display.safeAreaTopLeft, gs.display.safeAreaBottomRight);
            if (safe.w < screen.x || safe.h < screen.y) {
                const Vec2 a = Apply(toDc, {safe.x, safe.y}), b = Apply(toDc, {safe.Right(), safe.Bottom()});
                p.StrokeRect({a.x, a.y, b.x - a.x, b.y - a.y}, theme::AxisY().Alpha(0.8f), 1.f);
            }
        }
    }

    // The grid (when snapping to it: over each canvas, while its lines are far enough apart) and the
    // lines a drag snapped to.
    void DrawSnapping(ui::Context& c, const Rect& r, const Affine& toDc) {
        auto& p = c.P();
        if (state_.snapGrid && state_.gridStep >= 1) {
            for (const UiNode& n : tree_.nodes) {
                if (n.parent >= 0) continue;
                const Affine m = toDc * n.self;
                const float stepOnPanel = state_.gridStep * std::hypot(m.a, m.b);
                if (stepOnPanel < 8) continue;
                const ui::Color col = theme::SceneGrid();
                for (float x = n.box.x; x <= n.box.Right() + 1e-3f; x += state_.gridStep) {
                    const Vec2 a = Apply(m, {x, n.box.y}), b = Apply(m, {x, n.box.Bottom()});
                    p.Line(a.x, a.y, b.x, b.y, col, 1.f);
                }
                for (float y = n.box.y; y <= n.box.Bottom() + 1e-3f; y += state_.gridStep) {
                    const Vec2 a = Apply(m, {n.box.x, y}), b = Apply(m, {n.box.Right(), y});
                    p.Line(a.x, a.y, b.x, b.y, col, 1.f);
                }
            }
        }
        if (snapX_) p.Line(*snapX_, r.y, *snapX_, r.Bottom(), theme::GizmoHot(), 1.f);
        if (snapY_) p.Line(r.x, *snapY_, r.Right(), *snapY_, theme::GizmoHot(), 1.f);
        if (marquee_) {
            const Rect m{marquee_->x, marquee_->y, marquee_->w, marquee_->h};
            p.FillRect(m, theme::Accent.Alpha(0.12f));
            p.StrokeRect(m, theme::Accent, 1.f);
        }
    }

    void DrawSelection(ui::Context& c, const game_production::SceneViewHost& host, const Affine& toDc) {
        auto& p = c.P();
        const auto selected = SelectedNodes(host);
        for (const UiNode* s : selected) {
            const auto k = Corners(*s, toDc);
            for (int i = 0; i < 4; ++i) p.Line(k[i].x, k[i].y, k[(i + 1) % 4].x, k[(i + 1) % 4].y, theme::Accent, 1.5f);
        }
        const UiNode* n = selected.size() == 1 ? selected.front() : nullptr;
        if (!n) return;
        if (!host.playing) {
            auto hot = [&](Part part, UiHandle h = UiHandle::TopLeft) {
                const bool dragging = (part == Part::Handle && drag_ == Drag::Resize && handle_ == h) || (part == Part::Knob && drag_ == Drag::Rotate) ||
                                      (part == Part::Pivot && drag_ == Drag::Pivot);
                const bool over = drag_ == Drag::None && hot_.part == part && (part != Part::Handle || hot_.handle == h);
                return dragging || over ? theme::GizmoHot() : theme::Accent;
            };
            // The turning knob and its stem.
            const Vec2 top = BoxPoint(*n, toDc, 0.5f, 0), knob = Knob(*n, toDc);
            p.Line(top.x, top.y, knob.x, knob.y, theme::Accent, 1.f);
            p.FillCircle(knob.x, knob.y, kKnobRadius, hot(Part::Knob));
            // The 8 handles.
            for (size_t i = 0; i < kHandleAt.size(); ++i) {
                const Vec2 h = BoxPoint(*n, toDc, kHandleAt[i].first, kHandleAt[i].second);
                ui::Path sq;
                sq.Begin({h.x - kHandle, h.y - kHandle});
                sq.Line({h.x + kHandle, h.y - kHandle});
                sq.Line({h.x + kHandle, h.y + kHandle});
                sq.Line({h.x - kHandle, h.y + kHandle});
                p.FillPath(sq, hot(Part::Handle, static_cast<UiHandle>(i)));
            }
            // The 中心 (where it turns and where 位置 measures from): a ring.
            const Vec2 pv = PivotOnPanel(*n, toDc);
            p.FillCircle(pv.x, pv.y, 5, hot(Part::Pivot));
            p.FillCircle(pv.x, pv.y, 2.5f, theme::SceneBackground());
        }
        // Its anchors: where it is fixed in its parent (a small triangle pointing at each anchor's corner).
        if (!state_.anchors || n->parent < 0) return;
        const UiNode& parent = tree_.nodes[static_cast<size_t>(n->parent)];
        Vec2 mn, mx;
        AnchorRange(n->place->anchor, mn, mx);
        const Affine pm = toDc * n->toScreen;
        const UiBox& pb = parent.box;
        auto at = [&](float fx, float fy) { return Apply(pm, {pb.x + pb.w * fx, pb.y + pb.h * fy}); };
        const ui::Color col = theme::Text.Alpha(0.9f);
        const float s = 9.f;
        const std::array<std::pair<float, float>, 4> pts{{{mn.x, mn.y}, {mx.x, mn.y}, {mx.x, mx.y}, {mn.x, mx.y}}};
        const std::array<std::pair<float, float>, 4> dirs{{{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}}};
        for (size_t i = 0; i < 4; ++i) {
            const Vec2 a = at(pts[i].first, pts[i].second);
            const float dx = dirs[i].first, dy = dirs[i].second;
            ui::Path tri;
            tri.Begin(a);
            tri.Line({a.x + dx * s, a.y + dy * s * 0.4f});
            tri.Line({a.x + dx * s * 0.4f, a.y + dy * s});
            p.FillPath(tri, col);
        }
        // A line from the anchors' point to its 中心 (where 位置 measures from).
        const Vec2 from = at(mn.x + (mx.x - mn.x) * n->place->pivot.x, mn.y + (mx.y - mn.y) * n->place->pivot.y);
        const Vec2 to = PivotOnPanel(*n, toDc);
        p.Line(from.x, from.y, to.x, to.y, col.Alpha(0.5f), 1.f);
    }

    void DrawHints(ui::Context& c, const Rect& r) {
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Bottom;
        ts.wrap = true;
        c.P().Text(r.Inset(10, 8, 10, 6),
                   Tr(L"クリック: 選ぶ (Shift・Ctrl で足す)   空いた所のドラッグ: 囲んで選ぶ   ドラッグ: 動かす   四角: 大きさ (Shift で形を保つ)   上の丸: 回す   中の輪: 回す中心   Alt: 吸い付かない   ホイール: 拡大   右ドラッグ: 画面を動かす"),
                   ts);
        if (tree_.nodes.empty()) {
            ui::TextStyle hint = ts;
            hint.size = 13;
            hint.v = ui::VAlign::Center;
            hint.h = ui::HAlign::Center;
            hint.color = theme::TextFaint;
            c.P().Text(r.Inset(24), Tr(L"UI はまだありません。階層を右クリックして「キャンバスを作る」→ その下に「ボタンを作る」「文字を作る」などで作ります。"), hint);
        }
    }

    enum class Drag { None, Press, Move, Resize, Rotate, Pivot, Pan, Marquee, Cancelled };

    UiViewState state_;
    reflect::Unknowns unknowns_;
    UiRenderer renderer_;
    UiTree tree_;
    Affine lastToDc_;
    Rect fit_;
    double forgotAt_ = 0;
    Drag drag_ = Drag::None;
    Vec2 press_{0, 0};
    Vec2 panFrom_{0, 0};
    int pressed_ = 0;
    Hot hot_;
    UiHandle handle_ = UiHandle::TopLeft;
    std::vector<Edited> edits_;            // what a drag (or a line-up) changes
    std::vector<float> targetsX_, targetsY_;  // what they may snap to (FindTargets)
    std::pair<size_t, size_t> history_{0, 0};
    std::optional<float> snapX_, snapY_;  // the lines the drag snapped to (on the panel)
    std::optional<UiBox> marquee_;        // the box being dragged to select by (on the panel)
    std::vector<int> marqueeFrom_;        // what was selected before it (Shift / Ctrl: kept)
    std::string gridText_;                // the grid's step while typed
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateUiView() { return std::make_unique<UiView>(); }

}  // namespace rm::game_ui
