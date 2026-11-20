#include "rm_engine/game_x_2d/editor/SceneView2D.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/app/Worker.h"
#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/DebugPanel.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Gfx.h"
#include "platform/Image.h"
#include "rm_engine/game_x_2d/editor/SceneViewState2D.h"
#include "core/asset/BakedImage.h"
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

using asset::BakedImageInfo;
using asset::kMaxImageSize;
using asset::ReadBakedImage;
using asset::WriteBakedImage;

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

constexpr float kClickSlop = 4.f;     // これより小さくしか動かない押下はクリック
constexpr float kMarkRadius = 7.f;    // スプライトのないエンティティの輪 (DIP)
constexpr float kBarH = 28.f;         // 左上のツールの帯
constexpr float kToolW = 196.f, kSpaceW = 168.f, kPivotW = 150.f, kSnapW = 64.f, kGridW = 56.f, kDebugW = 84.f, kShapesW = 96.f, kBarGap = 8.f;
constexpr float kSnapBoxW = 280.f, kSnapRowH = 30.f;  // 刻みの箱 (刻みの下)
constexpr double kImageCheckSeconds = 1.0;    // 画像ファイルを見直す間隔 (ディスクで変わったか)
constexpr double kImageForgetSeconds = 10.0;  // どのエンティティも使わないままこの時間がたった画像は手放す

struct BarLayout {
    Rect tool, space, pivot, snap, grid, debug, shapes, all;
};

// Transform 2D を持つエンティティの、このフレームでの状態。
struct Placed {
    int id = 0;
    Mat3 world, parentWorld;
    int order = 0;        // ワールドでの順番 (親の分を足したもの)
    size_t tree = 0;      // 階層での位置 (後ろほど手前)
    bool active = true;
    bool sprite = false;  // スプライトがある (なければ小さな輪で描く。ゲームには出ない)
    space2d::SpriteDraw draw;
    int subOrder = 0;     // the sprite's 並び順
    std::optional<Camera2D> camera;  // カメラである (カメラ (2D))
};

// r の真ん中にある、`aspect` (幅 / 高さ) の一番大きい長方形 (ゲーム画面の形)。
Rect Fit(const Rect& r, float aspect) {
    const float a = std::max(aspect, 1e-3f);
    float w = r.w, h = r.w / a;
    if (h > r.h) {
        h = r.h;
        w = h * a;
    }
    return {r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h};
}

// アセットの id で一度だけ読む画像ファイル (同じ画像のエンティティが
// いくつあっても共有する)。ときどき見直す: ディスクで変わったファイルは
// 読み直し、読めないファイルは一度だけ知らせる。
struct ImageEntry {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    uint32_t width = 0, height = 0;  // ファイルのピクセル数 (ビットマップはもっと小さいことがある: kMaxImageSize)
    bool sharp = false;  // 画像の取り込みの設定の くっきり (スプライト自身の設定とあわせる)
    bool looked = false, loaded = false, told = false;
    std::filesystem::file_time_type time{};
    double checkedAt = 0, usedAt = 0;
    // 専用のスレッドで読む (ファイルと WIC)。ビットマップだけは、届いたときに
    // UI スレッドで作る。エントリを捨てると、ワーカーとこれから届く分も捨てる。
    std::unique_ptr<editor::Worker> worker;
    bool reading = false;
    struct Arrived {
        std::optional<platform::Image> image;  // nullopt: 理由は `error` にある
        std::string error;
        uint32_t width = 0, height = 0;
        bool sharp = false;
    };
    std::optional<Arrived> arrived;
};

// ドラッグされている 1 つ分: ドラッグを始めたときの状態と、最後にドラッグが残した状態。
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
        DropOldGpuThings(host);
        Begin(c, host);
        if (host.game) {  // ゲームビュー: 絵だけを、ゲームのカメラ越しに描く
            DrawGame(c, r, host);
            return;
        }
        c.P().PushClip(r);
        const std::optional<BarLayout> bar = Bar(r);
        if (!bar) snapOpen_ = false;
        if (HandleInput(c, r, host, bar)) Place(c, host);  // このフレームのドラッグで物が動いた
        Render(c, r, host, state_.camera, theme::SceneBackground());
        DrawGizmo(c, r, host);
        if (drag_ == Drag::Band) w::DrawSelectionBand(c, Normalized(press_, {c.In().mouseX, c.In().mouseY}).Intersect(r));
        DrawHints(c, r, bar);
        c.P().PopClip();
        if (bar) DrawBar(c, *bar);
        if (bar && snapOpen_) DrawSnapBox(c, SnapBox(r, *bar));
    }

    // 3D のゲームでは、3D の絵の上に 2D の絵を、「3D の上に重ねる」にした カメラ (2D) 越しに
    // 描く。使うのは 最初のカメラ がそれならそれ、そうでなければ階層で最初の有効なもの。
    // どちらもなければ何も描かない。
    void DrawLayer(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 8 || r.h < 8) return;
        DropOldGpuThings(host);  // (3D のゲームの 2D の層はここでだけ描く)
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

    // GPU のデバイスが作り直された (platform::Gfx::Generation): Direct2D の物は捨てる。
    // レンダラーのブラシとエフェクト、画像のビットマップ (必要になったら読み直す)。
    void DropOldGpuThings(game_production::SceneViewHost& host) {
        if (const uint64_t now = host.host.Graphics().Generation(); now != generation_) {
            generation_ = now;
            renderer_ = {};
            images_.clear();
        }
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

    // --- 物の置き場所 ---------------------------------------------------------------------
    void Place(ui::Context& c, game_production::SceneViewHost& host) {
        placed_.clear();
        struct Parent {
            Mat3 world;
            int order = 0;
        };
        std::unordered_map<int, Parent> parents;  // 親は TreeOrder で子より先に来る
        size_t index = 0;
        for (const auto& t : host.scene.TreeOrder()) {
            const auto& e = *t.entity;
            const auto it = parents.find(e.data.parent);
            const Parent parent = it != parents.end() ? it->second : Parent{};
            const game_production::Component* comp = e.FindComponent(transformType_);
            if (!comp || !comp->Known()) {
                parents[e.data.id] = parent;  // 自分の場所を持たない: 子は親についていく
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
                p.draw.crisp = sp.crisp || (img && img->sharp);  // スプライト自身の設定、または画像の取り込みの設定
                if (img) {
                    p.draw.image = img->bitmap.Get();
                    // アトラスのセル (ビットマップのピクセル単位)。平面での大きさはファイルの
                    // ピクセル数から決める (縮小したビットマップでも、スプライトの大きさは変わらない)。
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

    // `ref` が指す画像 (今どこにあっても) を、最初に求められたときに読む。null: 四角。
    // 核の画像の読み方 (platform/Image: WIC) 1 本で読み、Direct2D のビットマップにする。
    const ImageEntry* Image(ui::Context& c, const AssetRef& ref, game_production::SceneViewHost& host) {
        if (ref.Empty()) return nullptr;
        ImageEntry& m = images_[ref.guid];
        m.usedAt = now_;
        if (m.arrived) {
            // ワーカーで読む: ビットマップはここで作る (Direct2D は UI スレッドのもの)。
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
        if (m.reading) c.RequestAnimation();  // 読んでいる絵が届くまで描き直し続ける
        if (m.looked && now_ - m.checkedAt < kImageCheckSeconds) return m.bitmap ? &m : nullptr;
        m.looked = true;
        m.checkedAt = now_;
        const std::filesystem::path file = host.host.AssetFile(ref);
        const std::string name = file.empty() ? ref.guid : PathToUtf8(file.filename());
        std::error_code ec;
        // (画像そのものか、.meta の取り込みの設定)
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
        if (m.loaded && time == m.time) return m.bitmap ? &m : nullptr;  // 変わっていない
        m.loaded = true;
        m.time = time;
        // ワーカーへ渡す。新しい絵が届くまで古いビットマップは残す。
        if (!m.worker) m.worker = std::make_unique<editor::Worker>();
        m.reading = true;
        ImageEntry* entry = &m;  // (エントリはワーカーの投稿より長く生きる: 投稿はエントリと一緒に捨てる)
        m.worker->Run([file, entry](std::stop_token stop, const editor::Worker::Post& post) {
            ImageEntry::Arrived got;
            // 画像の取り込みの設定 (.meta): くっきり と、どこまで大きくしてよいか。
            const auto imp = asset::ReadImport<asset::ImageImport>(file);
            got.sharp = imp.filter == asset::TextureFilter::Sharp;
            const uint32_t most = std::min<uint32_t>(kMaxImageSize, static_cast<uint32_t>(std::max(imp.maxSize, 1)));
            IWICImagingFactory* wic = platform::ThreadImagingFactory();
            // (まずファイル自身の大きさ。ビットマップより大きい絵は縮小するが、スプライトの大きさは変えない)
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

    // ギズモの軸は、場所を持つ最後に選んだエンティティ (最後にクリックしたもの) から取る。
    const Placed* Primary(std::span<const int> selection) const {
        for (auto it = selection.rbegin(); it != selection.rend(); ++it) {
            if (const Placed* p = FindPlaced(*it)) return p;
        }
        return nullptr;
    }

    // ギズモの場所: 最後に選んだ 1 つの上 (それぞれ)、または動く物すべての真ん中
    // (選んだ親の子は数えない)。軸は最後の 1 つのもの (中心)。
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

    // クリックが当たる物: スプライトの長方形、またはスプライトのない物の輪。
    bool Hits(const Placed& p, const Rect& r, Vec2 mouse) const {
        const space2d::ScreenRect sr = ToScreenRect(r);
        if (p.sprite) {
            const Vec2 half = p.draw.size * 0.5f;
            return space2d::InsidePlacedBox(space2d::ToWorld(state_.camera, sr, mouse), p.world, {-half.x, -half.y}, half);
        }
        const Vec2 at = space2d::ToScreen(state_.camera, sr, space2d::TransformPoint(p.world, {0, 0}));
        return space2d::Length(mouse - at) <= kMarkRadius + 3.f;
    }

    // 物が画面のどこにあるか: スプライトの角を囲む箱、または輪を囲む箱。
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

    // マウスの下の一番手前のもの (0: なし)。
    int PickAt(const Rect& r, Vec2 mouse) const {
        const auto order = DrawOrder();
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const Placed& p = placed_[*it];
            if (Hits(p, r, mouse)) return p.id;
        }
        return 0;
    }

    // 奥から手前へ。
    std::vector<size_t> DrawOrder() const {
        std::vector<space2d::DrawKey> keys;
        keys.reserve(placed_.size());
        for (const Placed& p : placed_) keys.push_back({p.order, p.subOrder, p.tree});
        return space2d::DrawOrder(keys);
    }

    // --- ツールの帯と刻みの箱 -------------------------------------------------------------
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
        b.shapes = place(kShapesW);
        const float right = std::max({b.tool.Right(), b.space.Right(), b.pivot.Right(), b.snap.Right(), b.grid.Right(), b.debug.Right(), b.shapes.Right()});
        b.all = {r.x + 8, r.y + 8, right - (r.x + 8), b.shapes.Bottom() - (r.y + 8)};
        return b;
    }

    // 刻みの箱。刻みの下に出す (ビューの中に収める)。
    static Rect SnapBox(const Rect& r, const BarLayout& b) {
        const float h = 10 + 5 * kSnapRowH + 6;
        return {std::max(r.x + 8, std::min(b.snap.x, r.Right() - 8 - kSnapBoxW)), b.snap.Bottom() + 4, kSnapBoxW, h};
    }

    bool OverBar(const Rect& r, const std::optional<BarLayout>& bar, float x, float y) const {
        if (!bar) return false;
        for (const Rect& part : {bar->tool, bar->space, bar->pivot, bar->snap, bar->grid, bar->debug, bar->shapes}) {
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
        // 刻み: 刻みの箱を開く (ドラッグがいつも刻みで動くときは、強調の色)。
        if (w::Button(c, "gizmo2d_snap", bar.snap, Tr(L"刻み"), state_.snapAlways ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            snapOpen_ = !snapOpen_;
        }
        if (!snapOpen_ && c.IsHovered(bar.snap)) w::Tooltip(c, Tr(L"ドラッグの刻む幅 (Ctrl を押しながらドラッグすると刻んで動く)"));
        if (w::Button(c, "scene2d_grid", bar.grid, Tr(L"格子"), state_.grid ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.grid = !state_.grid;
        }
        if (c.IsHovered(bar.grid)) w::Tooltip(c, Tr(L"格子を出す・消す"));
        // デバッグ: ゲームのスクリプトが Play 中に描く線 (rm::Debug) を、このビューに出す。
        if (w::Button(c, "scene2d_debug_draw", bar.debug, Tr(L"デバッグ"), state_.debugDraw ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.debugDraw = !state_.debugDraw;
        }
        if (c.IsHovered(bar.debug)) w::Tooltip(c, Tr(L"Play 中にスクリプトが rm::Debug で描く線を、このシーンビューに出す (押すと切り替え。ゲームビューにはいつも出る)"));
        // 形: プラグインがシーンの上に出す線 (物理: コライダー)。選んだ物の分か、すべての分。
        if (w::Button(c, "scene2d_shapes", bar.shapes, state_.allShapes ? Tr(L"形: 全部") : Tr(L"形: 選択"), w::ButtonKind::Secondary)) {
            state_.allShapes = !state_.allShapes;
        }
        if (c.IsHovered(bar.shapes)) w::Tooltip(c, Tr(L"コライダー (物理の当たりの形) などの線を、選んだ物だけに出すか、全部の物に出すか (押すと切り替え)"));
    }

    // 刻み: 移動・回転・拡大縮小を一度にどれだけ動かすか、それをいつもやるか。ツールと同じビューの
    // 状態 (UserSettings に保存する。プロジェクトの編集ではない)。
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
            const bool off = i == 0 && state_.snapToGrid;  // 代わりにグリッドの刻みを使う
            ui::TextStyle l = label;
            if (off) l.color = theme::TextFaint;
            c.P().Text({box.x + 12, y, 140, kSnapRowH - 6}, row.name, l);
            w::NumberField(c, row.id, {box.Right() - 12 - 110, y, 110, kSnapRowH - 6}, *row.value, row.min, row.max, snapText_[i]);
        }
        w::Checkbox(c, "snap2d_always", {box.x + 12, y + 2, box.w - 24, 24}, state_.snapAlways, Tr(L"いつも刻む (Ctrl を押すとなめらか)"));
    }

    // --- 入力 ---------------------------------------------------------------------------
    // このフレームのドラッグでシーンが変わったら true を返す。
    bool HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const std::optional<BarLayout>& bar) {
        const ui::Input& in = c.In();
        const bool overBar = OverBar(r, bar, in.mouseX, in.mouseY);
        bool hovered = c.IsHovered(r) && !overBar;
        // 刻みの箱は、外をクリックする (そのクリックは他に何もしない) か Esc で閉じる。
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

        // Play ではシーンは動いているコピー: 見せるだけで編集しない (ギズモもなく、物をドラッグできない)。
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
                // 中心: 回転と拡大縮小はギズモの真ん中を中心にする (移動はどちらでも同じ)。
                pivot_ = state_.pivot == gizmo::Pivot::Center ? std::optional<gizmo::Frame>(*frame) : std::nullopt;
                drag_ = Drag::Gizmo;
            } else if (c.MousePressed()) {
                drag_ = Drag::Press;
                press_ = mouse;
                pressed_ = host.playing ? 0 : PickAt(r, mouse);
            }
        } else if (drag_ != Drag::None) {
            // ボタンを押したまま Undo / Redo (Ctrl+Z) した: シーンがドラッグを始めたときの
            // ままとは限らない (物が消えた、Transform 2D が戻った) ので、ドラッグをやめる (何も積まない)。
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
                    drag_ = Drag::Band;  // 何もない所から: 四角で触れた物を選ぶ
                } else {
                    // 物の上で始めたドラッグはその物を動かす (選ばれているなら選択中の物みんなと一緒に、
                    // 選ばれていないなら、それだけを選んで動かす)。
                    // 押した物は探し直す: 消えているかもしれない (ボタンを押したまま Ctrl+Z した)。
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
                    CancelEdit();  // すべて元の場所に戻す。ボタンは離す必要がある
                    drag_ = Drag::Cancelled;
                } else {
                    // Ctrl: 刻みで動かす。いつも刻みのときは滑らかに動かす。
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

    // クリック (押した場所でボタンを離した): 下にある物を選ぶ。Ctrl で選択に足す / 外す。
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

    // ドラッグで作った四角: 触れた物すべて (階層の順)。Ctrl で選択に足す。
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

    // --- ドラッグで編集する ---------------------------------------------------------------
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

    // ドラッグを始めてから履歴が変わった (Undo / Redo、または他からの編集)。
    bool HistoryMoved(game_production::SceneViewHost& host) const {
        return history_ != std::pair{host.host.History().UndoCount(), host.host.History().RedoCount()};
    }

    // 履歴が動いたあとでドラッグをやめる: ドラッグが変えた各項目は、そのあとに他で
    // 変わっていなければ、始めの値に戻す (Undo / Redo が決めた値はそのまま)。何も積まない。
    void AbandonEdit() {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (!t.object) continue;  // 消えた
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

    // ドラッグ中は Transform を直接変える (インスペクターも追う)。履歴に積むのは
    // ボタンを離したときの 1 件。
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

    // 編集 = Undo の 1 件 = 未保存 (憲法 7): 何を変えても、ドラッグ全体で 1 件。
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
        if (!group->Empty()) host.host.History().Push(std::move(group));  // ドラッグ中にもう反映済み
        edited_.clear();
        gizmo_.reset();
        pivot_.reset();
    }

    // --- ゲームビュー --------------------------------------------------------------------
    // ゲーム設定の 最初のカメラ (Transform 2D のあるエンティティの カメラ (2D)) 越しに、
    // ゲーム画面の形で描く。できないときは足りない物と、設定への道を出す。
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

    // --- 描く ---------------------------------------------------------------------------
    // `clear` のアルファが 0: 別の空間の絵の上に重ねる層 (塗りつぶさない)。
    void Render(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const space2d::Camera2D& view, Color clear) {
        const bool game = host.game;
        const space2d::ScreenRect sr = ToScreenRect(r);
        lines_.clear();
        overlay_.clear();
        sprites_.clear();
        if (!game && state_.grid) Grid(sr);
        if (!game) {
            // 原点を通る軸: X は赤、Y は緑 (ギズモの矢印と同じ)。
            const Vec2 a = space2d::ToWorld(view, sr, {r.x, r.Bottom()}), b = space2d::ToWorld(view, sr, {r.Right(), r.y});
            lines_.push_back({{a.x, 0}, {b.x, 0}, theme::AxisX().Alpha(0.8f), 1.f});
            lines_.push_back({{0, a.y}, {0, b.y}, theme::AxisY().Alpha(0.8f), 1.f});
        }
        for (const size_t i : DrawOrder()) {
            const Placed& p = placed_[i];
            if (!p.sprite || (game && !p.active)) continue;  // オフ: ゲームには出ない
            space2d::SpriteDraw d = p.draw;
            if (!p.active) d.color = d.color.Alpha(d.color.a * 0.35f);  // オフ: シーンビューでは薄く
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
        // Play: スクリプトが 2D で rm::Debug で描いた線 (このシーンビューは デバッグ がオンのときだけ)。
        debug_.clear();
        if (host.debug && (game || state_.debugDraw)) {
            for (const game_production::DebugLine& l : host.debug->Lines()) {
                if (l.space == game_production::DebugSpace::Space2D) debug_.push_back({{l.a.x, l.a.y}, {l.b.x, l.b.y}, l.color, 1.5f});
            }
        }
        // プラグインがシーンの上に出す線 (物理: コライダーの形): シーンビューだけ。
        if (!game) {
            sceneLines_.Clear();
            for (const auto& l : host.host.Registry().Contributions<game_production::SceneLinesInfo>(game_production::kSceneLinesPoint)) {
                l.item->lines(host.scene, host.selection, state_.allShapes, sceneLines_);
            }
            for (const game_production::DebugLine& l : sceneLines_.Lines()) {
                if (l.space == game_production::DebugSpace::Space2D) debug_.push_back({{l.a.x, l.a.y}, {l.b.x, l.b.y}, l.color, 1.5f});
            }
        }
        f.debug = debug_;
        renderer_.Render(c.P().DC(), f);
        // デバッグ ▸ 描画とカメラ: 何を描いたか、カメラはどこか。
        if (game_production::ViewReportsWanted()) {
            using editor::Tr;
            using editor::TrF;
            game_production::ViewReport rep;
            rep.title = game ? Tr(L"ゲームビュー (2D)") : Tr(L"シーンビュー (2D)");
            rep.lines.push_back({Tr(L"スプライト"), TrF(L"{} 個", f.sprites.size())});
            rep.lines.push_back({Tr(L"線"), TrF(L"{} 本", f.lines.size() + f.overlay.size() + f.debug.size())});
            rep.lines.push_back({Tr(L"大きさ"), TrF(L"{:.0f} x {:.0f} DIP", f.viewport.w, f.viewport.h)});
            rep.lines.push_back({Tr(L"カメラの中心"), std::format(L"({:.2f}, {:.2f})", f.camera.center.x, f.camera.center.y)});
            rep.lines.push_back({Tr(L"カメラの拡大"), TrF(L"1 単位 = {:.1f} DIP ・ 回転 {:.1f} 度", f.camera.zoom, f.camera.rotation)});
            game_production::ReportView(this, std::move(rep));
        }
    }

    // 見えている平面のグリッド: 線が離れて見える刻みにして、5 本目 / 10 本目ごとを濃くする。
    void Grid(const space2d::ScreenRect& sr) {
        const space2d::GridStep g = space2d::GridFor(state_.camera.zoom);
        const Vec2 mn = space2d::ToWorld(state_.camera, sr, {sr.x, sr.y + sr.h});
        const Vec2 mx = space2d::ToWorld(state_.camera, sr, {sr.x + sr.w, sr.y});
        const long long x0 = static_cast<long long>(std::floor(mn.x / g.step)), x1 = static_cast<long long>(std::ceil(mx.x / g.step));
        const long long y0 = static_cast<long long>(std::floor(mn.y / g.step)), y1 = static_cast<long long>(std::ceil(mx.y / g.step));
        if (x1 - x0 > 2000 || y1 - y0 > 2000) return;  // (GridFor の刻みならありえない。念のための守り)
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

    // カメラが見る範囲 (ゲーム画面の形を、カメラの場所のまわりにカメラと一緒に回したもの) と、
    // 上の辺の小さな印 (どちらが上か)。
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

    // スプライトのない物: 小さな輪 (画面での大きさは同じ) と、X 軸に沿った短い線。
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

    // 絵の上のギズモ (gizmo2d の形から画面に描く: 描いた物がそのままつかめる物)。
    void DrawGizmo(ui::Context& c, const Rect& r, const game_production::SceneViewHost& host) {
        if (host.playing || drag_ == Drag::Body || drag_ == Drag::Band) return;  // Play: 編集しない
        const gizmo::Tool tool = drag_ == Drag::Gizmo && gizmo_ ? gizmo_->tool : state_.tool;
        // ドラッグ中、ギズモは物についていく (今ある場所から作り直す)。
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
            // 物の X に沿った線: どれだけ回ったかを見せる。
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

    SceneViewState2D state_;      // カメラ、ツール、刻み、グリッド (UserSettings に保存)
    reflect::Unknowns unknowns_;  // 新しい版のキー: そのまま書き戻す
    space2d::Renderer2D renderer_;
    uint64_t generation_ = 0;  // renderer_ と images_ を作った GPU のデバイス
    std::string transformType_, spriteType_, cameraType_;
    std::vector<Placed> placed_;
    std::vector<space2d::Line2D> lines_, overlay_, debug_;
    game_production::DebugDraw sceneLines_;  // このフレームのプラグインのシーンの線 (SceneLinesInfo)
    std::vector<space2d::SpriteDraw> sprites_;
    std::unordered_map<std::string, ImageEntry> images_;  // Sprite::image (アセットの id) ごと
    double now_ = 0;

    Drag drag_ = Drag::None;
    Vec2 press_{0, 0};
    int pressed_ = 0;  // 左ボタンを押したときにマウスの下にいたエンティティ (0: なし)
    gizmo::Handle hot_ = gizmo::Handle::None;
    std::optional<gizmo::Drag> gizmo_;
    std::optional<gizmo::Frame> pivot_;  // 中心: ドラッグが回す / 拡大縮小する中心
    std::vector<Edited> edited_;
    std::pair<size_t, size_t> history_{0, 0};  // ドラッグを始めたときの履歴の Undo / Redo の数
    bool snapOpen_ = false;              // 刻みの箱が開いている
    std::array<std::string, 3> snapText_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateSceneView2D() { return std::make_unique<SceneView2D>(); }

}  // namespace rm::game_x_2d
