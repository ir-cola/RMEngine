#include "rm_engine/game_x_2d/editor/SceneView2D.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/app/Worker.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Gfx.h"
#include "platform/Image.h"
#include "rm_engine/game_x_2d/editor/SceneViewState2D.h"
#include "rm_engine/game_x_2d/runtime/BakedImage.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/editor/Gizmo2D.h"
#include "rm_engine/space2d/runtime/Atlas.h"
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
#include <utility>

namespace rm::game_x_2d {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
namespace gizmo = space2d::gizmo2d;
using editor::Tr;
using editor::TrF;
using space2d::Mat3;
using space2d::Transform2D;
using space2d::operator+;
using space2d::operator-;
using space2d::operator*;
using ui::Rect;

constexpr float kClickSlop = 4.f;     // a press that moves less than this is a click
constexpr float kMarkRadius = 7.f;    // the ring of an entity without a sprite (DIPs)
constexpr float kBarH = 28.f;         // the tool strip at the top left
constexpr float kToolW = 196.f, kSpaceW = 168.f, kPivotW = 150.f, kSnapW = 64.f, kGridW = 56.f, kDebugW = 84.f, kBarGap = 8.f;
constexpr float kSnapBoxW = 280.f, kSnapRowH = 30.f;  // the box of the steps (under 刻み)
constexpr double kImageCheckSeconds = 1.0;    // how often an image file is looked at again (changed on disk?)
constexpr double kImageForgetSeconds = 10.0;  // an image no entity uses for this long is let go

struct BarLayout {
    Rect tool, space, pivot, snap, grid, debug, all;
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
    std::optional<Camera2D> camera;  // it is a camera (カメラ (2D))
};

// The largest rectangle of `aspect` (width / height) in the middle of r (the game screen's shape).
Rect Fit(const Rect& r, float aspect) {
    const float a = std::max(aspect, 1e-3f);
    float w = r.w, h = r.w / a;
    if (h > r.h) {
        h = r.h;
        w = h * a;
    }
    return {r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h};
}

// An image file read once by the asset's id (several entities with the same image share it). Looked at
// again now and then: a file changed on disk is read again; a file that cannot be read is told once.
struct ImageEntry {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    uint32_t width = 0, height = 0;  // the file's pixels (the bitmap may be smaller: kMaxImageSize)
    bool sharp = false;  // the image's import setting くっきり (with the sprite's own)
    bool looked = false, loaded = false, told = false;
    std::filesystem::file_time_type time{};
    double checkedAt = 0, usedAt = 0;
    // Read on a thread of its own (the file and WIC); only the bitmap is made on the UI thread, when
    // it arrives. Dropping the entry drops the worker and what it would still bring.
    std::unique_ptr<editor::Worker> worker;
    bool reading = false;
    struct Arrived {
        std::optional<platform::Image> image;  // nullopt: `error` says why
        std::string error;
        uint32_t width = 0, height = 0;
        bool sharp = false;
    };
    std::optional<Arrived> arrived;
};

// One thing being dragged: as it was when the drag began, and as the drag last left it.
struct Edited {
    Transform2D start;
    Transform2D last;
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

Rect Normalized(Vec2 a, Vec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)}; }

class SceneView2D final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 8 || r.h < 8) return;
        // The GPU's devices were made again (platform::Gfx::Generation): Direct2D's things go - the
        // renderer's brush and effect, the images' bitmaps (read again when needed).
        if (const uint64_t now = host.host.Graphics().Generation(); now != generation_) {
            generation_ = now;
            renderer_ = {};
            images_.clear();
        }
        Begin(c, host);
        if (host.game) {  // the game view: only the picture, through the game's camera
            DrawGame(c, r, host);
            return;
        }
        c.P().PushClip(r);
        const std::optional<BarLayout> bar = Bar(r);
        if (!bar) snapOpen_ = false;
        if (HandleInput(c, r, host, bar)) Place(c, host);  // a drag moved things this frame
        Render(c, r, host, state_.camera, theme::SceneBackground());
        DrawGizmo(c, r, host);
        if (drag_ == Drag::Band) w::DrawSelectionBand(c, Normalized(press_, {c.In().mouseX, c.In().mouseY}).Intersect(r));
        DrawHints(c, r, bar);
        c.P().PopClip();
        if (bar) DrawBar(c, *bar);
        if (bar && snapOpen_) DrawSnapBox(c, SnapBox(r, *bar));
    }

    // In a 3D game: the 2D picture over the 3D one, through a カメラ (2D) with 3D の上に重ねる - the
    // 最初のカメラ when it is one, else the first active one in the 階層. Nothing without one.
    void DrawLayer(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 8 || r.h < 8) return;
        Begin(c, host);
        const Placed* cam = nullptr;
        auto usable = [](const Placed& p) { return p.camera && p.camera->overlay && p.active; };
        if (const Placed* chosen = host.camera != 0 ? FindPlaced(static_cast<int>(host.camera)) : nullptr; chosen && usable(*chosen)) {
            cam = chosen;
        }
        for (const Placed& p : placed_) {
            if (!cam && usable(p)) cam = &p;
        }
        if (!cam) return;
        const Rect at = Fit(r, host.aspect);
        c.P().PushClip(at);
        Render(c, at, host, ViewOf(*cam->camera, cam->world, at.h), Color{0, 0, 0, 0});
        c.P().PopClip();
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
    void Begin(ui::Context& c, game_production::SceneViewHost& host) {
        transformType_ = reflect::TypeOf<Transform2D>().name;
        spriteType_ = reflect::TypeOf<Sprite>().name;
        cameraType_ = reflect::TypeOf<Camera2D>().name;
        now_ = c.Time();
        Place(c, host);
        ForgetUnusedImages();
    }

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
            if (const game_production::Component* cc = e.FindComponent(cameraType_); cc && cc->Known()) {
                p.camera = *static_cast<const Camera2D*>(cc->value.Get());
            }
            if (const game_production::Component* sc = e.FindComponent(spriteType_); sc && sc->Known()) {
                const auto& sp = *static_cast<const Sprite*>(sc->value.Get());
                p.sprite = true;
                p.subOrder = sp.order;
                p.draw.world = p.world;
                p.draw.color = sp.color;
                p.draw.flipX = sp.flipX;
                p.draw.flipY = sp.flipY;
                const ImageEntry* img = Image(c, sp.image, host);
                p.draw.crisp = sp.crisp || (img && img->sharp);  // the sprite's own, or the image's import setting
                if (img) {
                    p.draw.image = img->bitmap.Get();
                    // The cell of the atlas, in the bitmap's pixels; its size in the plane from the file's
                    // pixels (a bitmap scaled down to fit keeps the sprite's size).
                    const D2D1_SIZE_U px = img->bitmap->GetPixelSize();
                    p.draw.part = space2d::CellOf(px.width, px.height, sp.columns, sp.rows, sp.cell);
                    const space2d::PixelRect file = space2d::CellOf(img->width, img->height, sp.columns, sp.rows, sp.cell);
                    const float ppu = std::max(sp.pixelsPerUnit, 0.01f);
                    p.draw.size = {static_cast<float>(file.w) / ppu, static_cast<float>(file.h) / ppu};
                }
            }
            placed_.push_back(p);
        }
    }

    // The image `ref` names (wherever it is now), read the first time it is asked for; null: a square.
    // Read by the core's one way to read pictures (platform/Image: WIC), made a Direct2D bitmap.
    const ImageEntry* Image(ui::Context& c, const AssetRef& ref, game_production::SceneViewHost& host) {
        if (ref.Empty()) return nullptr;
        ImageEntry& m = images_[ref.guid];
        m.usedAt = now_;
        if (m.arrived) {
            // Read on the worker: the bitmap is made here (Direct2D belongs to the UI thread).
            const bool again = m.bitmap != nullptr;
            ImageEntry::Arrived got = std::move(*m.arrived);
            m.arrived.reset();
            m.reading = false;
            const std::string name = PathToUtf8(host.host.AssetFile(ref).filename());
            m.bitmap = got.image ? platform::BitmapFromImage(c.P().DC(), *got.image) : nullptr;
            if (got.image && !m.bitmap) got.error = "Direct2D のビットマップにできませんでした";
            if (m.bitmap) {
                m.width = got.width;
                m.height = got.height;
                m.sharp = got.sharp;
                log::Info("2D のシーンビュー: 画像「{}」を{}読みました ({} x {} ピクセル)", name, again ? "もう一度" : "", m.width, m.height);
            } else {
                log::Warn("2D のシーンビュー: 画像「{}」を読めないので四角で描きます: {}", name, got.error);
            }
        }
        if (m.reading) c.RequestAnimation();  // drawn again until the picture being read arrives
        if (m.looked && now_ - m.checkedAt < kImageCheckSeconds) return m.bitmap ? &m : nullptr;
        m.looked = true;
        m.checkedAt = now_;
        const std::filesystem::path file = host.host.AssetFile(ref);
        const std::string name = file.empty() ? ref.guid : PathToUtf8(file.filename());
        std::error_code ec;
        // (the picture or its import settings in the .meta)
        const auto time = file.empty() ? std::filesystem::file_time_type{} : asset::ChangedAt(file, ec);
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
        if (m.loaded && time == m.time) return m.bitmap ? &m : nullptr;  // not changed
        m.loaded = true;
        m.time = time;
        // To the worker; the old bitmap stays until the new picture is here.
        if (!m.worker) m.worker = std::make_unique<editor::Worker>();
        m.reading = true;
        ImageEntry* entry = &m;  // (the entry outlives its worker's posts: they are dropped with it)
        m.worker->Run([file, entry](std::stop_token stop, const editor::Worker::Post& post) {
            ImageEntry::Arrived got;
            // The image's import settings (its .meta): くっきり, and how big it may be.
            const auto imp = asset::ReadImport<asset::ImageImport>(file);
            got.sharp = imp.sharp;
            const uint32_t most = std::min<uint32_t>(kMaxImageSize, static_cast<uint32_t>(std::max(imp.maxSize, 1)));
            IWICImagingFactory* wic = platform::ThreadImagingFactory();
            // (the file's own size first: a picture bigger than a bitmap is scaled down, the sprite keeps its size)
            auto whole = platform::LoadImageFile(wic, file, platform::PixelOrder::PremultipliedBgra, {}, &got.error);
            if (whole) {
                got.width = whole->width;
                got.height = whole->height;
                const bool big = whole->width > most || whole->height > most;
                if (big && !stop.stop_requested()) {
                    auto fitted = platform::LoadImageFile(wic, file, platform::PixelOrder::PremultipliedBgra, {most, most}, &got.error);
                    if (fitted) whole = std::move(fitted);
                }
                got.image = std::move(whole);
            }
            if (stop.stop_requested()) return;
            post([entry, got = std::move(got)]() mutable { entry->arrived = std::move(got); });
        });
        return m.bitmap ? &m : nullptr;
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

    // The gizmo's axes come from the last selected entity that has a place (the one clicked last).
    const Placed* Primary(std::span<const int> selection) const {
        for (auto it = selection.rbegin(); it != selection.rend(); ++it) {
            if (const Placed* p = FindPlaced(*it)) return p;
        }
        return nullptr;
    }

    // Where the gizmo is: on the one selected last (それぞれ), or in the middle of every selected thing
    // that moves - a child of a selected parent not counted - with the last one's axes (中心).
    std::optional<gizmo::Frame> GizmoFrame(const game_production::SceneViewHost& host, gizmo::Tool tool) const {
        const Placed* p = Primary(host.selection);
        if (!p) return std::nullopt;
        const gizmo::Frame own = gizmo::MakeFrame(p->world, tool, state_.space);
        if (state_.pivot == gizmo::Pivot::Each) return own;
        std::vector<Vec2> places;
        for (const int id : host.scene.Roots(host.selection)) {
            if (const Placed* q = FindPlaced(id)) places.push_back(space2d::TransformPoint(q->world, {0, 0}));
        }
        return gizmo::CenterFrame(places, own);
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

    // Where a thing is on the screen: the box around its sprite's corners, or around its ring.
    Rect ScreenBounds(const Placed& p, const Rect& r) const {
        const space2d::ScreenRect sr = ToScreenRect(r);
        if (!p.sprite) {
            const Vec2 at = space2d::ToScreen(state_.camera, sr, space2d::TransformPoint(p.world, {0, 0}));
            return {at.x - kMarkRadius, at.y - kMarkRadius, kMarkRadius * 2, kMarkRadius * 2};
        }
        const Vec2 half = p.draw.size * 0.5f;
        Vec2 k[4];
        space2d::PlacedBoxCorners(p.world, {-half.x, -half.y}, half, k);
        Vec2 mn = space2d::ToScreen(state_.camera, sr, k[0]), mx = mn;
        for (const Vec2& q : k) {
            const Vec2 s = space2d::ToScreen(state_.camera, sr, q);
            mn = {std::min(mn.x, s.x), std::min(mn.y, s.y)};
            mx = {std::max(mx.x, s.x), std::max(mx.y, s.y)};
        }
        return {mn.x, mn.y, mx.x - mn.x, mx.y - mn.y};
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

    // --- The tool strip and the box of the steps -------------------------------------------------
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
        b.pivot = place(kPivotW);
        b.snap = place(kSnapW);
        b.grid = place(kGridW);
        b.debug = place(kDebugW);
        const float right = std::max({b.tool.Right(), b.space.Right(), b.pivot.Right(), b.snap.Right(), b.grid.Right(), b.debug.Right()});
        b.all = {r.x + 8, r.y + 8, right - (r.x + 8), b.debug.Bottom() - (r.y + 8)};
        return b;
    }

    // The box of the steps, under 刻み (kept inside the view).
    static Rect SnapBox(const Rect& r, const BarLayout& b) {
        const float h = 10 + 5 * kSnapRowH + 6;
        return {std::max(r.x + 8, std::min(b.snap.x, r.Right() - 8 - kSnapBoxW)), b.snap.Bottom() + 4, kSnapBoxW, h};
    }

    bool OverBar(const Rect& r, const std::optional<BarLayout>& bar, float x, float y) const {
        if (!bar) return false;
        for (const Rect& part : {bar->tool, bar->space, bar->pivot, bar->snap, bar->grid, bar->debug}) {
            if (part.Contains(x, y)) return true;
        }
        return snapOpen_ && SnapBox(r, *bar).Contains(x, y);
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
        const std::wstring each = Tr(L"それぞれ"), center = Tr(L"中心");
        const std::wstring eachTip = Tr(L"選んだ物をそれぞれの場所で回す・大きさを変える。ギズモは最後に選んだ物に (Z で切り替え)");
        const std::wstring centerTip = Tr(L"選んだ物全部の真ん中にギズモを出し、そこを中心に回す・大きさを変える (Z で切り替え)");
        const std::array<std::wstring_view, 2> pivots{each, center};
        const std::array<std::wstring_view, 2> pivotTips{eachTip, centerTip};
        if (const int i = w::Segmented(c, "gizmo2d_pivot", bar.pivot, pivots, static_cast<int>(state_.pivot), pivotTips); i >= 0) {
            state_.pivot = static_cast<gizmo::Pivot>(i);
        }
        // 刻み: opens the box of the steps (in the accent while drags always go in steps).
        if (w::Button(c, "gizmo2d_snap", bar.snap, Tr(L"刻み"), state_.snapAlways ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            snapOpen_ = !snapOpen_;
        }
        if (!snapOpen_ && c.IsHovered(bar.snap)) w::Tooltip(c, Tr(L"ドラッグの刻む幅 (Ctrl を押しながらドラッグすると刻んで動く)"));
        if (w::Button(c, "scene2d_grid", bar.grid, Tr(L"格子"), state_.grid ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.grid = !state_.grid;
        }
        if (c.IsHovered(bar.grid)) w::Tooltip(c, Tr(L"格子を出す・消す"));
        // デバッグ: the lines the game's scripts draw while it plays (rm::Debug), in this view.
        if (w::Button(c, "scene2d_debug_draw", bar.debug, Tr(L"デバッグ"), state_.debugDraw ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.debugDraw = !state_.debugDraw;
        }
        if (c.IsHovered(bar.debug)) w::Tooltip(c, Tr(L"Play 中にスクリプトが rm::Debug で描く線を、このシーンビューに出す (押すと切り替え。ゲームビューにはいつも出る)"));
    }

    // The steps: how far a move, a turn and a scale go at a time; and whether always. View state like the
    // tool (kept in UserSettings, not an edit of the project).
    void DrawSnapBox(ui::Context& c, const Rect& box) {
        w::DrawSurface(c, box, w::Surface::Popup);
        ui::TextStyle label;
        label.size = theme::FontSize;
        label.color = theme::TextDim;
        label.v = ui::VAlign::Center;
        float y = box.y + 10;
        w::Checkbox(c, "snap2d_grid", {box.x + 12, y + 2, box.w - 24, 24}, state_.snapToGrid, Tr(L"移動は格子の目に合わせる (近づくと細かく)"));
        y += kSnapRowH;
        struct Row {
            const char* id;
            std::wstring name;
            float* value;
            float min, max;
        };
        Row rows[] = {{"snap2d_move", Tr(L"移動"), &state_.snapMove, 0.001f, 100000.f},
                      {"snap2d_degrees", Tr(L"回転 (度)"), &state_.snapDegrees, 0.1f, 180.f},
                      {"snap2d_scale", Tr(L"大きさ"), &state_.snapScale, 0.001f, 10.f}};
        for (size_t i = 0; i < std::size(rows); ++i, y += kSnapRowH) {
            Row& row = rows[i];
            const bool off = i == 0 && state_.snapToGrid;  // the grid's step is used instead
            ui::TextStyle l = label;
            if (off) l.color = theme::TextFaint;
            c.P().Text({box.x + 12, y, 140, kSnapRowH - 6}, row.name, l);
            w::NumberField(c, row.id, {box.Right() - 12 - 110, y, 110, kSnapRowH - 6}, *row.value, row.min, row.max, snapText_[i]);
        }
        w::Checkbox(c, "snap2d_always", {box.x + 12, y + 2, box.w - 24, 24}, state_.snapAlways, Tr(L"いつも刻む (Ctrl を押すとなめらか)"));
    }

    // --- Input ---------------------------------------------------------------------------------
    // Returns true when a drag changed the scene this frame.
    bool HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const std::optional<BarLayout>& bar) {
        const ui::Input& in = c.In();
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
        const Vec2 mouse{in.mouseX, in.mouseY};
        const space2d::ScreenRect sr = ToScreenRect(r);
        bool changed = false;

        // In Play the scene is the copy that plays: shown, not edited (no gizmo, no dragging things).
        const std::optional<gizmo::Frame> frame = host.playing ? std::nullopt : GizmoFrame(host, state_.tool);
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
                // 中心: turns and scales go about the gizmo's middle (a move is the same either way).
                pivot_ = state_.pivot == gizmo::Pivot::Center ? std::optional<gizmo::Frame>(*frame) : std::nullopt;
                drag_ = Drag::Gizmo;
            } else if (c.MousePressed()) {
                drag_ = Drag::Press;
                press_ = mouse;
                pressed_ = host.playing ? 0 : PickAt(r, mouse);
            }
        } else if (drag_ != Drag::None) {
            // Undo / redo (Ctrl+Z) while the button is still held: the scene may no longer be what the drag
            // started from (the thing gone, its Transform 2D back) - the drag stops (nothing is pushed).
            if ((drag_ == Drag::Gizmo || drag_ == Drag::Body) && HistoryMoved(host)) {
                AbandonEdit();
                drag_ = Drag::Cancelled;
            }
            const bool held = drag_ == Drag::PanRight ? in.down[1] : drag_ == Drag::PanMiddle ? in.down[2] : in.down[0];
            if (!held) {
                if (drag_ == Drag::Press) Click(host, r, in.ctrl);
                if (drag_ == Drag::Band) SelectInBand(host, r, Normalized(press_, mouse), in.ctrl);
                if (drag_ == Drag::Gizmo || drag_ == Drag::Body) EndEdit(host);
                drag_ = Drag::None;
            } else if (drag_ == Drag::PanRight || drag_ == Drag::PanMiddle) {
                space2d::Pan(state_.camera, in.mouseDX, in.mouseDY);
            } else if (drag_ == Drag::Band) {
                if (c.KeyPressed(ui::Key::Escape)) drag_ = Drag::Cancelled;
            } else if (drag_ == Drag::Press && space2d::Length(mouse - press_) >= kClickSlop) {
                if (pressed_ == 0) {
                    drag_ = Drag::Band;  // from an empty place: a rectangle selects what it touches
                } else {
                    // A drag that started on a thing moves it (with the rest of the selection, when it is
                    // selected; alone - and selected now - when it is not).
                    // The thing pressed is looked for again: it may have gone since (Ctrl+Z with the button held).
                    const Placed* grabbed = FindPlaced(pressed_);
                    const bool inSelection = std::find(host.selection.begin(), host.selection.end(), pressed_) != host.selection.end();
                    std::vector<int> ids = inSelection ? host.scene.Roots(host.selection) : std::vector<int>{pressed_};
                    if (grabbed && !inSelection) host.select({pressed_});
                    if (grabbed && BeginEdit(host, ids)) {
                        gizmo::Frame f;
                        f.origin = space2d::TransformPoint(grabbed->world, {0, 0});
                        gizmo_ = gizmo::BeginDrag(gizmo::Tool::Move, gizmo::Handle::Free, f, state_.camera, sr, press_);
                        pivot_.reset();
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
                    // Ctrl: in steps - or smoothly, when it is always in steps.
                    gizmo::Snap snap;
                    snap.on = in.ctrl != state_.snapAlways;
                    snap.move = state_.snapToGrid ? space2d::GridFor(state_.camera.zoom).step : state_.snapMove;
                    snap.degrees = state_.snapDegrees;
                    snap.scale = state_.snapScale;
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
            if (c.KeyPressed(ui::Key::Z)) state_.pivot = state_.pivot == gizmo::Pivot::Each ? gizmo::Pivot::Center : gizmo::Pivot::Each;
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

    // The rectangle dragged out: everything it touches (in the 階層's order); Ctrl adds to the selection.
    void SelectInBand(game_production::SceneViewHost& host, const Rect& r, const Rect& band, bool ctrl) {
        std::vector<int> sel = ctrl ? std::vector<int>(host.selection.begin(), host.selection.end()) : std::vector<int>{};
        for (const Placed& p : placed_) {
            const Rect b = ScreenBounds(p, r);
            const bool touches = b.x <= band.Right() && b.Right() >= band.x && b.y <= band.Bottom() && b.Bottom() >= band.y;
            if (touches && std::find(sel.begin(), sel.end(), p.id) == sel.end()) sel.push_back(p.id);
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
            const Transform2D now = *static_cast<const Transform2D*>(t.object);
            edited_.push_back({now, now, p->parentWorld, p->world, std::move(resolve)});
        }
        history_ = {host.host.History().UndoCount(), host.host.History().RedoCount()};
        return !edited_.empty();
    }

    // The history changed since the drag began (an undo / redo, or an edit from elsewhere).
    bool HistoryMoved(game_production::SceneViewHost& host) const {
        return history_ != std::pair{host.host.History().UndoCount(), host.host.History().RedoCount()};
    }

    // Stops a drag after the history moved under it: each field the drag changed - and nothing else
    // changed since - goes back to where it started (what the undo / redo set stays). Nothing is pushed.
    void AbandonEdit() {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (!t.object) continue;  // gone
            auto& tr = *static_cast<Transform2D*>(t.object);
            if (tr.position == e.last.position) tr.position = e.start.position;
            if (tr.rotation == e.last.rotation) tr.rotation = e.start.rotation;
            if (tr.scale == e.last.scale) tr.scale = e.start.scale;
        }
        edited_.clear();
        gizmo_.reset();
        pivot_.reset();
        log::Info("ドラッグ中に元に戻す / やり直しがあったので、ドラッグをやめました");
    }

    // While dragging, the Transforms are changed directly (the inspector follows); the history gets
    // one entry when the button is let go.
    void ApplyEdit(const gizmo::Change& change) {
        for (Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (t.object) {
                e.last = gizmo::Apply(e.start, e.parentWorld, e.startWorld, change, pivot_ ? &*pivot_ : nullptr);
                *static_cast<Transform2D*>(t.object) = e.last;
            }
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
        pivot_.reset();
    }

    // --- The game view -------------------------------------------------------------------------
    // Through the game settings' 最初のカメラ (a カメラ (2D) on an entity with a Transform 2D), in the game
    // screen's shape; what is missing when it cannot (and the way to the settings).
    void DrawGame(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) {
        std::wstring problem;
        const game_production::SceneEntity* e = host.camera != 0 ? host.scene.Find(static_cast<int>(host.camera)) : nullptr;
        const Placed* cam = e ? FindPlaced(e->data.id) : nullptr;
        if (host.camera == 0) {
            problem = Tr(L"ゲームの設定の「最初のカメラ」が選ばれていません。カメラ (2D) の部品を付けたエンティティを選ぶと、そこから見た絵がここに映ります。");
        } else if (!e) {
            problem = Tr(L"「最初のカメラ」のエンティティがシーンにありません (消されたかもしれません)。ゲームの設定で選び直してください。");
        } else if (!cam || !cam->camera) {
            problem = TrF(L"「{}」にカメラ (2D) の部品がありません (Transform 2D も要ります)。インスペクターの「部品を足す」で付けてください。",
                          Utf8ToWide(e->data.name));
        } else if (!cam->active) {
            problem = TrF(L"カメラの「{}」が有効ではありません。有効にすると、そこから見た絵が映ります。", Utf8ToWide(e->data.name));
        }
        if (!problem.empty()) {
            const float cy = r.y + r.h * 0.5f;
            ui::TextStyle ts;
            ts.size = 13;
            ts.wrap = true;
            ts.color = theme::TextDim;
            ts.h = ui::HAlign::Center;
            ts.v = ui::VAlign::Bottom;
            c.P().Text({r.x + 24, r.y + 8, r.w - 48, cy - r.y}, problem, ts);
            const Rect button{r.x + (r.w - 200) * 0.5f, cy + 16, 200, 36};
            if (host.openSettings && w::Button(c, "game_camera_settings", button, Tr(L"ゲームの設定を開く"))) host.openSettings();
            return;
        }
        const Rect at = Fit(r, host.aspect);
        c.P().PushClip(at);
        Render(c, at, host, ViewOf(*cam->camera, cam->world, at.h), BackgroundOf(*cam->camera, host.background));
        c.P().PopClip();
    }

    // --- Drawing -------------------------------------------------------------------------------
    // `clear` alpha 0: a layer over another space's picture (nothing filled).
    void Render(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const space2d::Camera2D& view, Color clear) {
        const bool game = host.game;
        const space2d::ScreenRect sr = ToScreenRect(r);
        lines_.clear();
        overlay_.clear();
        sprites_.clear();
        if (!game && state_.grid) Grid(sr);
        if (!game) {
            // The axes through the origin: X red, Y green (as the gizmo's arrows).
            const Vec2 a = space2d::ToWorld(view, sr, {r.x, r.Bottom()}), b = space2d::ToWorld(view, sr, {r.Right(), r.y});
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
                if (p.camera) CameraFrame(p, host.aspect, selected ? theme::Accent : theme::SceneCamera());
                if (p.sprite) {
                    if (selected) Outline(p, theme::Accent);
                } else {
                    Mark(p, selected ? theme::Accent : p.active ? theme::SceneObject() : theme::SceneObjectInactive());
                }
            }
        }
        space2d::RenderFrame2D f;
        f.viewport = sr;
        f.camera = view;
        f.clear = clear;
        f.lines = lines_;
        f.sprites = sprites_;
        f.overlay = overlay_;
        // Play: the lines the scripts drew with rm::Debug in 2D (this scene view only while デバッグ is on).
        debug_.clear();
        if (host.debug && (game || state_.debugDraw)) {
            for (const game_production::DebugLine& l : host.debug->Lines()) {
                if (l.space == game_production::DebugSpace::Space2D) debug_.push_back({{l.a.x, l.a.y}, {l.b.x, l.b.y}, l.color, 1.5f});
            }
        }
        f.debug = debug_;
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

    // What a camera sees (the game screen's shape about its place, turned with it), and a small mark on
    // its top edge (which way is up).
    void CameraFrame(const Placed& p, float aspect, ui::Color col) {
        const Vec2 half = HalfExtent(*p.camera, aspect);
        const space2d::WorldPlacement2D at = space2d::Decompose(p.world);
        const Mat3 frame = space2d::Translation(at.position) * space2d::Rotation(at.rotation);
        Vec2 k[4];
        space2d::PlacedBoxCorners(frame, {-half.x, -half.y}, half, k);
        for (int i = 0; i < 4; ++i) overlay_.push_back({k[i], k[(i + 1) % 4], col, 1.5f});
        const float tick = half.y * 0.08f;
        const Vec2 a = space2d::TransformPoint(frame, {-tick, half.y}), top = space2d::TransformPoint(frame, {0, half.y + tick});
        const Vec2 b = space2d::TransformPoint(frame, {tick, half.y});
        overlay_.push_back({a, top, col, 1.5f});
        overlay_.push_back({top, b, col, 1.5f});
    }

    // A thing without a sprite: a small ring (the same size on the screen) with a tick along its X axis.
    void Mark(const Placed& p, ui::Color col) {
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
        if (host.playing || drag_ == Drag::Body || drag_ == Drag::Band) return;  // Play: not edited
        const gizmo::Tool tool = drag_ == Drag::Gizmo && gizmo_ ? gizmo_->tool : state_.tool;
        // While dragging, the gizmo follows the things (made again from where they are now).
        const std::optional<gizmo::Frame> shown = GizmoFrame(host, tool);
        if (!shown) return;
        const gizmo::ScreenShape s = gizmo::ShapeOf(*shown, state_.camera, ToScreenRect(r));
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
                   Tr(L"右 / 中ドラッグ: 平行移動   ホイール: 近づく   クリック: 選ぶ (Ctrl: 足す・外す)   何も無い所からドラッグ: 四角で囲んで選ぶ   物をドラッグ: 動かす   F: 選んだ物へ   W / E / R: 移動・回転・拡大縮小   X: ローカル / ワールド   Z: それぞれ / 中心   Ctrl+ドラッグ: 刻んで動かす   Esc: ドラッグをやめる"),
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

    enum class Drag { None, PanRight, PanMiddle, Press, Band, Body, Gizmo, Cancelled };

    SceneViewState2D state_;      // the camera, the tool, the steps, the grid (kept in UserSettings)
    reflect::Unknowns unknowns_;  // keys of a newer version: written back unchanged
    space2d::Renderer2D renderer_;
    uint64_t generation_ = 0;  // the GPU's devices renderer_ and images_ were made on
    std::string transformType_, spriteType_, cameraType_;
    std::vector<Placed> placed_;
    std::vector<space2d::Line2D> lines_, overlay_, debug_;
    std::vector<space2d::SpriteDraw> sprites_;
    std::unordered_map<std::string, ImageEntry> images_;  // by Sprite::image (the asset's id)
    double now_ = 0;

    Drag drag_ = Drag::None;
    Vec2 press_{0, 0};
    int pressed_ = 0;  // the entity under the mouse when the left button went down (0: none)
    gizmo::Handle hot_ = gizmo::Handle::None;
    std::optional<gizmo::Drag> gizmo_;
    std::optional<gizmo::Frame> pivot_;  // 中心: what the drag turns / scales about
    std::vector<Edited> edited_;
    std::pair<size_t, size_t> history_{0, 0};  // the undo / redo counts of the history when the drag began
    bool snapOpen_ = false;              // the box of the steps is open
    std::array<std::string, 3> snapText_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateSceneView2D() { return std::make_unique<SceneView2D>(); }

}  // namespace rm::game_x_2d
