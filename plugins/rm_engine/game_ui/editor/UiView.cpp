#include "rm_engine/game_ui/editor/UiView.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "core/UndoHistory.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Image.h"
#include "platform/Shell.h"
#include "rm_engine/game_ui/editor/UiViewState.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"
#include "rm_engine/game_ui/runtime/UiMotion.h"
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

constexpr float kMargin = 16.f;      // パネルの中でゲームの画面のまわりの余白
constexpr float kBarH = 28.f;        // 左上の帯
constexpr float kHandle = 4.5f;      // ハンドルの四角の半分 (DIP)
constexpr float kHandleSlop = 8.f;   // マウスがハンドルにどれだけ近ければ掴めるか
constexpr float kKnob = 26.f;        // 回すつまみ: 枠の上辺の真ん中からこれだけ上
constexpr float kKnobRadius = 5.f;
constexpr float kClickSlop = 4.f;    // これより動かない押下はクリック
constexpr float kMinZoom = 0.1f, kMaxZoom = 10.f;
constexpr float kSnapReach = 6.f;    // パネル上の DIP: 辺がどれだけ近づくと吸着するか
constexpr double kForgetSeconds = 2.0;  // 絵とフォントはこの間隔で読み直す (ファイルが変わるかもしれない)

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

// UiHandle の順のハンドル。枠の上の点 (0..1: x, y)。
constexpr std::array<std::pair<float, float>, 8> kHandleAt{
    {{0.f, 0.f}, {0.5f, 0.f}, {1.f, 0.f}, {1.f, 0.5f}, {1.f, 1.f}, {0.5f, 1.f}, {0.f, 1.f}, {0.f, 0.5f}}};

// パネル上の角をぐるりと囲む枠 (回したものでは: それを収める枠)。
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
        // ゲームの画面をパネルに収め、そのうえでビューの拡大 (パネルの真ん中を中心に) と移動をかけたもの。
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
        // 作っているキャンバス (切ってあってもウィンドウを見せる。ほかも見る を入れない限り、それだけ)。
        if (editCanvas_ && !host.scene.Find(editCanvas_)) editCanvas_ = 0;
        std::vector<int> show(host.selection.begin(), host.selection.end());
        if (editCanvas_) show.push_back(editCanvas_);
        tree_ = BuildUi(host.scene, ScreenOf(gs, screen), show);
        if (editCanvas_ && !state_.otherCanvases) tree_ = OnlyCanvas(tree_, editCanvas_);
        // (動画 はここでは再生しない - ゲームのビューが再生する、音は 1 つ - 最初の絵だけ。虹色 は
        // 回る: あるあいだは毎フレーム描き直す)
        renderer_.SetPlaying(false);
        renderer_.SetShowClipped(true);  // (スクロールの枠の外の物も薄く: 並べるため)
        renderer_.Draw(dc, host.host.Graphics(), tree_, toDc, [&host](const AssetRef& ref) { return host.host.AssetContent(ref); });
        if (UiRenderer::Animates(tree_)) c.RequestAnimation();
        const std::optional<Rect> strip = Strip(r);
        const std::optional<Rect> row2 = Row2(r);
        const std::optional<Rect> alignBar = AlignBar(r, host);
        lastToDc_ = toDc;
        fit_ = fit;
        std::vector<Rect> bars;
        for (const auto& b : {strip, row2, alignBar}) {
            if (b) bars.push_back(*b);
        }
        if (anchorMenu_.open) bars.push_back(AnchorMenuRect());
        if (!host.playing) HandleInput(c, r, host, toDc, bars);
        DrawGuides(c, game, toDc, gs, screen);
        DrawSnapping(c, r, toDc);
        DrawSelection(c, host, toDc);
        // (操作の説明はヘルプに: 2026-11-20 から画面には出さない)
        if (!host.playing) {
            DropPicture(c, r, host, toDc);
            EditText(c, host, toDc);
        }
        c.P().PopClip();
        if (strip) DrawStrip(c, *strip);
        if (row2 && !host.playing) DrawRow2(c, *row2, host);
        if (alignBar && !host.playing) DrawAlignBar(c, *alignBar, host);
        if (!host.playing) DrawAnchorMenu(c, host);
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
    // --- 帯: セーフエリア / アンカー の切り替え、拡大、吸着 ----------------------------------------
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
        // 吸着: ほかのものへ (補助線) と、格子へ (格子、その間隔つき)。
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

    // --- 整列バー (2 つ以上選んだとき): 揃える、等間隔に並べる -----------------------------------
    std::optional<Rect> AlignBar(const Rect& r, const game_production::SceneViewHost& host) const {
        if (r.w < 640 || r.h < 3 * kBarH + 60 || SelectedNodes(host).size() < 2) return std::nullopt;
        return Rect{r.x + 8, r.y + 6 + 2 * kBarH, 624, kBarH - 4};
    }

    // --- 2 行目: 作っているキャンバス。1 つ選んだときは、そのアンカーと順 --------------------------
    static std::optional<Rect> Row2(const Rect& r) {
        if (r.w < 640 || r.h < 2 * kBarH + 60) return std::nullopt;
        // (ボタンが届くところまでの幅 - 道の 点を足す / 点を消す も: そこへのクリックはビューのものではない)
        return Rect{r.x + 8, r.y + 6 + kBarH, std::min(824.f, r.w - 16), kBarH - 4};
    }

    // シーンのキャンバス (切ってあってもなくても)、シーンの順に。
    static std::vector<std::pair<int, std::string>> Canvases(const game_production::SceneViewHost& host) {
        std::vector<std::pair<int, std::string>> out;
        const std::string type = reflect::TypeOf<UiCanvas>().name;
        for (const auto& t : host.scene.TreeOrder()) {
            const game_production::Component* comp = t.entity->FindComponent(type);
            // ウィンドウだけ: 別のキャンバスの中にあるキャンバスは、そのウィンドウの一部 (一緒に作る)。
            if (comp && comp->Known() && IsWindow(host.scene, t.entity->data.id)) out.emplace_back(t.entity->data.id, t.entity->data.name);
        }
        return out;
    }

    void DrawRow2(ui::Context& c, const Rect& s, game_production::SceneViewHost& host) {
        const Rect canvas{s.x, s.y, 220, s.h}, others{s.x + 226, s.y, 110, s.h};
        std::wstring name = Tr(L"キャンバス: 全部");
        for (const auto& [id, n] : Canvases(host)) {
            if (id == editCanvas_) name = TrF(L"キャンバス: {}", Utf8ToWide(n));
        }
        if (w::ComboBox(c, "ui_canvas", canvas, name).clicked) canvasMenu_.OpenAt(canvas.x, canvas.Bottom());
        if (c.IsHovered(canvas)) w::Tooltip(c, Tr(L"作るキャンバス (ウィンドウ) を選ぶ。選んだキャンバスは、有効を外していてもここに出ます"));
        if (canvasMenu_.open) {
            const auto list = Canvases(host);
            std::vector<w::MenuItem> items{{Tr(L"全部")}};
            for (const auto& [id, n] : list) items.push_back({Utf8ToWide(n), {}, true, false, id == editCanvas_});
            const int chosen = w::MenuPopup(c, canvasMenu_, items, 240);
            if (chosen == 0) editCanvas_ = 0;
            if (chosen > 0 && static_cast<size_t>(chosen) <= list.size()) {
                editCanvas_ = list[static_cast<size_t>(chosen - 1)].first;
                host.select({editCanvas_});
            }
        }
        if (editCanvas_) {
            if (w::Button(c, "ui_other_canvases", others, Tr(L"ほかも見る"), state_.otherCanvases ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
                state_.otherCanvases = !state_.otherCanvases;
            }
            if (c.IsHovered(others)) w::Tooltip(c, Tr(L"選んだキャンバスのほかに、有効なキャンバスも重ねて見る (HUD の上のポーズ画面など)"));
        }
        // 1 つ選んだとき: そのアンカー (選ぶ絵) と、兄弟の中での順。
        const UiNode* single = Single(host);
        if (!single) return;
        const Rect anchor{s.x + 344, s.y, 120, s.h}, front{s.x + 470, s.y, 74, s.h}, back{s.x + 550, s.y, 74, s.h};
        if (w::Button(c, "ui_anchor_pick", anchor, Tr(L"アンカー …"))) anchorMenu_.OpenAt(anchor.x, anchor.Bottom() + 2);
        if (c.IsHovered(anchor)) w::Tooltip(c, Tr(L"固定する場所を図から選ぶ (場所は変わらない)。Shift: 回す中心もそこへ。Alt: 位置もそこへ (伸ばす向きは親いっぱい)"));
        if (w::Button(c, "ui_front", front, Tr(L"一番前へ"))) Reorder(host, *single, true);
        if (c.IsHovered(front)) w::Tooltip(c, Tr(L"兄弟の中で一番手前に描く (手前の順)"));
        if (w::Button(c, "ui_back", back, Tr(L"一番後ろへ"))) Reorder(host, *single, false);
        if (c.IsHovered(back)) w::Tooltip(c, Tr(L"兄弟の中で一番奥に描く (手前の順)"));
        // その 道 (道に沿って動く): 点を 1 つ足す、1 つ減らす。
        if (const UiPath* path = PathOf(host, single->entity); path && !host.playing) {
            const Rect add{s.x + 636, s.y, 90, s.h}, remove{s.x + 732, s.y, 90, s.h};
            if (w::Button(c, "ui_path_add", add, Tr(L"点を足す"))) {
                std::vector<Vec2> pts = path->points;
                const Vec2 last = pts.empty() ? Vec2{0, 0} : pts.back();
                const Vec2 before = pts.size() >= 2 ? pts[pts.size() - 2] : Vec2{last.x - 80, last.y};
                pts.push_back({last.x + (last.x - before.x), last.y + (last.y - before.y)});  // (進んできた方向へ)
                pathPoint_ = static_cast<int>(pts.size()) - 1;
                SetPath(host, single->entity, path->points, pts, "道に点を足す");
            }
            if (c.IsHovered(add)) w::Tooltip(c, Tr(L"道の最後に点を 1 つ足す (点はドラッグで動かせます)"));
            const bool canRemove = path->points.size() > 2;
            if (w::Button(c, "ui_path_remove", remove, Tr(L"点を消す"), w::ButtonKind::Secondary, canRemove)) {
                std::vector<Vec2> pts = path->points;
                const size_t i = pathPoint_ >= 0 && static_cast<size_t>(pathPoint_) < pts.size() ? static_cast<size_t>(pathPoint_) : pts.size() - 1;
                pts.erase(pts.begin() + static_cast<std::ptrdiff_t>(i));
                pathPoint_ = -1;
                SetPath(host, single->entity, path->points, pts, "道の点を消す");
            }
            if (c.IsHovered(remove)) w::Tooltip(c, Tr(L"選んだ点 (無ければ最後の点) を消す。2 つは残ります"));
        }
    }

    // --- 道に沿って動く (UI-4): その点をパネル上でドラッグする -------------------------------------
    static const UiPath* PathOf(const game_production::SceneViewHost& host, int entity) {
        const game_production::SceneEntity* e = host.scene.Find(entity);
        const game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<UiPath>().name) : nullptr;
        return c && c->Known() ? static_cast<const UiPath*>(c->value.Get()) : nullptr;
    }
    // パネル上の 道 の点: 中心 (位置 の基準) から、親の単位で。
    static Vec2 PathPointOnPanel(const UiNode& n, const Affine& toDc, Vec2 p) {
        const Vec2 pv = PivotPoint(*n.place, n.box);
        return Apply(toDc * n.toScreen, {pv.x + p.x, pv.y + p.y});
    }
    // 点が変わった (すでに、またはこれから書く): Undo の 1 件。
    void SetPath(game_production::SceneViewHost& host, int entity, const std::vector<Vec2>& before, const std::vector<Vec2>& after, const char* label) {
        if (!host.component) return;
        auto resolve = host.component(entity, reflect::TypeOf<UiPath>().name);
        const reflect::TypeDesc& type = reflect::TypeOf<UiPath>();
        std::optional<size_t> index;
        for (size_t i = 0; i < type.fields.size(); ++i) {
            if (std::string_view(type.fields[i].name) == "points") index = i;
        }
        const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
        if (!index || !t.object) return;
        auto* path = static_cast<UiPath*>(t.object);
        UiPath was = *path, now = *path;
        was.points = before;
        now.points = after;
        json::Value b = reflect::SaveField(type.fields[*index], &was, nullptr);
        json::Value a = reflect::SaveField(type.fields[*index], &now, nullptr);
        if (a == b) return;
        path->points = after;
        auto group = std::make_unique<CommandGroup>(label);
        group->Add(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{*index}, std::move(b), std::move(a), resolve, label));
        host.host.History().Push(std::move(group));  // すでに適用済み
    }

    // アンカーの選択: 4 x 4 の絵 (親の中の枠と、固定される所の点 / 線)。
    Rect AnchorMenuRect() const { return {anchorMenu_.x, anchorMenu_.y, 4 * 40.f + 12, 4 * 40.f + 12}; }
    void DrawAnchorMenu(ui::Context& c, game_production::SceneViewHost& host) {
        if (!anchorMenu_.open) return;
        const UiNode* single = Single(host);
        if (!single) {
            anchorMenu_.open = false;
            return;
        }
        const Rect box = AnchorMenuRect();
        w::DrawSurface(c, box, w::Surface::Popup);
        int curRow = 1, curCol = 1;
        AnchorCell(single->place->anchor, curRow, curCol);
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                const Rect cell{box.x + 6 + 40.f * static_cast<float>(col), box.y + 6 + 40.f * static_cast<float>(row), 38, 38};
                const auto res = w::Selectable(c, std::format("ui_anchor_{}_{}", row, col), cell, row == curRow && col == curCol);
                // その絵: 親 (輪郭) と、そのものが固定される所 (点。引き伸ばすときは線)。
                auto& p = c.P();
                const Rect in = cell.Inset(8);
                p.StrokeRect(in, theme::TextDim, 1.f);
                Vec2 mn, mx;
                AnchorRange(AnchorAt(row, col), mn, mx);
                const float x0 = in.x + in.w * mn.x, x1 = in.x + in.w * mx.x, y0 = in.y + in.h * mn.y, y1 = in.y + in.h * mx.y;
                const ui::Color mark = theme::Accent;
                if (x0 == x1 && y0 == y1) p.FillCircle(x0, y0, 3, mark);
                if (x0 != x1) p.Line(x0, y0, x1, y0, mark, 2.f);
                if (y0 != y1) p.Line(x0, y0, x0, y1, mark, 2.f);
                if (x0 != x1 && y0 != y1) {
                    p.Line(x1, y0, x1, y1, mark, 2.f);
                    p.Line(x0, y1, x1, y1, mark, 2.f);
                }
                if (res.clicked) {
                    SetAnchor(host, *single, AnchorAt(row, col), c.In().shift, c.In().alt);
                    anchorMenu_.open = false;
                }
            }
        }
        if (c.KeyPressed(ui::Key::Escape) || (c.MousePressed() && !c.IsHovered(box) && !anchorMenu_.fresh)) anchorMenu_.open = false;
        anchorMenu_.fresh = false;
    }

    // 新しいアンカー (場所は保つ: Reanchored)。Shift: 中心 もそこへ。Alt: 位置 もそこへ
    // (引き伸ばす向きは親を埋める)。
    void SetAnchor(game_production::SceneViewHost& host, const UiNode& n, UiAnchor anchor, bool pivotToo, bool placeToo) {
        edits_.clear();
        if (!Begin(host, n)) return;
        const Edited& e = edits_.front();
        UiRect next = Reanchored(e.start, anchor, e.parentBox);
        Vec2 mn, mx;
        AnchorRange(anchor, mn, mx);
        if (pivotToo) next = MovePivot(next, e.parentBox, {(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f});
        if (placeToo) {
            next.position = {0, 0};
            if (mn.x != mx.x) next.size.x = 0;
            if (mn.y != mx.y) next.size.y = 0;
        }
        if (UiRect* t = Target(e)) *t = next;
        Commit(host, "アンカーを変える");
    }

    // 兄弟の中で最初 / 最後 (手前の順: 最大より 1 つ大きい / 最小より 1 つ小さい)。
    void Reorder(game_production::SceneViewHost& host, const UiNode& n, bool front) {
        std::optional<int> most;
        for (const UiNode& o : tree_.nodes) {
            if (o.parent != n.parent || o.entity == n.entity || !o.place) continue;
            most = most ? (front ? std::max(*most, o.place->order) : std::min(*most, o.place->order)) : o.place->order;
        }
        edits_.clear();
        if (!Begin(host, n)) return;
        if (UiRect* t = Target(edits_.front())) t->order = most ? (front ? *most + 1 : *most - 1) : t->order;
        Commit(host, front ? "一番前へ" : "一番後ろへ");
    }

    // --- アセット パネルから落とした絵: そこに 画像 を作る (落とした先のものの下に) --------------
    void DropPicture(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const Affine& toDc) {
        if (!host.editScene || tree_.nodes.empty()) return;
        const auto isPicture = [&host](const w::DragThing& t) {
            if (t.kind != "asset" || t.values.size() != 1) return false;
            const auto file = host.host.AssetFile(AssetRef{t.values[0]});
            return !file.empty() && asset::KindOf(PathToUtf8(file.filename().wstring()), false) == asset::Kind::Image;
        };
        const w::DragThing* dropped = w::DropTarget(c, r, isPicture);
        if (!dropped) return;
        const Vec2 at{c.In().mouseX, c.In().mouseY};
        // マウスの下の一番手前のものの中へ。なければその下のキャンバス、それもなければ最初のキャンバス。
        int parentIndex = -1;
        if (const int picked = PickAt(toDc, at)) {
            for (size_t i = 0; i < tree_.nodes.size(); ++i) {
                if (tree_.nodes[i].entity == picked) parentIndex = static_cast<int>(i);
            }
        }
        if (parentIndex < 0) {
            for (size_t i = 0; i < tree_.nodes.size(); ++i) {
                const UiNode& n = tree_.nodes[i];
                if (n.parent < 0 && (parentIndex < 0 || Overlap(PanelBox(Corners(n, toDc)), {at.x, at.y, 0, 0}))) parentIndex = static_cast<int>(i);
            }
        }
        if (parentIndex < 0) return;
        const UiNode& parent = tree_.nodes[static_cast<size_t>(parentIndex)];
        const auto inv = Invert(toDc * parent.self);
        if (!inv) return;
        const Vec2 inParent = Apply(*inv, at);
        const AssetRef ref{dropped->values[0]};
        const std::filesystem::path file = host.host.AssetFile(ref);
        // 絵の大きさのまま (その画素をキャンバスの単位として)。ただし親の半分まで。
        Vec2 size{200, 200};
        std::string err;
        if (const auto img = platform::LoadImageFile(host.host.Graphics().Wic(), file, platform::PixelOrder::Rgba, {}, &err)) {
            const float k = std::min({1.f, parent.box.w * 0.5f / std::max(1.f, static_cast<float>(img->width)), parent.box.h * 0.5f / std::max(1.f, static_cast<float>(img->height))});
            size = {static_cast<float>(img->width) * k, static_cast<float>(img->height) * k};
        }
        const std::string name = PathToUtf8(file.stem().wstring());
        const int parentEntity = parent.entity;
        const UiBox parentBox = parent.box;
        host.editScene({parentEntity}, "画像を置く", [&](game_production::Scene& scene) -> std::vector<int> {
            const int id = scene.Create(name, parentEntity);
            scene.AddComponent(id, reflect::TypeOf<UiRect>());
            scene.AddComponent(id, reflect::TypeOf<UiImage>());
            game_production::SceneEntity* e = scene.Find(id);
            if (!e) return {};
            if (auto* rc = e->FindComponent(reflect::TypeOf<UiRect>().name); rc && rc->Known()) {
                auto& rect = *static_cast<UiRect*>(rc->value.Get());
                rect.size = size;
                rect.position = {inParent.x - (parentBox.x + parentBox.w * 0.5f), inParent.y - (parentBox.y + parentBox.h * 0.5f)};
            }
            if (auto* ic = e->FindComponent(reflect::TypeOf<UiImage>().name); ic && ic->Known()) static_cast<UiImage*>(ic->value.Get())->image = ref;
            return {id};
        });
    }

    // --- 直接打つ文字 (文字 を持つものをダブルクリック) --------------------------------------------
    void StartTextEdit(ui::Context& c, game_production::SceneViewHost& host, int entity) {
        auto resolve = host.component(entity, reflect::TypeOf<UiText>().name);
        const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
        if (!t.object) return;
        textEntity_ = entity;
        textResolve_ = std::move(resolve);
        textBefore_ = static_cast<const UiText*>(t.object)->text;
        textNow_ = textBefore_;
        c.FocusText(c.MakeId("ui_text_edit"), Utf8ToWide(textNow_), true);
        textStarted_ = false;
    }

    void EditText(ui::Context& c, game_production::SceneViewHost& host, const Affine& toDc) {
        if (!textEntity_) return;
        const UiNode* n = tree_.Find(textEntity_);
        const reflect::EditTarget t = textResolve_ ? textResolve_() : reflect::EditTarget{};
        auto* text = static_cast<UiText*>(t.object);
        if (!n || !text) {
            textEntity_ = 0;
            return;
        }
        const UiBox b = PanelBox(Corners(*n, toDc));
        const Rect field{b.x, b.y + std::max(0.f, (b.h - 26) * 0.5f), std::max(b.w, 120.f), 26};
        w::TextField(c, "ui_text_edit", field, textNow_);
        text->text = textNow_;  // (すぐに見える。欄を出るとき Undo の 1 件)
        bool focused = c.Focused() == c.MakeId("ui_text_edit");
        // Enter: 確定。Esc: 元どおり (欄そのものは、どちらのキーも受け取らない)。
        if (focused && (c.KeyPressed(ui::Key::Enter) || c.KeyPressed(ui::Key::Escape))) {
            c.ClearFocus();
            focused = false;
        }
        if (!textStarted_) {
            textStarted_ = focused;
            return;
        }
        if (focused) return;
        // 離れたとき (Enter、ほかの所のクリック): 変更が履歴に入る。Esc: 元どおり。
        if (c.KeyPressed(ui::Key::Escape)) textNow_ = textBefore_;
        text->text = textBefore_;
        if (textNow_ != textBefore_) {
            const reflect::TypeDesc& type = reflect::TypeOf<UiText>();
            for (size_t i = 0; i < type.fields.size(); ++i) {
                if (std::string_view(type.fields[i].name) != "text") continue;
                host.host.History().Push(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{i}, json::Value(textBefore_), json::Value(textNow_),
                                                                                   textResolve_, "文字を変更"),
                                         false);
            }
        }
        textEntity_ = 0;
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

    // 選んだものを (位置 を) 動かして、揃える / 等間隔に並べる: Undo の 1 件。
    void LineUp(game_production::SceneViewHost& host, std::optional<UiAlign> align, bool across) {
        edits_.clear();
        // (選んだものの下にあるものは、その親と一緒に動く: 自分でもう一度揃えると
        // 2 回動いてしまう - 親のずれと、自分のずれ。)
        for (const UiNode* n : OutermostSelected(host)) Begin(host, *n);
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

    // --- パネル上でものがある所 ----------------------------------------------------------------------
    // パネル上の、ノードの枠の点 (fx, fy: 0..1)。ノードと一緒に回す。
    static Vec2 BoxPoint(const UiNode& n, const Affine& toDc, float fx, float fy) {
        return Apply(toDc * n.self, {n.box.x + n.box.w * fx, n.box.y + n.box.h * fy});
    }
    // パネル上の、ノードの枠の角 (左上、右上、右下、左下)。
    static std::array<Vec2, 4> Corners(const UiNode& n, const Affine& toDc) {
        return {BoxPoint(n, toDc, 0, 0), BoxPoint(n, toDc, 1, 0), BoxPoint(n, toDc, 1, 1), BoxPoint(n, toDc, 0, 1)};
    }
    // 回すつまみ: 上辺の真ん中の上 (枠自身の上方向に沿って)。
    static Vec2 Knob(const UiNode& n, const Affine& toDc) {
        const Vec2 top = BoxPoint(n, toDc, 0.5f, 0), bottom = BoxPoint(n, toDc, 0.5f, 1);
        const float len = std::max(Distance(top, bottom), 1e-3f);
        return {top.x + (top.x - bottom.x) / len * kKnob, top.y + (top.y - bottom.y) / len * kKnob};
    }
    static Vec2 PivotOnPanel(const UiNode& n, const Affine& toDc) {
        const Vec2 p = PivotPoint(*n.place, n.box);
        return Apply(toDc * n.self, p);
    }

    // パネルの点の下にある一番手前の UI のもの (文字も: ここでは何でも選べる)。
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

    // 選んだ UI のもの (キャンバスは除く)。ツリーの順。それと、ギズモが付くもの (1 つだけ選んだとき)。
    std::vector<const UiNode*> SelectedNodes(const game_production::SceneViewHost& host) const {
        std::vector<const UiNode*> out;
        for (const UiNode& n : tree_.nodes) {
            if (n.place && n.parent >= 0 && std::find(host.selection.begin(), host.selection.end(), n.entity) != host.selection.end()) out.push_back(&n);
        }
        return out;
    }
    // 選んだもののうち、ほかの選んだものの下にないもの (親と一緒に動く)。
    std::vector<const UiNode*> OutermostSelected(const game_production::SceneViewHost& host) const {
        std::vector<const UiNode*> out;
        for (const UiNode* n : SelectedNodes(host)) {
            bool parentSelected = false;
            for (int k = n->parent; k >= 0 && !parentSelected; k = tree_.nodes[static_cast<size_t>(k)].parent) {
                parentSelected = std::find(host.selection.begin(), host.selection.end(), tree_.nodes[static_cast<size_t>(k)].entity) != host.selection.end();
            }
            if (!parentSelected) out.push_back(n);
        }
        return out;
    }
    const UiNode* Single(const game_production::SceneViewHost& host) const {
        const auto nodes = SelectedNodes(host);
        return nodes.size() == 1 ? nodes.front() : nullptr;
    }
    // ノード i が `entities` のどれかの下にある (またはそのもの) か。
    bool Under(size_t i, const std::vector<int>& entities) const {
        for (int k = static_cast<int>(i); k >= 0; k = tree_.nodes[static_cast<size_t>(k)].parent) {
            if (std::find(entities.begin(), entities.end(), tree_.nodes[static_cast<size_t>(k)].entity) != entities.end()) return true;
        }
        return false;
    }

    // 選んだもののギズモのうち、マウスの下にあるもの。
    enum class Part { None, Handle, Knob, Pivot, PathPoint };
    struct Hot {
        Part part = Part::None;
        UiHandle handle = UiHandle::TopLeft;
        int point = -1;  // PathPoint: どの点か
    };
    // `pivotFirst` (Shift): 道の点より 中心 を優先する - 最初の点はたいてい中心の所 (そのものがある所)
    // にあり、そこでは点のほうをよく動かすため。
    Hot HotAt(const UiNode& n, const Affine& toDc, Vec2 mouse, const UiPath* path, bool pivotFirst) const {
        if (Distance(mouse, Knob(n, toDc)) <= kKnobRadius + 4) return {Part::Knob};
        const bool onPivot = Distance(mouse, PivotOnPanel(n, toDc)) <= 6;
        if (onPivot && pivotFirst) return {Part::Pivot};
        if (path) {
            for (size_t i = path->points.size(); i-- > 0;) {
                if (Distance(mouse, PathPointOnPanel(n, toDc, path->points[i])) <= 7) return {Part::PathPoint, UiHandle::TopLeft, static_cast<int>(i)};
            }
        }
        if (onPivot) return {Part::Pivot};
        for (size_t i = 0; i < kHandleAt.size(); ++i) {
            const Vec2 h = BoxPoint(n, toDc, kHandleAt[i].first, kHandleAt[i].second);
            if (std::abs(mouse.x - h.x) <= kHandleSlop && std::abs(mouse.y - h.y) <= kHandleSlop) return {Part::Handle, static_cast<UiHandle>(i)};
        }
        return {};
    }

    // --- 入力 -----------------------------------------------------------------------------------
    void HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const Affine& toDc, const std::vector<Rect>& bars) {
        const ui::Input& in = c.In();
        const Vec2 mouse{in.mouseX, in.mouseY};
        const bool overBars = std::any_of(bars.begin(), bars.end(), [&](const Rect& b) { return b.Contains(in.mouseX, in.mouseY); });
        const bool hovered = c.IsHovered(r) && !overBars && !textEntity_ && !w::DraggedThing() && !canvasMenu_.open && !anchorMenu_.open;
        // ビュー: ホイールでマウスのあたりを中心に拡大し、右またはまん中のドラッグで動かす。
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
            hot_ = hovered && single ? HotAt(*single, toDc, mouse, PathOf(host, single->entity), in.shift) : Hot{};
            if (hot_.part != Part::None) c.SetCursor(ui::Cursor::Hand);
        }
        if (drag_ == Drag::None && hovered && c.MousePressed()) {
            press_ = mouse;
            edits_.clear();
            if (hot_.part == Part::PathPoint && single) {
                // 道の点: ドラッグする (終わりに Undo の 1 件)。
                const UiPath* path = PathOf(host, single->entity);
                if (path && hot_.point >= 0 && static_cast<size_t>(hot_.point) < path->points.size()) {
                    pathEntity_ = single->entity;
                    pathPoint_ = hot_.point;
                    pathStart_ = pathLast_ = path->points;
                    pathToDc_ = toDc * single->toScreen;
                    history_ = {host.host.History().UndoCount(), host.host.History().RedoCount()};
                    drag_ = Drag::PathPoint;
                }
                return;
            }
            pathPoint_ = -1;  // (ほかの所を押した: 点は選ばれない)
            if (hot_.part != Part::None && single && Begin(host, *single)) {
                FindTargets();
                drag_ = hot_.part == Part::Handle ? Drag::Resize : hot_.part == Part::Knob ? Drag::Rotate : Drag::Pivot;
                handle_ = hot_.handle;
                return;
            }
            pressed_ = PickAt(toDc, mouse);
            // 文字 を持つものをダブルクリック: そこで打つ。
            const bool twice = pressed_ && pressed_ == lastClicked_ && c.Time() - lastClickAt_ < 0.4;
            lastClicked_ = pressed_;
            lastClickAt_ = c.Time();
            if (twice && host.scene.Find(pressed_) && host.scene.Find(pressed_)->FindComponent(reflect::TypeOf<UiText>().name)) {
                StartTextEdit(c, host, pressed_);
                drag_ = Drag::Cancelled;
                return;
            }
            const bool adding = in.shift || in.ctrl;
            std::vector<int> sel(host.selection.begin(), host.selection.end());
            if (adding && pressed_) {
                // Shift / Ctrl: 加える、または外す。
                const auto it = std::find(sel.begin(), sel.end(), pressed_);
                if (it != sel.end()) sel.erase(it);
                else sel.push_back(pressed_);
                host.select(sel);
                drag_ = Drag::Cancelled;
            } else if (pressed_) {
                // 選んであるもののどれかの上: そのまま保つ (ドラッグで全部動く)。そうでなければそれだけ。
                if (std::find(sel.begin(), sel.end(), pressed_) == sel.end()) host.select({pressed_});
                drag_ = Drag::Press;
            } else {
                // 何もない所: 範囲で選ぶ枠 (Shift / Ctrl: 選んであるものに加える)。
                marqueeFrom_ = adding ? sel : std::vector<int>{};
                if (!adding) host.select({});
                drag_ = Drag::Marquee;
            }
            return;
        }
        if (drag_ == Drag::None) return;
        if (drag_ == Drag::PathPoint) {
            DragPathPoint(host, mouse, in.down[0]);
            return;
        }
        const bool editing = drag_ == Drag::Move || drag_ == Drag::Resize || drag_ == Drag::Rotate || drag_ == Drag::Pivot;
        if (editing && HistoryMoved(host)) {
            AbandonDrag();  // ドラッグ中の Undo / Redo: ドラッグをやめる (戻す。何も積まない)
            drag_ = Drag::Cancelled;
        }
        if (drag_ == Drag::Marquee) {
            // 枠がドラッグした枠に触れるものすべて (キャンバスは除く)。
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
        if (drag_ == Drag::Move) dropOn_ = in.ctrl ? DropParent(toDc, mouse) : -1;
        if (!in.down[0]) {
            // Ctrl を押したままほかのもの (またはキャンバス) の上で離した: その子になる。場所はそのまま。
            if (drag_ == Drag::Move && dropOn_ >= 0) {
                Rehome(host, toDc, tree_.nodes[static_cast<size_t>(dropOn_)]);
            } else if (editing) {
                End(host);
            }
            drag_ = Drag::None;
            dropOn_ = -1;
            snapX_.reset();
            snapY_.reset();
            return;
        }
        if (drag_ == Drag::Press && Distance(mouse, press_) >= kClickSlop) {
            // 選んだものがすべて動く (動くほかのものの下にあるものは別: 親と一緒に動く)。
            for (const UiNode* n : OutermostSelected(host)) Begin(host, *n);
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

    // Ctrl で落としたとき、動かしているものを下に入れる先のノード: マウスの下の一番手前で、動かして
    // いるもの (とその下) でないもの。なければその下のキャンバス。-1: なし。
    int DropParent(const Affine& toDc, Vec2 at) const {
        std::vector<int> moving;
        for (const Edited& e : edits_) moving.push_back(e.entity);
        const auto inv = Invert(toDc);
        if (!inv) return -1;
        const Vec2 s = Apply(*inv, at);
        for (int i = static_cast<int>(tree_.nodes.size()) - 1; i >= 0; --i) {
            const UiNode& n = tree_.nodes[static_cast<size_t>(i)];
            if (Under(static_cast<size_t>(i), moving)) continue;
            if (n.parent < 0 ? PanelBox(Corners(n, toDc)).Contains(at) : Hits(n, s)) return i;
        }
        return -1;
    }

    // `parent` の下に移したもの。今のパネル上の場所のまま (その 中心、その回転): Undo の 1 件。
    void Rehome(game_production::SceneViewHost& host, const Affine& toDc, const UiNode& parent) {
        if (!host.editScene) {
            End(host);
            return;
        }
        struct Move {
            int entity;
            UiRect rect;
        };
        std::vector<Move> moves;
        std::vector<int> touched{parent.entity};
        const Affine parentSelf = toDc * parent.self;
        const auto inv = Invert(parentSelf);
        const float parentTurn = std::atan2(parentSelf.b, parentSelf.a) * 180.f / 3.14159265f;
        for (const Edited& e : edits_) {
            UiRect* t = Target(e);
            if (!t || !inv) continue;
            const UiBox box = PlaceBox(*t, e.parentBox);
            const Vec2 pivotOnPanel = Apply(e.parentToDc, PivotPoint(*t, box));
            const float oldTurn = std::atan2(e.parentToDc.b, e.parentToDc.a) * 180.f / 3.14159265f;
            moves.push_back({e.entity, Rehomed(*t, e.parentBox, parent.box, Apply(*inv, pivotOnPanel), t->rotation + oldTurn - parentTurn)});
            *t = e.start;  // (記録は、ドラッグの前のシーンを見る)
            touched.push_back(e.entity);
            if (const auto* se = host.scene.Find(e.entity)) touched.push_back(se->data.parent);
        }
        edits_.clear();
        host.editScene(touched, "UI の親を変える", [&](game_production::Scene& scene) -> std::vector<int> {
            std::vector<int> ids;
            for (const Move& m : moves) {
                ids.push_back(m.entity);
                const int one[] = {m.entity};
                scene.Move(one, parent.entity, game_production::Scene::kEnd);
                if (auto* se = scene.Find(m.entity)) {
                    if (auto* rc = se->FindComponent(reflect::TypeOf<UiRect>().name); rc && rc->Known()) *static_cast<UiRect*>(rc->value.Get()) = m.rect;
                }
            }
            return ids;
        });
    }

    // パネルの点 `at` がゲームの同じ場所の上に残るように、`factor` 倍に拡大する。
    void ZoomAt(Vec2 at, float factor) {
        const float before = std::clamp(state_.zoom, kMinZoom, kMaxZoom);
        const float after = std::clamp(before * factor, kMinZoom, kMaxZoom);
        const float k = after / before;
        const Vec2 mid{fit_.x + fit_.w * 0.5f + state_.pan.x, fit_.y + fit_.h * 0.5f + state_.pan.y};
        const Vec2 midAfter{at.x + (mid.x - at.x) * k, at.y + (mid.y - at.y) * k};
        state_.pan = {midAfter.x - fit_.x - fit_.w * 0.5f, midAfter.y - fit_.y - fit_.h * 0.5f};
        state_.zoom = after;
    }

    // --- 編集 -----------------------------------------------------------------------------------
    struct Edited {
        int entity = 0;
        UiRect start;
        UiRect last;        // ドラッグが最後に書いたもの (ドラッグ中の Undo / Redo: AbandonDrag)
        UiBox parentBox;    // 親の枠 (その単位で: アンカーがある所)
        Affine parentToDc;  // 親の単位 -> パネル (マウスの動きを 位置 にするため)
        Affine selfToDc;    // そのものの単位 (回した) -> パネル (ハンドル、中心)
        UiBox box;          // 開始時のその枠 (親の単位で)
        Vec2 pivotOnPanel;  // 回る所 (パネル上)
        std::function<reflect::EditTarget()> resolve;
    };

    bool Begin(game_production::SceneViewHost& host, const UiNode& n) {
        if (!host.component || n.parent < 0) return false;
        auto resolve = host.component(n.entity, reflect::TypeOf<UiRect>().name);
        const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
        if (!t.object) return false;
        const UiRect& now = *static_cast<const UiRect*>(t.object);
        edits_.push_back(Edited{n.entity, now, now, tree_.nodes[static_cast<size_t>(n.parent)].box, lastToDc_ * n.toScreen, lastToDc_ * n.self, n.box,
                                PivotOnPanel(n, lastToDc_), std::move(resolve)});
        history_ = {host.host.History().UndoCount(), host.host.History().RedoCount()};
        return true;
    }

    // 編集するものが吸着してよい先: ほかのすべてのものの辺と真ん中 (動くものとその下のものは除く) -
    // その親とキャンバスも - パネル上で。
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

    // UI の配置 がパネル上にある場所 (それを収める枠。回したもの)。編集の親つき。
    static UiBox PanelBoxOf(const Edited& e, const UiRect& r) {
        const UiBox b = PlaceBox(r, e.parentBox);
        const Affine m = e.parentToDc * About(PivotPoint(r, b), r.rotation, 1);
        return PanelBox({Apply(m, {b.x, b.y}), Apply(m, {b.Right(), b.y}), Apply(m, {b.Right(), b.Bottom()}), Apply(m, {b.x, b.Bottom()})});
    }
    // 親の単位がパネル上で回っていない (そのとき大きさ変更の吸着が働く)。
    static bool ParentUpright(const Edited& e) { return std::abs(e.parentToDc.b) < 1e-5f && std::abs(e.parentToDc.c) < 1e-5f; }

    static UiRect* Target(const Edited& e) {
        const reflect::EditTarget t = e.resolve();
        return static_cast<UiRect*>(t.object);
    }

    bool HistoryMoved(game_production::SceneViewHost& host) const {
        return history_ != std::pair{host.host.History().UndoCount(), host.host.History().RedoCount()};
    }

    // ドラッグ中は UI の配置を直接変える (インスペクターが追いかける)。終わりに Undo の 1 件。
    // Shift: 角は形を保つ。回転は 15 度ずつ。Alt: 吸着しない。
    void ApplyDrag(Vec2 mouse, bool shift, bool noSnap) {
        const Vec2 d{mouse.x - press_.x, mouse.y - press_.y};
        snapX_.reset();
        snapY_.reset();
        if (drag_ == Drag::Move) {
            // 全部を、パネル上の同じ動きで。吸着は、全部を囲む枠を見る。
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
            for (Edited& e : edits_) {
                UiRect* t = Target(e);
                const auto inv = Invert(e.parentToDc);
                if (!t || !inv) continue;
                const Vec2 m = Linear(*inv, move);
                UiRect next = e.start;
                next.position = {e.start.position.x + m.x, e.start.position.y + m.y};
                // 格子 (1 つ動かしたとき: その左上、親の単位で)。
                if (!noSnap && state_.snapGrid && edits_.size() == 1) {
                    const UiBox box = PlaceBox(next, e.parentBox);
                    if (!snapX_) next.position.x += GridShift(box.x, state_.gridStep);
                    if (!snapY_) next.position.y += GridShift(box.y, state_.gridStep);
                }
                *t = next;
                e.last = next;
            }
            return;
        }
        if (edits_.empty()) return;
        Edited& e = edits_.front();
        UiRect* t = Target(e);
        if (!t) return;
        UiRect next = e.start;
        if (drag_ == Drag::Resize) {
            // そのものの (回した) 単位での動き: ハンドルが引くもの。
            const auto inv = Invert(e.selfToDc);
            if (!inv) return;
            Vec2 along = Linear(*inv, d);
            next = ResizeByHandle(e.start, e.parentBox, handle_, along, shift);
            // 動く辺が吸着する (まっすぐなもの、形は自由): ほかのものへ、なければ格子へ。
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
            // そのものの (回していない) 枠の中のマウス: 新しい 中心 を枠の 0..1 で (枠の内側)。
            const auto inv = Invert(e.selfToDc);
            if (!inv || e.box.w <= 1e-4f || e.box.h <= 1e-4f) return;
            const Vec2 q = Apply(*inv, mouse);
            const Vec2 pivot{std::clamp((q.x - e.box.x) / e.box.w, 0.f, 1.f), std::clamp((q.y - e.box.y) / e.box.h, 0.f, 1.f)};
            next = MovePivot(e.start, e.parentBox, pivot);
        }
        *t = next;
        e.last = next;
    }

    // 道の点がマウスに付いてくる (親の単位で)。離すと Undo の 1 件。履歴がその下で動いた:
    // 戻す (Undo / Redo が設定したものは残る)。
    void DragPathPoint(game_production::SceneViewHost& host, Vec2 mouse, bool held) {
        auto resolve = host.component ? host.component(pathEntity_, reflect::TypeOf<UiPath>().name) : nullptr;
        const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
        auto* path = static_cast<UiPath*>(t.object);
        if (!path || static_cast<size_t>(pathPoint_) >= path->points.size() || path->points.size() != pathStart_.size()) {
            drag_ = Drag::Cancelled;
            return;
        }
        if (HistoryMoved(host)) {
            if (path->points == pathLast_) path->points = pathStart_;
            drag_ = Drag::Cancelled;
            return;
        }
        const auto inv = Invert(pathToDc_);
        if (!inv) return;
        const Vec2 m = Linear(*inv, {mouse.x - press_.x, mouse.y - press_.y});
        const Vec2 start = pathStart_[static_cast<size_t>(pathPoint_)];
        path->points[static_cast<size_t>(pathPoint_)] = {start.x + m.x, start.y + m.y};
        pathLast_ = path->points;
        if (!held) {
            const std::vector<Vec2> now = path->points;
            path->points = pathStart_;
            SetPath(host, pathEntity_, pathStart_, now, "道の点を動かす");
            drag_ = Drag::None;
        }
    }

    // 履歴がドラッグの下で動いた (ボタンを押したまま Ctrl+Z): ドラッグが変えた各欄 - で、そのあと
    // ほかに変わっていないもの - は、始めの値に戻る (Undo / Redo が設定したものは残る)。
    // 何も積まない: シーンは履歴が言うとおり (憲法 7)。
    void AbandonDrag() {
        for (const Edited& e : edits_) {
            UiRect* t = Target(e);
            if (!t) continue;  // なくなった
            if (t->anchor == e.last.anchor) t->anchor = e.start.anchor;
            if (t->position == e.last.position) t->position = e.start.position;
            if (t->size == e.last.size) t->size = e.start.size;
            if (t->rotation == e.last.rotation) t->rotation = e.start.rotation;
            if (t->pivot == e.last.pivot) t->pivot = e.start.pivot;
        }
        edits_.clear();
        log::Info("ドラッグ中に元に戻す / やり直しがあったので、ドラッグをやめました");
    }

    void End(game_production::SceneViewHost& host) {
        const char* label = drag_ == Drag::Resize   ? "UI の大きさを変更"
                            : drag_ == Drag::Rotate ? "UI を回転"
                            : drag_ == Drag::Pivot  ? "UI の回す中心を移動"
                                                    : "UI を移動";
        Commit(host, label);
    }

    // 編集が変えたもの (すでに適用済み) は、1 件として履歴に入る。編集 = Undo の 1 件 = 未保存
    // (憲法 7)。
    void Commit(game_production::SceneViewHost& host, const char* label) {
        auto group = std::make_unique<CommandGroup>(label);
        const reflect::TypeDesc& type = reflect::TypeOf<UiRect>();
        for (const Edited& e : edits_) {
            const UiRect* t = Target(e);
            if (!t) continue;
            for (const char* field : {"anchor", "position", "size", "rotation", "pivot", "order"}) {
                const auto index = FieldIndex(field);
                if (!index) continue;
                const reflect::FieldDesc& fd = type.fields[*index];
                json::Value before = reflect::SaveField(fd, &e.start, nullptr);
                json::Value after = reflect::SaveField(fd, t, nullptr);
                if (before == after) continue;
                group->Add(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{*index}, std::move(before), std::move(after), e.resolve, label));
            }
        }
        if (!group->Empty()) host.host.History().Push(std::move(group));  // すでに適用済み
        edits_.clear();
    }

    // --- UI の上に描く ----------------------------------------------------------------------------
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

    // 格子 (それに吸着するとき: 各キャンバスの上に、線の間隔が十分あいているあいだ) と、ドラッグが
    // 吸着した線。
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
        if (dropOn_ >= 0 && static_cast<size_t>(dropOn_) < tree_.nodes.size()) {
            const auto k = Corners(tree_.nodes[static_cast<size_t>(dropOn_)], toDc);
            for (int i = 0; i < 4; ++i) p.Line(k[i].x, k[i].y, k[(i + 1) % 4].x, k[(i + 1) % 4].y, theme::GizmoHot(), 2.f);
        }
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
        // その 道: 歩く線とその点 (選んだ点と、マウスの下の点は明るく)。
        if (const UiPath* path = n->place ? PathOf(host, n->entity) : nullptr) {
            const UiPathLine line = PathLine(*path);
            for (size_t i = 1; i < line.points.size(); ++i) {
                const Vec2 a = PathPointOnPanel(*n, toDc, line.points[i - 1]), b = PathPointOnPanel(*n, toDc, line.points[i]);
                p.Line(a.x, a.y, b.x, b.y, theme::AxisY().Alpha(0.9f), 1.5f);
            }
            for (size_t i = 0; i < path->points.size(); ++i) {
                const Vec2 pt = PathPointOnPanel(*n, toDc, path->points[i]);
                const bool lit = static_cast<int>(i) == pathPoint_ || (drag_ == Drag::None && hot_.part == Part::PathPoint && hot_.point == static_cast<int>(i));
                p.FillCircle(pt.x, pt.y, 5.5f, lit ? theme::GizmoHot() : theme::AxisY());
                p.FillCircle(pt.x, pt.y, 2.5f, theme::SceneBackground());
            }
        }
        if (!host.playing) {
            auto hot = [&](Part part, UiHandle h = UiHandle::TopLeft) {
                const bool dragging = (part == Part::Handle && drag_ == Drag::Resize && handle_ == h) || (part == Part::Knob && drag_ == Drag::Rotate) ||
                                      (part == Part::Pivot && drag_ == Drag::Pivot);
                const bool over = drag_ == Drag::None && hot_.part == part && (part != Part::Handle || hot_.handle == h);
                return dragging || over ? theme::GizmoHot() : theme::Accent;
            };
            // 回すつまみとその茎。
            const Vec2 top = BoxPoint(*n, toDc, 0.5f, 0), knob = Knob(*n, toDc);
            p.Line(top.x, top.y, knob.x, knob.y, theme::Accent, 1.f);
            p.FillCircle(knob.x, knob.y, kKnobRadius, hot(Part::Knob));
            // 8 つのハンドル。
            for (size_t i = 0; i < kHandleAt.size(); ++i) {
                const Vec2 h = BoxPoint(*n, toDc, kHandleAt[i].first, kHandleAt[i].second);
                ui::Path sq;
                sq.Begin({h.x - kHandle, h.y - kHandle});
                sq.Line({h.x + kHandle, h.y - kHandle});
                sq.Line({h.x + kHandle, h.y + kHandle});
                sq.Line({h.x - kHandle, h.y + kHandle});
                p.FillPath(sq, hot(Part::Handle, static_cast<UiHandle>(i)));
            }
            // 中心 (回る所で、位置 を測る基準): 輪。
            const Vec2 pv = PivotOnPanel(*n, toDc);
            p.FillCircle(pv.x, pv.y, 5, hot(Part::Pivot));
            p.FillCircle(pv.x, pv.y, 2.5f, theme::SceneBackground());
        }
        // そのアンカー: 親の中で固定される所 (各アンカーの角を指す小さな三角)。
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
        // アンカーの点から 中心 (位置 を測る基準) への線。
        const Vec2 from = at(mn.x + (mx.x - mn.x) * n->place->pivot.x, mn.y + (mx.y - mn.y) * n->place->pivot.y);
        const Vec2 to = PivotOnPanel(*n, toDc);
        p.Line(from.x, from.y, to.x, to.y, col.Alpha(0.5f), 1.f);
    }

    enum class Drag { None, Press, Move, Resize, Rotate, Pivot, Pan, Marquee, PathPoint, Cancelled };

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
    std::vector<Edited> edits_;            // ドラッグ (または整列) が変えるもの
    int pathEntity_ = 0, pathPoint_ = -1;  // 点をドラッグしている / 選んでいる 道 (点を消す)
    std::vector<Vec2> pathStart_, pathLast_;
    Affine pathToDc_;
    std::vector<float> targetsX_, targetsY_;  // 吸着してよい先 (FindTargets)
    std::pair<size_t, size_t> history_{0, 0};
    std::optional<float> snapX_, snapY_;  // ドラッグが吸着した線 (パネル上)
    std::optional<UiBox> marquee_;        // 選ぶためにドラッグしている枠 (パネル上)
    std::vector<int> marqueeFrom_;        // その前に選んであったもの (Shift / Ctrl: 残す)
    std::string gridText_;                // 打っている間の格子の間隔
    int editCanvas_ = 0;                  // 作っているキャンバス (0: 全部)
    w::Popup canvasMenu_, anchorMenu_;
    int dropOn_ = -1;                     // Ctrl で落とした先の新しい親 (ノードの添字)、-1: なし
    int lastClicked_ = 0;                 // (ダブルクリック)
    double lastClickAt_ = -1;
    int textEntity_ = 0;                  // パネル上で打っている 文字 (0: なし)
    std::function<reflect::EditTarget()> textResolve_;
    std::string textBefore_, textNow_;
    bool textStarted_ = false;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateUiView() { return std::make_unique<UiView>(); }

}  // namespace rm::game_ui
