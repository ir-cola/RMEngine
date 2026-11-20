// 書き出したゲームの 2D の描画 (game_production/runtime/GameDraw.h): ゲームの カメラ (2D) 越しに
// 平面を、2D 空間のレンダラーでウィンドウに描く。順番も見た目もエディターのゲームビューと同じ:
// スプライトは奥から手前へ (Transform 2D の 並び順、次にスプライトの、次に階層の)、画像の
// アトラスのセル、画像がなければ色の四角。オフの物は描かない。
// 3D のゲームの上では (DrawOverlay): 「3D の上に重ねる」の カメラ (2D) 越しに、最初のカメラ が
// それならそれ、そうでなければ最初の有効なもの。背景は塗りつぶさない (エディターのゲームビューと同じ)。

#include "rm_engine/game_x_2d/runtime/GameDraw2D.h"

#include "base/Log.h"
#include "platform/Image.h"
#include "core/asset/BakedImage.h"
#include "rm_engine/game_x_2d/runtime/Register.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/runtime/Atlas.h"
#include "rm_engine/space2d/runtime/Renderer2D.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace rm::game_x_2d {

using asset::BakedImageInfo;
using asset::kMaxImageSize;
using asset::ReadBakedImage;
using asset::WriteBakedImage;

namespace {

class Draw2D final : public game_production::GameDrawer {
public:
    void Draw(const game_production::GameDrawFrame& f) override { DrawWith(f, false); }
    void DrawOverlay(const game_production::GameDrawFrame& f) override { DrawWith(f, true); }

private:
    void DrawWith(const game_production::GameDrawFrame& f, bool overlay) {
        if (generation_ != f.gfx.Generation() || dc_ != f.dc) {  // デバイスが作り直された: ビットマップは捨てる
            generation_ = f.gfx.Generation();
            dc_ = f.dc;
            images_.clear();
            renderer_ = {};
        }
        const std::string transform = reflect::TypeOf<space2d::Transform2D>().name, sprite = reflect::TypeOf<Sprite>().name,
                          camera = reflect::TypeOf<Camera2D>().name;
        struct Placed {
            space2d::SpriteDraw draw;
            space2d::DrawKey key;
        };
        std::vector<Placed> placed;
        std::optional<std::pair<Camera2D, space2d::Mat3>> eye, firstOverlay;
        struct Parent {
            space2d::Mat3 world;
            int order = 0;
        };
        std::unordered_map<int, Parent> parents;
        size_t index = 0;
        for (const auto& t : f.scene.TreeOrder()) {
            const auto& e = *t.entity;
            const auto it = parents.find(e.data.parent);
            const Parent parent = it != parents.end() ? it->second : Parent{};
            const game_production::Component* tc = e.FindComponent(transform);
            if (!tc || !tc->Known()) {
                parents[e.data.id] = parent;
                ++index;
                continue;
            }
            const auto& tr = *static_cast<const space2d::Transform2D*>(tc->value.Get());
            const space2d::Mat3 world = space2d::WorldMatrix(parent.world, tr);
            const int order = parent.order + tr.order;
            parents[e.data.id] = {world, order};
            const size_t tree = index++;
            if (!t.activeInHierarchy) continue;  // オフ: ゲームには出ない
            if (const game_production::Component* cc = e.FindComponent(camera); cc && cc->Known()) {
                const auto& cam = *static_cast<const Camera2D*>(cc->value.Get());
                if (static_cast<uint64_t>(e.data.id) == f.camera && (!overlay || cam.overlay)) eye.emplace(cam, world);
                if (overlay && cam.overlay && !firstOverlay) firstOverlay.emplace(cam, world);
            }
            const game_production::Component* sc = e.FindComponent(sprite);
            if (!sc || !sc->Known()) continue;
            const auto& sp = *static_cast<const Sprite*>(sc->value.Get());
            Placed p;
            p.key = {order, sp.order, tree};
            p.draw.world = world;
            p.draw.color = sp.color;
            p.draw.flipX = sp.flipX;
            p.draw.flipY = sp.flipY;
            const Loaded* img = Image(f, sp.image);
            p.draw.crisp = sp.crisp || (img && img->sharp);  // スプライト自身の設定、または画像の取り込みの設定 (エディターと同じ)
            if (img) {
                // ビットマップのピクセルでのセル。ワールドでの大きさはファイルのピクセル数から (2D シーンビューと同じ)。
                const D2D1_SIZE_U px = img->bitmap->GetPixelSize();
                p.draw.image = img->bitmap.Get();
                p.draw.part = space2d::CellOf(px.width, px.height, sp.columns, sp.rows, sp.cell);
                const space2d::PixelRect file = space2d::CellOf(img->width, img->height, sp.columns, sp.rows, sp.cell);
                const float ppu = std::max(sp.pixelsPerUnit, 0.01f);
                p.draw.size = {static_cast<float>(file.w) / ppu, static_cast<float>(file.h) / ppu};
            }
            placed.push_back(p);
        }
        if (overlay) {
            if (!eye) eye = firstOverlay;
            if (!eye) return;  // 3D の絵の上に重ねる層はない
        }
        if (!eye) {
            if (!told_) log::Warn("ゲームの設定の「最初のカメラ」がカメラ (2D) の付いた有効なエンティティを指していないので、背景だけを塗ります");
            told_ = true;
            f.dc->Clear(D2D1::ColorF(f.background.r, f.background.g, f.background.b, 1));
            return;
        }
        std::vector<space2d::DrawKey> keys;
        for (const Placed& p : placed) keys.push_back(p.key);
        sprites_.clear();
        for (const size_t i : space2d::DrawOrder(keys)) sprites_.push_back(placed[i].draw);
        space2d::RenderFrame2D frame;
        frame.viewport = {0, 0, f.width, f.height};
        frame.camera = ViewOf(eye->first, eye->second, f.height);
        frame.clear = overlay ? Color{0, 0, 0, 0} : BackgroundOf(eye->first, f.background);
        frame.sprites = sprites_;
        renderer_.Render(f.dc, frame);
    }

    // ゲームのアセットから読んだ画像: ビットマップ、ファイル自身の大きさ、くっきり。
    struct Loaded {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        uint32_t width = 0, height = 0;
        bool sharp = false;
    };

    // アセット id が指す画像を、ゲームのアセットから一度だけ読む (null: 色の四角)。
    // ベイク済み (BakedImage: 大きさと取り込みの設定つきのファイル) か、そのままの画像ファイル。
    const Loaded* Image(const game_production::GameDrawFrame& f, const AssetRef& ref) {
        if (ref.Empty()) return nullptr;
        auto [it, made] = images_.try_emplace(ref.guid);
        Loaded& m = it->second;
        if (made) {
            std::string err;
            const auto bytes = f.asset(ref);
            const auto baked = bytes ? ReadBakedImage(*bytes, &err) : std::nullopt;
            if (baked) {
                const BakedImageInfo& info = baked->info;
                const uint32_t most = info.maxSize > 0 ? std::min(info.maxSize, kMaxImageSize) : kMaxImageSize;
                const auto pixels = platform::DecodeImage(f.gfx.Wic(), std::span(reinterpret_cast<const uint8_t*>(baked->picture.data()), baked->picture.size()),
                                                          platform::PixelOrder::PremultipliedBgra, {most, most}, &err);
                if (pixels) m.bitmap = platform::BitmapFromImage(f.dc, *pixels);
                if (pixels && !m.bitmap) err = "Direct2D のビットマップにできませんでした";
                if (m.bitmap) {
                    m.width = info.width > 0 ? info.width : pixels->width;  // (ベイクしていない画像: そのピクセル数)
                    m.height = info.height > 0 ? info.height : pixels->height;
                    m.sharp = info.sharp;
                    log::Info("画像を読みました: {} ({} x {}{}{})", ref.guid, m.width, m.height, m.sharp ? "・くっきり" : "",
                              pixels->width != m.width ? std::format("・{} x {} に縮めて", pixels->width, pixels->height) : std::string());
                }
            }
            if (!bytes) {
                log::Warn("画像がゲームに入っていないので四角で描きます ({})", ref.guid);
            } else if (!m.bitmap) {
                log::Warn("画像を読めないので四角で描きます ({}): {}", ref.guid, err);
            }
        }
        return m.bitmap ? &m : nullptr;
    }

    space2d::Renderer2D renderer_;
    uint64_t generation_ = 0;
    ID2D1DeviceContext* dc_ = nullptr;
    std::unordered_map<std::string, Loaded> images_;
    std::vector<space2d::SpriteDraw> sprites_;
    bool told_ = false;
};

}  // namespace

std::unique_ptr<game_production::GameDrawer> CreateGameDraw2D() { return std::make_unique<Draw2D>(); }

namespace {

// 書き出したゲームがこのプラグインについて登録するもの (生成した main が下の C の名前で呼ぶ):
// エディターにもあるランタイムの部分と、描画役 (エディターは代わりにシーンビューを持つ)。
void RegisterForGame(RuntimeRegistry& registry) {
    RegisterRuntime(registry);
    static const game_production::GameDrawInfo kDraw{"2d", 10, &CreateGameDraw2D};
    registry.Contribute(game_production::kGameDrawPoint, kDraw);
}

}  // namespace

}  // namespace rm::game_x_2d

RM_RUNTIME_ENTRY(rm_engine, game_x_2d, rm::game_x_2d::RegisterForGame)
