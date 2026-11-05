#include "rm_engine/game_x_3d/editor/SceneView3D.h"

#include "base/Log.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

namespace rm::game_x_3d {

namespace {

namespace theme = editor::theme;
using editor::Tr;
using space3d::Mat4;
using space3d::operator*;  // Vec3 is rm::Vec3: its 3D operators live in space3d
using ui::Rect;

constexpr float kLookDegreesPerDip = 0.25f;
constexpr float kOrbitDegreesPerDip = 0.4f;
constexpr float kClickSlop = 4.f;  // a press that moves less than this is a click
constexpr int kGridHalf = 20;      // the grid goes from -20 to 20 (1 per line, darker every 10)

struct Placed {
    int id;
    Mat4 world;
    bool active;
};

class SceneView3D final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 8 || r.h < 8) return;
        if (!Ready(host)) {
            ui::TextStyle ts;
            ts.size = 13;
            ts.wrap = true;
            ts.color = theme::Danger;
            ts.h = ui::HAlign::Center;
            c.P().Text(r.Inset(24), Tr(L"3D の描画を始められませんでした。くわしくはコンソールを見てください。"), ts);
            return;
        }
        Place(host.scene);
        HandleInput(c, r, host);
        Render(c, r, host);
        DrawHints(c, r);
    }

private:
    bool Ready(game_production::SceneViewHost& host) {
        if (!tried_) {
            tried_ = true;
            std::string err;
            if (!renderer_.Init(host.host.Graphics().D3D(), &err)) log::Error("シーンビュー: {}", err);
        }
        return renderer_.Ready();
    }

    // Where every entity with a Transform is in the world (its parents' Transforms included).
    void Place(const game_production::Scene& scene) {
        placed_.clear();
        const std::string transformType = reflect::TypeOf<space3d::Transform>().name;
        std::unordered_map<int, Mat4> world;  // parents come before their children in TreeOrder
        for (const auto& t : scene.TreeOrder()) {
            const auto& e = *t.entity;
            const auto it = world.find(e.data.parent);
            const Mat4 parent = it != world.end() ? it->second : Mat4{};
            const game_production::Component* comp = e.FindComponent(transformType);
            if (comp && comp->Known()) {
                const Mat4 w = space3d::WorldMatrix(parent, *static_cast<const space3d::Transform*>(comp->value.Get()));
                world[e.data.id] = w;
                placed_.push_back({e.data.id, w, t.activeInHierarchy});
            } else {
                world[e.data.id] = parent;  // no place of its own: its children follow the parent
            }
        }
    }

    Mat4 ViewProjection(const Rect& r) const { return cam_.ProjectionMatrix(r.w / r.h) * cam_.ViewMatrix(); }

    void HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) {
        const ui::Input& in = c.In();
        const double now = c.Time();
        const float dt = static_cast<float>(std::clamp(now - lastTime_, 0.0, 0.1));
        lastTime_ = now;
        const bool hovered = c.IsHovered(r);
        // How far the mouse moved (still counted while the cursor is held at the drag's start).
        const float dx = in.mouseDX, dy = in.mouseDY;

        // Start a drag (only inside the view); it goes on outside until the button is let go.
        if (drag_ == Drag::None && hovered) {
            if (c.MousePressed(ui::Mouse::Right)) {
                drag_ = Drag::Look;
            } else if (c.MousePressed(ui::Mouse::Middle)) {
                drag_ = Drag::Pan;
            } else if (c.MousePressed() && in.alt) {
                drag_ = Drag::Orbit;
            } else if (c.MousePressed()) {
                drag_ = Drag::Click;
                pressX_ = in.mouseX;
                pressY_ = in.mouseY;
            }
        } else if (drag_ != Drag::None) {
            const bool held = drag_ == Drag::Look ? in.down[1] : drag_ == Drag::Pan ? in.down[2] : in.down[0];
            if (!held) {
                if (drag_ == Drag::Click && std::hypot(in.mouseX - pressX_, in.mouseY - pressY_) < kClickSlop) Pick(r, host);
                drag_ = Drag::None;
            } else if (drag_ == Drag::Look) {
                space3d::Look(cam_, -dx * kLookDegreesPerDip, dy * kLookDegreesPerDip);
                Fly(c, dt);
            } else if (drag_ == Drag::Pan) {
                space3d::Pan(cam_, dx, dy, r.h);
            } else if (drag_ == Drag::Orbit) {
                space3d::Orbit(cam_, -dx * kOrbitDegreesPerDip, dy * kOrbitDegreesPerDip);
            }
        }
        // Turning / sliding never stops at the edge of the screen: the cursor hides and stays where the
        // drag started (from its first frame), and comes back there when the button is let go.
        if (drag_ != Drag::None && drag_ != Drag::Click) c.LockMouse();
        if (hovered && in.wheel != 0.f) space3d::Dolly(cam_, in.wheel);
        if (hovered && c.Focused() == 0 && drag_ != Drag::Look && c.KeyPressed(ui::Key::F)) FrameSelection(host);
    }

    // While looking around (right button held): W A S D forward / left / back / right, E Q up / down.
    void Fly(ui::Context& c, float dt) {
        auto axis = [&](ui::Key plus, ui::Key minus) { return (c.KeyDown(plus) ? 1.f : 0.f) - (c.KeyDown(minus) ? 1.f : 0.f); };
        const Vec3 dir{axis(ui::Key::D, ui::Key::A), axis(ui::Key::E, ui::Key::Q), axis(ui::Key::W, ui::Key::S)};
        if (dir.x == 0 && dir.y == 0 && dir.z == 0) return;
        const float speed = std::max(2.f, cam_.distance * 0.8f) * (c.In().shift ? 4.f : 1.f);
        space3d::Fly(cam_, dir * (speed * dt));
        c.RequestAnimation();  // keeps flying while the keys are held, without moving the mouse
    }

    void Pick(const Rect& r, game_production::SceneViewHost& host) {
        const auto inv = space3d::Inverse(ViewProjection(r));
        if (!inv) return;
        const float ndcX = (pressX_ - r.x) / r.w * 2.f - 1.f;
        const float ndcY = 1.f - (pressY_ - r.y) / r.h * 2.f;
        const space3d::Ray ray = space3d::ScreenRay(*inv, ndcX, ndcY);
        std::optional<float> best;
        int hit = 0;
        for (const Placed& p : placed_) {
            const auto t = space3d::IntersectPlacedCube(ray, p.world);
            if (t && (!best || *t < *best)) {
                best = t;
                hit = p.id;
            }
        }
        host.select(best ? std::vector<int>{hit} : std::vector<int>{});
    }

    void FrameSelection(game_production::SceneViewHost& host) {
        for (const Placed& p : placed_) {
            if (std::find(host.selection.begin(), host.selection.end(), p.id) == host.selection.end()) continue;
            const Vec3 center = space3d::TransformPoint(p.world, {0, 0, 0});
            const float size = space3d::Length(space3d::TransformDirection(p.world, {1, 1, 1}));
            space3d::Frame(cam_, center, std::max(size * 0.5f, 0.5f));
            return;
        }
    }

    void Render(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) {
        // The grid on the floor (y = 0) and the axes through the middle.
        lines_.clear();
        auto line = [&](Vec3 a, Vec3 b, ui::Color col) {
            lines_.push_back({a, col});
            lines_.push_back({b, col});
        };
        const float h = static_cast<float>(kGridHalf);
        for (int i = -kGridHalf; i <= kGridHalf; ++i) {
            const float v = static_cast<float>(i);
            const ui::Color minor = i % 10 == 0 ? theme::SceneGridMajor() : theme::SceneGrid();
            line({v, 0, -h}, {v, 0, h}, i == 0 ? theme::AxisZ() : minor);  // along Z (the line x = 0 is the Z axis)
            line({-h, 0, v}, {h, 0, v}, i == 0 ? theme::AxisX() : minor);  // along X
        }
        line({0, 0, 0}, {0, 1.5f, 0}, theme::AxisY());

        boxes_.clear();
        for (const Placed& p : placed_) {
            const bool selected = std::find(host.selection.begin(), host.selection.end(), p.id) != host.selection.end();
            const ui::Color col = selected ? theme::SceneObjectSelected() : p.active ? theme::SceneObject() : theme::SceneObjectInactive();
            boxes_.push_back({p.world, col});
            if (selected) Outline(p.world, theme::Accent);
        }

        // The picture in pixels of this window (sharp on high-DPI screens).
        ID2D1DeviceContext* dc = c.P().DC();
        float dpiX = 96, dpiY = 96;
        dc->GetDpi(&dpiX, &dpiY);
        const float scale = dpiX / 96.f;
        space3d::RenderFrame f;
        f.width = static_cast<uint32_t>(std::max(1.f, std::round(r.w * scale)));
        f.height = static_cast<uint32_t>(std::max(1.f, std::round(r.h * scale)));
        f.view = cam_.ViewMatrix();
        f.projection = cam_.ProjectionMatrix(r.w / r.h);
        f.clear = theme::SceneBackground();
        f.lines = lines_;
        f.boxes = boxes_;

        // Direct2D's drawing so far goes to the GPU first; then Direct3D draws into its own picture.
        dc->Flush();
        ID3D11Texture2D* picture = renderer_.Render(host.host.Graphics().D3DContext(), f);
        if (!picture) return;
        if (picture != bitmapTexture_ || dc != bitmapDc_ || !bitmap_) {
            bitmap_.Reset();
            Microsoft::WRL::ComPtr<IDXGISurface> surface;
            if (FAILED(picture->QueryInterface(IID_PPV_ARGS(&surface)))) return;
            const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpiX, dpiY);
            if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap_))) return;
            bitmapTexture_ = picture;
            bitmapDc_ = dc;
        }
        c.P().Image(bitmap_.Get(), r);
    }

    // The 12 edges of the placed unit cube.
    void Outline(const Mat4& w, ui::Color col) {
        Vec3 k[8];
        for (int i = 0; i < 8; ++i) {
            k[i] = space3d::TransformPoint(w, {(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f});
        }
        const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges) {
            lines_.push_back({k[e[0]], col});
            lines_.push_back({k[e[1]], col});
        }
    }

    void DrawHints(ui::Context& c, const Rect& r) {
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Bottom;
        c.P().Text(r.Inset(10, 8, 10, 8),
                   Tr(L"右ドラッグ: 見回す (W A S D / Q E で移動)   中ドラッグ: 平行移動   ホイール: 近づく   Alt+左ドラッグ: 回る   F: 選んだ物へ   クリック: 選ぶ"), ts);
        if (placed_.empty()) {
            ui::TextStyle hint = ts;
            hint.size = 12;
            hint.v = ui::VAlign::Top;
            hint.color = theme::TextFaint;
            c.P().Text(r.Inset(10, 8, 10, 8), Tr(L"Transform の付いたエンティティが、ここに箱で出ます。"), hint);
        }
    }

    enum class Drag { None, Look, Pan, Orbit, Click };

    space3d::OrbitView cam_;
    space3d::Renderer renderer_;
    bool tried_ = false;
    std::vector<Placed> placed_;
    std::vector<space3d::LineVertex> lines_;
    std::vector<space3d::Box> boxes_;

    Drag drag_ = Drag::None;
    float pressX_ = 0, pressY_ = 0;
    double lastTime_ = 0;

    // The picture as a Direct2D bitmap, made again when the picture or the window's context changes.
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    ID3D11Texture2D* bitmapTexture_ = nullptr;
    ID2D1DeviceContext* bitmapDc_ = nullptr;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateSceneView3D() { return std::make_unique<SceneView3D>(); }

}  // namespace rm::game_x_3d
