// 書き出したゲームの 3D 描画 (game_production/runtime/GameDraw.h): ゲームのカメラ越しの
// シーンを、3D 空間のレンダラーでウィンドウに描く。
//
// 3D モデル (メッシュ) を持つ物は、書き出しで焼いたモデル (MeshBake: エディターと同じように
// 読み、このエンジンの形にしたもの) と、GPU が読める形の画像 (DDS: BC7 が無い GPU では
// RGBA にする) で描く。または 形 (箱, 球 ...) で描く。モデルが読めないときは箱で描く。
// メッシュ の無い物は描かない (エディターでは印で出る)。ライト、環境光、影、カメラ、シーンの
// 空 (SkyPaint.h: エディターと同じ描き手)、オフの物は、エディターのゲームビューと同じ。

#include "rm_engine/game_x_3d/runtime/GameDraw3D.h"

#include "base/Log.h"
#include "core/asset/BakedImage.h"
#include "platform/Image.h"
#include "rm_engine/game_x_3d/runtime/Register.h"
#include "rm_engine/game_production/runtime/GameDraw.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/game_x_3d/runtime/SkyPaint.h"
#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/space3d/runtime/BlockCompress.h"
#include "rm_engine/space3d/runtime/MeshBake.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Texture.h"
#include "rm_engine/space3d/runtime/TextureFile.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <dxgi.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rm::game_x_3d {

namespace {

class Draw3D final : public game_production::GameDrawer {
public:
    void Draw(const game_production::GameDrawFrame& f) override {
        const Color bg = f.background;
        if (!renderer_.Ready() || generation_ != f.gfx.Generation()) {
            bitmap_.Reset();
            picture_ = nullptr;
            models_.clear();  // (もう無いデバイスの上に作ったもの)
            pictures_.clear();
            sky_ = SkyPainter{};
            generation_ = f.gfx.Generation();
            std::string err;
            if (!renderer_.Init(f.gfx.D3D(), &err)) {
                if (!told_) log::Error("3D の描画を始められません: {}", err);
                told_ = true;
                f.dc->Clear(D2D1::ColorF(bg.r, bg.g, bg.b, 1));
                return;
            }
        }
        const std::string transform = reflect::TypeOf<space3d::Transform>().name, mesh = reflect::TypeOf<MeshRenderer>().name,
                          camera = reflect::TypeOf<Camera>().name, light = reflect::TypeOf<Light>().name,
                          ambient = reflect::TypeOf<AmbientLight>().name;
        boxes_.clear();
        meshes_.clear();
        lights_.clear();
        std::optional<space3d::Ambient> amb;
        std::optional<std::pair<Camera, space3d::Mat4>> eye;
        std::unordered_map<int, space3d::Mat4> world;  // 親は TreeOrder で子より先に来る
        shown_.clear();                                 // オンのものの位置 (WorldDrawFrame::world)
        drawnWith_.clear();                             // ... と、それらを描くモデル (WorldDrawFrame::model)
        for (const auto& t : f.scene.TreeOrder()) {
            const auto& e = *t.entity;
            const auto it = world.find(e.data.parent);
            const space3d::Mat4 parent = it != world.end() ? it->second : space3d::Mat4{};
            const game_production::Component* tc = e.FindComponent(transform);
            if (!tc || !tc->Known()) {
                world[e.data.id] = parent;
                continue;
            }
            const space3d::Mat4 w = space3d::WorldMatrix(parent, *static_cast<const space3d::Transform*>(tc->value.Get()));
            world[e.data.id] = w;
            if (!t.activeInHierarchy) continue;  // オフ: ゲームには出ない
            shown_[e.data.id] = w;
            const game_production::Component* cc = e.FindComponent(camera);
            const game_production::Component* lc = e.FindComponent(light);
            const game_production::Component* ac = e.FindComponent(ambient);
            const game_production::Component* mc = e.FindComponent(mesh);
            if (cc && cc->Known() && static_cast<uint64_t>(e.data.id) == f.camera) eye.emplace(*static_cast<const Camera*>(cc->value.Get()), w);
            if (lc && lc->Known()) lights_.push_back(ToRenderLight(*static_cast<const Light*>(lc->value.Get()), w));
            if (ac && ac->Known() && !amb) amb = ToRenderAmbient(*static_cast<const AmbientLight*>(ac->value.Get()));
            if (!mc || !mc->Known()) continue;  // メッシュ が無い: 見えるものが無い (空のエンティティ、カメラ、パーティクル ...)
            const MeshRenderer* mr = static_cast<const MeshRenderer*>(mc->value.Get());
            const bool shadow = mr->castShadows;
            constexpr Color kPlain{0.78f, 0.80f, 0.84f, 1};  // 箱 (モデルが無い) と単純な形
            if (const auto shape = ShapeOf(mr->shape)) {
                if (const space3d::GpuMesh* g = renderer_.ShapeOf(*shape)) {
                    meshes_.push_back({g, w, kPlain, shadow, {}});
                    drawnWith_[e.data.id] = g;
                }
                continue;
            }
            if (const Model* model = ModelOf(f, mr->model)) {
                meshes_.push_back({model->gpu.get(), w, Color{1, 1, 1, 1}, shadow, model->materials});
                drawnWith_[e.data.id] = model->gpu.get();
                continue;
            }
            boxes_.push_back({w, kPlain, shadow});
        }
        // (kMaxLights より多いとき: レンダラーの MakeLights が選ぶ - 影を付ける太陽は順番で切られない。)
        if (!eye) {
            // 見るカメラが無い (ゲーム設定の 最初のカメラ): 背景だけ。
            if (!told_) log::Warn("ゲームの設定の「最初のカメラ」がカメラの付いた有効なエンティティを指していないので、背景だけを塗ります");
            told_ = true;
            f.dc->Clear(D2D1::ColorF(bg.r, bg.g, bg.b, 1));
            return;
        }
        float dpiX = 96, dpiY = 96;
        f.dc->GetDpi(&dpiX, &dpiY);
        space3d::RenderFrame frame;
        frame.width = static_cast<uint32_t>(std::max(1.f, f.width * dpiX / 96.f));
        frame.height = static_cast<uint32_t>(std::max(1.f, f.height * dpiY / 96.f));
        frame.view = space3d::ViewFromWorld(eye->second);
        frame.projection = ProjectionOf(eye->first, f.width / std::max(1.f, f.height));
        frame.clear = BackgroundOf(eye->first, bg);
        frame.boxes = boxes_;
        frame.meshes = meshes_;
        frame.ambient = amb.value_or(space3d::Ambient{});
        frame.shadows = true;
        // シーンの 空 は何よりも後ろ (時間はゲームのもの。雲が流れる)。光を空に合わせる。
        seconds_ += f.dt;
        const Sky* sky = f.scene.Setting<Sky>();
        if (sky && sky->kind != SkyKind::None) {
            look_ = LookOf(*sky, seconds_, SkyWeatherOf(f.registry, f.scene, seconds_));  // (その雲: 天候のもの)
            const SkyLook& look = look_;
            SkyPainter::Sources sources;
            sources.picture = [&](const AssetRef& ref) { return PictureOf(f, ref); };
            sources.code = [&](const AssetRef& ref) { return f.asset(ref); };
            sources.name = [](const AssetRef& ref) { return ref.guid; };
            sources.readEvery = -1;  // ゲームのファイルは変わらない
            frame.sky = sky_.Prepare(renderer_, *sky, look, sources);
            FollowSky(look, lights_, frame.ambient);
        }
        frame.lights = lights_;
        // 他のプラグインが世界に自分で描くもの (パーティクル: WorldDraw.h)。
        if (!drawersMade_ && f.registry) {
            drawersMade_ = true;
            for (const auto& d : f.registry->Contributions<WorldDrawInfo>(kWorldDrawPoint)) {
                if (d.item->create) drawers_.push_back(d.item->create());
            }
        }
        extras_.clear();
        if (!drawers_.empty()) {
            WorldDrawFrame wf{f.scene, f.gfx,
                              [this](int id) -> const space3d::Mat4* {
                                  const auto it = shown_.find(id);
                                  return it != shown_.end() ? &it->second : nullptr;
                              },
                              f.dt, true, {}, f.asset, nullptr, f.registry};
            wf.seconds = seconds_;
            wf.sky = sky && sky->kind != SkyKind::None ? &look_ : nullptr;
            wf.model = [this](int id) -> const space3d::GpuMesh* {
                const auto it = drawnWith_.find(id);
                return it != drawnWith_.end() ? it->second : nullptr;
            };
            wf.shape = [this](space3d::Shape s) { return renderer_.ShapeOf(s); };
            // それらがフレームに足すもの (天候: 向きの地図、濡れ)。そのあと、それらが描くもの。
            for (const auto& d : drawers_) {
                FrameAdditions add;
                d->PrepareFrame(wf, add);
                if (frame.mapDirections.empty() && !add.mapDirections.empty()) {
                    frame.mapDirections = add.mapDirections;
                    frame.mapShapes = add.mapShapes;
                    frame.mapMovingOnly = add.mapMovingOnly;
                }
                if (!frame.surface) frame.surface = add.surface;
                if (frame.solids.empty()) frame.solids = add.solids;
                if (!(frame.fog.a > 0)) frame.fog = add.fog;
            }
            for (const auto& d : drawers_) d->Prepare(wf, extras_);
        }
        frame.extras = extras_;
        f.dc->Flush();  // 先に Direct2D の描画、そのあと Direct3D の絵
        ID3D11Texture2D* picture = renderer_.Render(f.gfx.D3DContext(), frame);
        if (!picture) return;
        if (picture != picture_ || !bitmap_) {
            bitmap_.Reset();
            Microsoft::WRL::ComPtr<IDXGISurface> surface;
            if (FAILED(picture->QueryInterface(IID_PPV_ARGS(&surface)))) return;
            const D2D1_BITMAP_PROPERTIES1 props =
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpiX, dpiY);
            if (FAILED(f.dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap_))) return;
            picture_ = picture;
        }
        f.dc->DrawBitmap(bitmap_.Get(), D2D1::RectF(0, 0, f.width, f.height));
    }

private:
    struct Model {
        std::shared_ptr<const space3d::GpuMesh> gpu;
        std::vector<space3d::DrawMaterial> materials;
    };
    // アセット id が指す焼いたモデルを、最初に GPU に置く (null: 箱を描く - 1 度だけ知らせる)。
    const Model* ModelOf(const game_production::GameDrawFrame& f, const AssetRef& ref) {
        if (ref.Empty()) return nullptr;
        auto [it, first] = models_.try_emplace(ref.guid);
        if (!first) return it->second.gpu ? &it->second : nullptr;
        std::string err;
        const auto bytes = f.asset(ref);
        std::vector<space3d::TextureSampling> sampling;  // 画像ごと: 読み方 (その取り込みの設定)
        const auto mesh = bytes ? space3d::ReadBakedMesh(*bytes, &err, &sampling) : std::nullopt;
        if (!mesh) {
            log::Warn("モデルを読めないので箱で描きます ({}): {}", ref.guid, bytes ? err : std::string("ゲームに入っていません"));
            return nullptr;
        }
        Model& m = it->second;
        m.gpu = renderer_.Upload(*mesh, &err);
        if (!m.gpu) {
            log::Warn("モデルを GPU に上げられないので箱で描きます ({}): {}", ref.guid, err);
            return nullptr;
        }
        int pictures = 0;
        for (const space3d::MeshMaterial& mat : mesh->materials) {
            space3d::DrawMaterial d = space3d::MaterialNumbers(mat);
            const auto slots = space3d::PictureSlots(mat);
            for (size_t slot = 0; slot < slots.size(); ++slot) {
                if (slots[slot].image < 0) continue;
                const std::string& path = mesh->images[static_cast<size_t>(slots[slot].image)].file;
                const auto dds = f.file(path);
                auto pixels = dds ? space3d::ReadDds(std::span(reinterpret_cast<const uint8_t*>(dds->data()), dds->size()), &err) : std::nullopt;
                if (!pixels) {
                    log::Warn("モデルの絵を読めません ({}): {}", path, err);
                    continue;
                }
                if (pixels->format == space3d::TextureFormat::BC7 && !renderer_.SupportsBC7()) {
                    // BC7 の無い GPU: 画像を素の RGBA にする。
                    for (platform::Image& level : pixels->levels) level = space3d::Decompress(level, pixels->format);
                    pixels->format = space3d::TextureFormat::Rgba8;
                }
                const size_t image = static_cast<size_t>(slots[slot].image);
                if (auto tex = renderer_.UploadTexture(*pixels, &err, image < sampling.size() ? sampling[image] : space3d::TextureSampling{})) {
                    space3d::SetPicture(d, slot, std::move(tex));
                    ++pictures;
                } else {
                    log::Warn("モデルの絵を GPU に上げられません ({}): {}", path, err);
                }
            }
            m.materials.push_back(std::move(d));
        }
        log::Info("モデルを GPU に上げました: {} (三角形 {}・材質 {}・絵 {})", ref.guid, mesh->indices.size() / 3, mesh->materials.size(), pictures);
        return &m;
    }

    // 空の画像 (画像アセット: 焼いたものそのまま、またはファイル) を GPU に置く。最初に
    // (null: 読めない - 1 度だけ知らせる)。
    space3d::TextureRef PictureOf(const game_production::GameDrawFrame& f, const AssetRef& ref) {
        auto [it, first] = pictures_.try_emplace(ref.guid);
        if (!first) return it->second;
        std::string err;
        const auto bytes = f.asset(ref);
        const auto baked = bytes ? asset::ReadBakedImage(*bytes, &err) : std::nullopt;
        std::optional<platform::Image> image;
        if (baked) {
            const auto* data = reinterpret_cast<const uint8_t*>(baked->picture.data());
            image = platform::DecodeImage(f.gfx.Wic(), std::span(data, baked->picture.size()), platform::PixelOrder::Rgba,
                                          {space3d::kMaxTextureSize, space3d::kMaxTextureSize}, &err);
        }
        if (image) it->second = renderer_.UploadTexture(*image, true, &err);
        if (!it->second) log::Warn("空の画像を読めません ({}): {}", ref.guid, bytes ? err : std::string("ゲームに入っていません"));
        return it->second;
    }

    space3d::Renderer renderer_;
    std::unordered_map<std::string, Model> models_;
    std::unordered_map<std::string, space3d::TextureRef> pictures_;  // 空の分。アセット id ごと
    SkyPainter sky_;
    SkyLook look_;  // このフレームのもの (レンダラーが雨 / 雪を読む)
    double seconds_ = 0;  // 空が動くもとになるゲームの時間
    std::vector<space3d::MeshDraw> meshes_;
    uint64_t generation_ = 0;
    std::vector<space3d::Box> boxes_;
    std::vector<space3d::Light> lights_;
    ID3D11Texture2D* picture_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    bool told_ = false;
    // 他のプラグインの世界への描画 (WorldDraw.h)。最初のフレームで作る。
    std::vector<std::unique_ptr<WorldDrawer>> drawers_;
    bool drawersMade_ = false;
    std::vector<space3d::ExtraDraw> extras_;
    std::unordered_map<int, space3d::Mat4> shown_;
    std::unordered_map<int, const space3d::GpuMesh*> drawnWith_;  // 各エンティティを描くモデル
};

}  // namespace

std::unique_ptr<game_production::GameDrawer> CreateGameDraw3D() { return std::make_unique<Draw3D>(); }

namespace {

// 書き出したゲームがこのプラグインから登録するもの (生成される main が下の C の名前で呼ぶ):
// エディターも持つランタイム側と、描き手 (エディターにはかわりにシーンビューがある)。
void RegisterForGame(RuntimeRegistry& registry) {
    RegisterRuntime(registry);
    static const game_production::GameDrawInfo kDraw{"3d", 0, &CreateGameDraw3D};
    registry.Contribute(game_production::kGameDrawPoint, kDraw);
}

}  // namespace

}  // namespace rm::game_x_3d

RM_RUNTIME_ENTRY(rm_engine, game_x_3d, rm::game_x_3d::RegisterForGame)
