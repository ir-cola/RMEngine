// The exported game's drawing in 3D (game_production/runtime/GameDraw.h): the scene through the game's
// camera, by 3D 空間's renderer, into the window.
//
// A thing with a 3D model (メッシュ) is drawn with the model the export baked (MeshBake: read as the editor
// reads it, in this engine's form) and its pictures as the GPU takes them (DDS: BC7 turned into RGBA on a
// GPU without it), or as its 形 (箱, 球 ...); when its model cannot be read, as a box. A thing without a
// メッシュ is not drawn (the editor shows it as a mark). Lights, 環境光, shadows, the camera, the scene's
// 空 (SkyPaint.h: the same painter as the editor's) and switched-off things are as in the editor's game
// view.

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
            models_.clear();  // (made on the devices that are gone)
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
        std::unordered_map<int, space3d::Mat4> world;  // parents come before their children in TreeOrder
        shown_.clear();                                 // the places of what is switched on (WorldDrawFrame::world)
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
            if (!t.activeInHierarchy) continue;  // switched off: not in the game
            shown_[e.data.id] = w;
            const game_production::Component* cc = e.FindComponent(camera);
            const game_production::Component* lc = e.FindComponent(light);
            const game_production::Component* ac = e.FindComponent(ambient);
            const game_production::Component* mc = e.FindComponent(mesh);
            if (cc && cc->Known() && static_cast<uint64_t>(e.data.id) == f.camera) eye.emplace(*static_cast<const Camera*>(cc->value.Get()), w);
            if (lc && lc->Known()) lights_.push_back(ToRenderLight(*static_cast<const Light*>(lc->value.Get()), w));
            if (ac && ac->Known() && !amb) amb = ToRenderAmbient(*static_cast<const AmbientLight*>(ac->value.Get()));
            if (!mc || !mc->Known()) continue;  // no メッシュ: nothing to see (an empty entity, a camera, パーティクル ...)
            const MeshRenderer* mr = static_cast<const MeshRenderer*>(mc->value.Get());
            const bool shadow = mr->castShadows;
            constexpr Color kPlain{0.78f, 0.80f, 0.84f, 1};  // a box (its model is missing) and a simple shape
            if (const auto shape = ShapeOf(mr->shape)) {
                if (const space3d::GpuMesh* g = renderer_.ShapeOf(*shape)) meshes_.push_back({g, w, kPlain, shadow, {}});
                continue;
            }
            if (const Model* model = ModelOf(f, mr->model)) {
                meshes_.push_back({model->gpu.get(), w, Color{1, 1, 1, 1}, shadow, model->materials});
                continue;
            }
            boxes_.push_back({w, kPlain, shadow});
        }
        // (More than kMaxLights: the renderer's MakeLights chooses - the sun with shadows is never cut by the order.)
        if (!eye) {
            // No camera to see through (the game settings' 最初のカメラ): the background only.
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
        // The scene's 空, behind everything (its time: the game's, as the clouds drift); 光を空に合わせる.
        seconds_ += f.dt;
        if (const Sky* sky = f.scene.Setting<Sky>(); sky && sky->kind != SkyKind::None) {
            const SkyLook look = LookOf(*sky, seconds_);
            SkyPainter::Sources sources;
            sources.picture = [&](const AssetRef& ref) { return PictureOf(f, ref); };
            sources.code = [&](const AssetRef& ref) { return f.asset(ref); };
            sources.name = [](const AssetRef& ref) { return ref.guid; };
            sources.readEvery = -1;  // the game's files do not change
            frame.sky = sky_.Prepare(renderer_, *sky, look, sources);
            FollowSky(look, lights_, frame.ambient);
        }
        frame.lights = lights_;
        // What other plugins draw into the world themselves (パーティクル: WorldDraw.h).
        if (!drawersMade_ && f.registry) {
            drawersMade_ = true;
            for (const auto& d : f.registry->Contributions<WorldDrawInfo>(kWorldDrawPoint)) {
                if (d.item->create) drawers_.push_back(d.item->create());
            }
        }
        extras_.clear();
        if (!drawers_.empty()) {
            const WorldDrawFrame wf{f.scene, f.gfx,
                                    [this](int id) -> const space3d::Mat4* {
                                        const auto it = shown_.find(id);
                                        return it != shown_.end() ? &it->second : nullptr;
                                    },
                                    f.dt, true, {}, f.asset, nullptr, f.registry};
            for (const auto& d : drawers_) d->Prepare(wf, extras_);
        }
        frame.extras = extras_;
        f.dc->Flush();  // Direct2D's drawing first, then Direct3D's picture
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
    // The baked model an asset id names, put on the GPU the first time (null: draw a box - told once).
    const Model* ModelOf(const game_production::GameDrawFrame& f, const AssetRef& ref) {
        if (ref.Empty()) return nullptr;
        auto [it, first] = models_.try_emplace(ref.guid);
        if (!first) return it->second.gpu ? &it->second : nullptr;
        std::string err;
        const auto bytes = f.asset(ref);
        std::vector<bool> sharp;  // per image: drawn くっきり (its import setting)
        const auto mesh = bytes ? space3d::ReadBakedMesh(*bytes, &err, &sharp) : std::nullopt;
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
                    // A GPU without BC7: the picture as plain RGBA.
                    for (platform::Image& level : pixels->levels) level = space3d::Decompress(level, pixels->format);
                    pixels->format = space3d::TextureFormat::Rgba8;
                }
                const size_t image = static_cast<size_t>(slots[slot].image);
                if (auto tex = renderer_.UploadTexture(*pixels, &err, image < sharp.size() && sharp[image])) {
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

    // A picture of the sky (an image asset: baked as it was, or the file) on the GPU, read the first
    // time (null: it cannot be read - told once).
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
    std::unordered_map<std::string, space3d::TextureRef> pictures_;  // the sky's, by asset id
    SkyPainter sky_;
    double seconds_ = 0;  // the game's time the sky moves by
    std::vector<space3d::MeshDraw> meshes_;
    uint64_t generation_ = 0;
    std::vector<space3d::Box> boxes_;
    std::vector<space3d::Light> lights_;
    ID3D11Texture2D* picture_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    bool told_ = false;
    // Other plugins' drawing into the world (WorldDraw.h), made at the first frame.
    std::vector<std::unique_ptr<WorldDrawer>> drawers_;
    bool drawersMade_ = false;
    std::vector<space3d::ExtraDraw> extras_;
    std::unordered_map<int, space3d::Mat4> shown_;
};

}  // namespace

std::unique_ptr<game_production::GameDrawer> CreateGameDraw3D() { return std::make_unique<Draw3D>(); }

namespace {

// What the exported game registers of this plugin (its generated main calls it by the C name below):
// the runtime part the editor has too, and the drawer (the editor has its scene views instead).
void RegisterForGame(RuntimeRegistry& registry) {
    RegisterRuntime(registry);
    static const game_production::GameDrawInfo kDraw{"3d", 0, &CreateGameDraw3D};
    registry.Contribute(game_production::kGameDrawPoint, kDraw);
}

}  // namespace

}  // namespace rm::game_x_3d

RM_RUNTIME_ENTRY(rm_engine, game_x_3d, rm::game_x_3d::RegisterForGame)
