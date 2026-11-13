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
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

namespace rm::game_ui {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
using editor::Tr;
using ui::Rect;

constexpr float kMargin = 16.f;      // around the game screen in the panel
constexpr float kBarH = 28.f;        // the strip at the top left
constexpr float kHandle = 5.f;       // half a corner's square (DIPs)
constexpr float kHandleSlop = 8.f;   // how near the mouse must be to a corner
constexpr float kClickSlop = 4.f;    // a press that moves less than this is a click
constexpr double kForgetSeconds = 2.0;  // pictures and fonts read again this often (their files may change)

UiBox ToBox(const Rect& r) { return {r.x, r.y, r.w, r.h}; }

Vec2 Linear(const Affine& m, Vec2 v) { return {m.a * v.x + m.c * v.y, m.b * v.x + m.d * v.y}; }

std::optional<size_t> FieldIndex(std::string_view name) {
    const auto fields = reflect::TypeOf<UiRect>().fields;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == name) return i;
    }
    return std::nullopt;
}

class UiView final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 40 || r.h < 40 || !host.settings) return;
        const game_production::GameSettings& gs = *host.settings;
        const Vec2 screen{static_cast<float>(std::max(1, gs.display.width)), static_cast<float>(std::max(1, gs.display.height))};
        Rect area = r.Inset(kMargin, kMargin + kBarH, kMargin, kMargin + 18);
        if (area.w < 10 || area.h < 10) area = r;
        const Rect game = game_production::GameScreenRect(area, screen.x / screen.y);
        const Affine toDc = Translate({game.x, game.y}) * Scaling(game.w / screen.x, game.h / screen.y);
        if (c.Time() - forgotAt_ > kForgetSeconds) {
            renderer_.Forget();
            forgotAt_ = c.Time();
        }

        c.P().PushClip(r);
        ID2D1DeviceContext* dc = c.P().DC();
        renderer_.Fill(dc, ToBox(r), theme::SceneBackground());
        renderer_.Fill(dc, ToBox(game), Color{gs.clearColor.r, gs.clearColor.g, gs.clearColor.b, 1});
        tree_ = BuildUi(host.scene, ScreenOf(gs, screen), host.selection);
        renderer_.Draw(dc, host.host.Graphics(), tree_, toDc, [&host](const AssetRef& ref) -> std::optional<std::string> {
            const std::filesystem::path file = host.host.AssetFile(ref);
            if (file.empty()) return std::nullopt;
            return platform::ReadFileBytes(file, nullptr);
        });
        const std::optional<Rect> strip = Strip(r);
        lastToDc_ = toDc;
        if (!host.playing) HandleInput(c, r, host, toDc, strip);
        DrawGuides(c, game, toDc, gs, screen);
        DrawSelection(c, host, toDc);
        DrawHints(c, r);
        c.P().PopClip();
        if (strip) DrawStrip(c, *strip);
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
    // --- The strip: セーフエリア / アンカー on or off ---------------------------------------------
    static std::optional<Rect> Strip(const Rect& r) {
        if (r.w < 260 || r.h < kBarH + 40) return std::nullopt;
        return Rect{r.x + 8, r.y + 6, 236, kBarH - 4};
    }

    void DrawStrip(ui::Context& c, const Rect& s) {
        const Rect safe{s.x, s.y, 120, s.h}, anchors{s.x + 128, s.y, 108, s.h};
        if (w::Button(c, "ui_safe", safe, Tr(L"セーフエリア"), state_.safeArea ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.safeArea = !state_.safeArea;
        }
        if (c.IsHovered(safe)) w::Tooltip(c, Tr(L"ゲームの設定の「画面」のセーフエリアを補助線で出す・消す"));
        if (w::Button(c, "ui_anchors", anchors, Tr(L"アンカー"), state_.anchors ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.anchors = !state_.anchors;
        }
        if (c.IsHovered(anchors)) w::Tooltip(c, Tr(L"選んだ物が親のどこに固定されているかの印を出す・消す"));
    }

    // --- Where things are on the panel ---------------------------------------------------------------
    // The corners of a node's box on the panel (top left, top right, bottom right, bottom left).
    static std::array<Vec2, 4> Corners(const UiNode& n, const Affine& toDc) {
        const Affine m = toDc * n.self;
        return {Apply(m, {n.box.x, n.box.y}), Apply(m, {n.box.Right(), n.box.y}), Apply(m, {n.box.Right(), n.box.Bottom()}),
                Apply(m, {n.box.x, n.box.Bottom()})};
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

    const UiNode* Selected(const game_production::SceneViewHost& host) const {
        for (auto it = host.selection.rbegin(); it != host.selection.rend(); ++it) {
            const UiNode* n = tree_.Find(*it);
            if (n && n->place) return n;
        }
        return nullptr;
    }

    // --- Input ---------------------------------------------------------------------------------
    void HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const Affine& toDc, const std::optional<Rect>& strip) {
        const ui::Input& in = c.In();
        const Vec2 mouse{in.mouseX, in.mouseY};
        const bool hovered = c.IsHovered(r) && !(strip && strip->Contains(in.mouseX, in.mouseY));
        const UiNode* sel = Selected(host);
        if (drag_ == Drag::None) {
            hotCorner_ = -1;
            if (hovered && sel) {
                const auto k = Corners(*sel, toDc);
                for (int i = 0; i < 4; ++i) {
                    if (std::abs(mouse.x - k[i].x) <= kHandleSlop && std::abs(mouse.y - k[i].y) <= kHandleSlop) hotCorner_ = i;
                }
            }
        }
        if (drag_ == Drag::None && hovered && c.MousePressed()) {
            press_ = mouse;
            if (hotCorner_ >= 0 && sel && Begin(host, *sel)) {
                drag_ = Drag::Resize;
                corner_ = hotCorner_;
            } else {
                pressed_ = PickAt(toDc, mouse);
                host.select(pressed_ ? std::vector<int>{pressed_} : std::vector<int>{});
                drag_ = Drag::Press;
            }
            return;
        }
        if (drag_ == Drag::None) return;
        if ((drag_ == Drag::Move || drag_ == Drag::Resize) && HistoryMoved(host)) {
            edited_.reset();  // undo / redo while dragging: the drag stops (nothing pushed)
            drag_ = Drag::Cancelled;
        }
        if (!in.down[0]) {
            if (drag_ == Drag::Move || drag_ == Drag::Resize) End(host);
            drag_ = Drag::None;
            return;
        }
        if (drag_ == Drag::Press && pressed_ && std::hypot(mouse.x - press_.x, mouse.y - press_.y) >= kClickSlop) {
            const UiNode* n = tree_.Find(pressed_);
            drag_ = n && n->place && Begin(host, *n) ? Drag::Move : Drag::Cancelled;
        }
        if ((drag_ == Drag::Move || drag_ == Drag::Resize) && edited_) {
            if (c.KeyPressed(ui::Key::Escape)) {
                if (UiRect* t = Target()) *t = edited_->start;
                edited_.reset();
                drag_ = Drag::Cancelled;
                return;
            }
            ApplyDrag(mouse);
        }
    }

    struct Edited {
        int entity = 0;
        UiRect start;
        Affine parentToDc;  // the parent's units -> the panel (to turn the mouse's move into 位置)
        Affine selfToDc;    // its own units (turned) -> the panel (for a corner)
        std::function<reflect::EditTarget()> resolve;
    };

    bool Begin(game_production::SceneViewHost& host, const UiNode& n) {
        if (!host.component) return false;
        auto resolve = host.component(n.entity, reflect::TypeOf<UiRect>().name);
        const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
        if (!t.object) return false;
        edited_ = Edited{n.entity, *static_cast<const UiRect*>(t.object), lastToDc_ * n.toScreen, lastToDc_ * n.self, std::move(resolve)};
        history_ = {host.host.History().UndoCount(), host.host.History().RedoCount()};
        return true;
    }

    UiRect* Target() const {
        if (!edited_) return nullptr;
        const reflect::EditTarget t = edited_->resolve();
        return static_cast<UiRect*>(t.object);
    }

    bool HistoryMoved(game_production::SceneViewHost& host) const {
        return history_ != std::pair{host.host.History().UndoCount(), host.host.History().RedoCount()};
    }

    // While dragging, UI の配置 is changed directly (the inspector follows); one undo entry at the end.
    void ApplyDrag(Vec2 mouse) {
        UiRect* t = Target();
        if (!t) return;
        const Vec2 d{mouse.x - press_.x, mouse.y - press_.y};
        UiRect next = edited_->start;
        if (drag_ == Drag::Move) {
            const auto inv = Invert(edited_->parentToDc);
            if (!inv) return;
            const Vec2 m = Linear(*inv, d);
            next.position = {edited_->start.position.x + m.x, edited_->start.position.y + m.y};
        } else {
            // The move in its own (turned) units; the corner's sides grow, the opposite corner stays.
            const auto inv = Invert(edited_->selfToDc);
            if (!inv) return;
            const Vec2 m = Linear(*inv, d);
            const float sx = (corner_ == 1 || corner_ == 2) ? 1.f : -1.f, sy = corner_ >= 2 ? 1.f : -1.f;
            const float dw = m.x * sx, dh = m.y * sy;  // (a box never goes below nothing: PlaceBox)
            next.size = {edited_->start.size.x + dw, edited_->start.size.y + dh};
            const Vec2 shiftOwn{sx > 0 ? dw * edited_->start.pivot.x : -dw * (1 - edited_->start.pivot.x),
                                sy > 0 ? dh * edited_->start.pivot.y : -dh * (1 - edited_->start.pivot.y)};
            const Vec2 shift = Linear(Rotation(edited_->start.rotation), shiftOwn);  // (its turn: into the parent's units)
            next.position = {edited_->start.position.x + shift.x, edited_->start.position.y + shift.y};
        }
        *t = next;
    }

    // Edit = undo entry = unsaved (rule 7).
    void End(game_production::SceneViewHost& host) {
        UiRect* t = Target();
        if (!edited_ || !t) {
            edited_.reset();
            return;
        }
        const char* label = drag_ == Drag::Resize ? "UI の大きさを変更" : "UI を移動";
        auto group = std::make_unique<CommandGroup>(label);
        const reflect::TypeDesc& type = reflect::TypeOf<UiRect>();
        for (const char* field : {"position", "size"}) {
            const auto index = FieldIndex(field);
            if (!index) continue;
            const reflect::FieldDesc& fd = type.fields[*index];
            json::Value before = reflect::SaveField(fd, &edited_->start, nullptr);
            json::Value after = reflect::SaveField(fd, t, nullptr);
            if (before == after) continue;
            group->Add(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{*index}, std::move(before), std::move(after),
                                                                  edited_->resolve, label));
        }
        if (!group->Empty()) host.host.History().Push(std::move(group));  // already applied while dragging
        edited_.reset();
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

    void DrawSelection(ui::Context& c, const game_production::SceneViewHost& host, const Affine& toDc) {
        const UiNode* n = Selected(host);
        if (!n) return;
        auto& p = c.P();
        const auto k = Corners(*n, toDc);
        for (int i = 0; i < 4; ++i) p.Line(k[i].x, k[i].y, k[(i + 1) % 4].x, k[(i + 1) % 4].y, theme::Accent, 1.5f);
        if (!host.playing) {
            for (int i = 0; i < 4; ++i) {
                const ui::Color col = i == hotCorner_ || (drag_ == Drag::Resize && i == corner_) ? theme::GizmoHot() : theme::Accent;
                ui::Path sq;
                sq.Begin({k[i].x - kHandle, k[i].y - kHandle});
                sq.Line({k[i].x + kHandle, k[i].y - kHandle});
                sq.Line({k[i].x + kHandle, k[i].y + kHandle});
                sq.Line({k[i].x - kHandle, k[i].y + kHandle});
                p.FillPath(sq, col);
            }
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
        const Vec2 to = Apply(toDc * n->self, PivotPoint(*n->place, n->box));
        p.Line(from.x, from.y, to.x, to.y, col.Alpha(0.5f), 1.f);
    }

    void DrawHints(ui::Context& c, const Rect& r) {
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Bottom;
        ts.wrap = true;
        c.P().Text(r.Inset(10, 8, 10, 6), Tr(L"クリック: 選ぶ   ドラッグ: 動かす   角の四角: 大きさを変える   Esc: ドラッグをやめる   階層の右クリック →「キャンバスを作る」などで UI を作れます"), ts);
        if (tree_.nodes.empty()) {
            ui::TextStyle hint = ts;
            hint.size = 13;
            hint.v = ui::VAlign::Center;
            hint.h = ui::HAlign::Center;
            hint.color = theme::TextFaint;
            c.P().Text(r.Inset(24), Tr(L"UI はまだありません。階層を右クリックして「キャンバスを作る」→ その下に「ボタンを作る」「文字を作る」などで作ります。"), hint);
        }
    }

    enum class Drag { None, Press, Move, Resize, Cancelled };

    UiViewState state_;
    reflect::Unknowns unknowns_;
    UiRenderer renderer_;
    UiTree tree_;
    Affine lastToDc_;
    double forgotAt_ = 0;
    Drag drag_ = Drag::None;
    Vec2 press_{0, 0};
    int pressed_ = 0;
    int hotCorner_ = -1, corner_ = 0;
    std::optional<Edited> edited_;
    std::pair<size_t, size_t> history_{0, 0};
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateUiView() { return std::make_unique<UiView>(); }

}  // namespace rm::game_ui
