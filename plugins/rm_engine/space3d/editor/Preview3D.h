#pragma once
// 3D 空間 - パネルの中の、それ自身の小さな 3D の画面 (パーティクルタブのプレビュー。あとでアニメーターや
// アセットのもの): 床の格子、マウスで回すカメラ (左ドラッグ: 回す、右 / 中ドラッグ: 動かす、ホイール:
// 近づく / 離れる)、呼び出し側が入れたもの (モデル、ExtraDraw)。
//
// 専用のレンダラーと画面を持つ (GPU のデバイスが作り直されたら作り直す: platform::Gfx::Generation)。

#include "platform/Gfx.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/View.h"
#include "ui/Context.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <span>
#include <string_view>
#include <vector>

namespace rm::space3d {

class Preview3D {
public:
    Preview3D();

    struct Content {
        std::span<const MeshDraw> meshes;
        std::span<const ExtraDraw> extras;
        // 床の格子をずらす (XZ。物が動く代わりに床が流れる: アニメーションのタブのルートモーション)。
        Vec3 gridShift{0, 0, 0};
    };
    // r に描く (その上のマウスも扱う: `id` でドラッグを区別する)。3D を描けないときは false。
    bool Draw(ui::Context& c, const ui::Rect& r, platform::Gfx& gfx, std::string_view id, const Content& content);
    OrbitView& Camera() { return camera_; }
    // このプレビューのレンダラー (形を GPU に上げる: Renderer::Upload)。描けないときは null。GPU のデバイスが
    // 作り直されると Generation が変わる: その前に上げた形は捨てて上げ直す。
    Renderer* Gpu(platform::Gfx& gfx);
    uint64_t Generation() const { return generation_; }

private:
    Renderer renderer_;
    bool tried_ = false;
    uint64_t generation_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    ID3D11Texture2D* texture_ = nullptr;
    ID2D1DeviceContext* dc_ = nullptr;
    OrbitView camera_;
    bool panning_ = false;
    std::vector<LineVertex> grid_;
    std::vector<LineVertex> shifted_;  // ずらした格子 (Content::gridShift)
    bool gridLight_ = false;  // 格子の色を取ったテーマ
};

}  // namespace rm::space3d
