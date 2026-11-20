#pragma once
// 2D 空間 - 2D ビューのギズモ: ドラッグして物を動かし、回し、拡大縮小する矢印・輪・箱。
// ここにあるのは計算だけ: ハンドルが画面のどこにあるか (描くのと当たり判定は同じ形 - 憲法 3)、
// どれがマウスの下にあるか、ドラッグが Transform 2D に何をするか。描くのはビューの仕事。
// 2D の空間を編集するソフトすべてが使う (今はゲームの 2D シーンビュー、あとでイラストのソフト)。
// 純粋 (I/O も描画もしない)。tests/TestSpace2d.cpp でテストする。
//
//   gizmo2d::Frame f = gizmo2d::MakeFrame(world, tool, space);
//   gizmo2d::Handle h = gizmo2d::HitTest(tool, f, camera, rect, mouse);
//   gizmo2d::Drag drag = gizmo2d::BeginDrag(tool, h, f, camera, rect, mouse);   // 押したとき
//   gizmo2d::Change ch = gizmo2d::Update(drag, camera, rect, mouse, snap);     // 動かすたび
//   transform = gizmo2d::Apply(start, parentWorld, startWorld, ch);          // (または &f を付けて: 中心まわり)
//
// ギズモは、カメラがどれだけ近くても画面上で同じ大きさ (DIP) を保つ。

#include "rm_engine/space2d/runtime/View2D.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace rm::space2d::gizmo2d {

RM_ENUM()
enum class Tool { Move, Rotate, Scale };
// 矢印の向き: 物自身の軸か、ワールドの軸か。(拡大縮小はいつも物自身の軸に沿う -
// Transform 2D の拡大率がそうだから。)
RM_ENUM()
enum class Space { Local, World };
// 複数を選んだときにギズモが置かれる所と、回転・拡大縮小の中心: 各自の位置まわり
// (ギズモは最後に選んだものの所)、または全体の真ん中まわり (ギズモもそこ)。
RM_ENUM()
enum class Pivot { Each, Center };

enum class Handle : uint8_t {
    None,
    X, Y,   // 矢印 / 拡大縮小の軸
    Free,   // 真ん中の四角: 自由に動かす / 均等に拡大縮小する
    Ring,   // 回転: 円
};

inline constexpr float kArrowLength = 80.f;  // 画面上の DIP
inline constexpr float kRingRadius = 70.f;
inline constexpr float kCenterHalf = 9.f;    // 真ん中の四角の半分
inline constexpr float kHitSlop = 7.f;       // マウスが線にどれだけ近ければよいか
inline constexpr float kScalePerDip = 0.01f; // 拡大縮小の真ん中の四角: 右 / 上へ 100 DIP で 2 倍

// 置かれたままのギズモ: `origin` (平面上) に、単位長さの軸 2 本 (平面上)。
struct Frame {
    Vec2 origin{0, 0};
    std::array<Vec2, 2> axes{Vec2{1, 0}, Vec2{0, 1}};
};
Frame MakeFrame(const Mat3& world, Tool tool, Space space);
// 複数の位置の真ん中 (中心)。軸は `axesFrom` (最後に選んだもの) のもの。
Frame CenterFrame(std::span<const Vec2> places, const Frame& axesFrom);

// 画面上: ギズモの真ん中と、各矢印の先。
struct ScreenShape {
    Vec2 origin;
    std::array<Vec2, 2> tips;
};
ScreenShape ShapeOf(const Frame& f, const Camera2D& cam, const ScreenRect& r);

// マウスの下のハンドル (画面の DIP)。
Handle HitTest(Tool tool, const Frame& f, const Camera2D& cam, const ScreenRect& r, Vec2 mouse);

struct Drag {
    Tool tool = Tool::Move;
    Handle handle = Handle::None;
    Frame frame;
    Vec2 startWorld{0, 0};   // ドラッグを始めたときの、平面上のマウス
    Vec2 startScreen{0, 0};  // ... と画面上のマウス
    float lastRaw = 0;       // 回転: 前回のマウスの角度 (何回転したか数えるため)
    float turned = 0;        // 回転: ここまでの度数 (180 を超えることもある)
};
// `handle` (None 以外) のドラッグ。マウスのある所から始まる。
Drag BeginDrag(Tool tool, Handle handle, const Frame& f, const Camera2D& cam, const ScreenRect& r, Vec2 mouse);

// ステップ (Ctrl): 各軸に沿って `move` 単位ずつ動き、`degrees` ずつ回り、開始時の `scale` 刻みで拡大縮小する。
struct Snap {
    bool on = false;
    float move = 0.25f;
    float degrees = 15.f;
    float scale = 0.1f;
};

// ここまでにドラッグがしたこと (開始から見て)。
struct Change {
    Vec2 move{0, 0};      // 平面上 (ワールド)
    float degrees = 0;    // 反時計回り
    Vec2 scale{1, 1};     // 開始時の何倍か。物自身の軸に沿う
};
Change Update(Drag& drag, const Camera2D& cam, const ScreenRect& r, Vec2 mouse, const Snap& snap);

// 変更後の Transform 2D (ドラッグを始めたときの状態から)。移動はワールドで行い、親の逆行列を通す。
// 回転は、反転した親の下では向きを逆にする。`pivot` (中心): 回転は位置もギズモの真ん中のまわりに運び、
// 拡大縮小は位置をギズモの軸に沿って真ん中から遠ざける。
Transform2D Apply(const Transform2D& start, const Mat3& parentWorld, const Mat3& startWorld, const Change& change,
                  const Frame* pivot = nullptr);

}  // namespace rm::space2d::gizmo2d
