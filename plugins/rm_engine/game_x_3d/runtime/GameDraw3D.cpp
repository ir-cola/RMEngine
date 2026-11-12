// The exported game's drawing in 3D (game_production/runtime/GameDraw.h): the scene through the game's
// camera, by 3D 空間's renderer, into the window.
//
// ★ First version: a thing with a 3D model (メッシュ) is drawn as its box (models are not read from the
// .rmpak yet - MeshFile reads files, and a model's other files are named inside it). Lights, 環境光,
// shadows, the camera and switched-off things are as in the editor's game view.

#include "rm_engine/game_x_3d/runtime/GameDraw3D.h"

#include "base/Log.h"
#include "rm_engine/game_x_3d/runtime/Register.h"
#include "rm_engine/game_production/runtime/GameDraw.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <dxgi.h>

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
        lights_.clear();
        std::optional<space3d::Ambient> amb;
        std::optional<std::pair<Camera, space3d::Mat4>> eye;
        std::unordered_map<int, space3d::Mat4> world;  // parents come before their children in TreeOrder
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
            const game_production::Component* cc = e.FindComponent(camera);
            const game_production::Component* lc = e.FindComponent(light);
            const game_production::Component* ac = e.FindComponent(ambient);
            const game_production::Component* mc = e.FindComponent(mesh);
            if (cc && cc->Known() && static_cast<uint64_t>(e.data.id) == f.camera) eye.emplace(*static_cast<const Camera*>(cc->value.Get()), w);
            if (lc && lc->Known()) lights_.push_back(ToRenderLight(*static_cast<const Light*>(lc->value.Get()), w));
            if (ac && ac->Known() && !amb) amb = ToRenderAmbient(*static_cast<const AmbientLight*>(ac->value.Get()));
            const bool marker = ((cc && cc->Known()) || (lc && lc->Known()) || (ac && ac->Known())) && !(mc && mc->Known());
            if (marker) continue;
            const bool shadow = !(mc && mc->Known()) || static_cast<const MeshRenderer*>(mc->value.Get())->castShadows;
            boxes_.push_back({w, Color{0.78f, 0.80f, 0.84f, 1}, shadow});
        }
        if (lights_.size() > space3d::kMaxLights) lights_.resize(space3d::kMaxLights);
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
        frame.lights = lights_;
        frame.ambient = amb.value_or(space3d::Ambient{});
        frame.shadows = true;
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
    space3d::Renderer renderer_;
    uint64_t generation_ = 0;
    std::vector<space3d::Box> boxes_;
    std::vector<space3d::Light> lights_;
    ID3D11Texture2D* picture_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    bool told_ = false;
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
