#pragma once
// 3D 空間 - ギズモ: 3D ビューでドラッグすると、物を動かし・回し・拡大縮小する矢印・輪・箱。ここにあるのは
// 計算だけ: ハンドルがどこにあるか (同じ形を描いて当たり判定にも使う - 憲法 3)、どれがマウスの下か、
// ドラッグが Transform に何をするか。描くのはビューの仕事。3D 空間を編集するソフトの全てが使う
// (今はゲームのシーンビュー、あとで 3D モデルのソフト)。純粋 - tests/TestSpace3d.cpp でテストする。
//
//   auto f = gizmo::MakeFrame(world, tool, space, camera, viewHeight);     // どこにあるか
//          (または MakeFrame(center, axes, ...): 複数のものの真ん中。Pivot::Center)
//   gizmo::Handle h = gizmo::HitTest(tool, *f, screen, camera.Eye(), mouse);
//   auto drag = gizmo::BeginDrag(tool, h, *f, screen, camera.Eye(), mouse);  // 押したとき
//   auto change = gizmo::Update(*drag, screen, mouse, snap);                // 動くたびに
//   transform = gizmo::Apply(start, parentWorld, startWorld, *change, space, transform.rotation, pivot);

#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rm::space3d::gizmo {

RM_ENUM()
enum class Tool { Move, Rotate, Scale };
// 移動 / 回転のハンドルの向き: 物自身の軸か、ワールドの軸。(スケールはいつも物自身の軸に沿う -
// Transform のスケールがそうだから。)
RM_ENUM()
enum class Space { Local, World };
// 複数選んだときにギズモが置かれる所と、何のまわりに回す / 拡大縮小するか: それぞれ自分の場所のまわり
// (ギズモは最後に選んだ 1 つに付く)、または全部が、それらの真ん中のまわり (ギズモもそこ)。
RM_ENUM()
enum class Pivot { Each, Center };

enum class Handle : uint8_t {
    None,
    X, Y, Z,       // 矢印 / 輪 / スケールの軸
    YZ, ZX, XY,    // 面の中を動かす四角 (動く 2 つの軸の名前で呼ぶ)
    Uniform,       // 真ん中の、均等に拡大縮小する箱
    View,          // 回転: 目の方を向いた外側の輪 - 視線のまわりに回す
    Free,          // 回転: 球の中 (輪の上ではない) - トラックボールのように自由に回す
};
// ハンドルの軸: X / Y / Z -> 0 / 1 / 2、面 -> 動かない方の軸、それ以外は -1。
int AxisOf(Handle h);

// 置かれたままのギズモ: `origin` に、単位の 3 軸 (ワールド)、長さ `size` の矢印 (ワールド単位)。
// それは画面上の `pixels` (距離によらず画面上で同じ大きさに保つ)。
struct Frame {
    Vec3 origin{0, 0, 0};
    std::array<Vec3, 3> axes{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    float size = 1.f;
    float pixels = 90.f;
};
std::array<Vec3, 3> AxesOf(const Mat4& world, Tool tool, Space space);
// `world` で置かれた物のギズモ。物が目の後ろなら nullopt。
std::optional<Frame> MakeFrame(const Mat4& world, Tool tool, Space space, const OrbitView& view, float viewHeight,
                               float pixels = 90.f);
// `origin` にこの軸で置いたギズモ (複数のものの真ん中、またはそのうち 1 つの軸)。
std::optional<Frame> MakeFrame(Vec3 origin, const std::array<Vec3, 3>& axes, const OrbitView& view, float viewHeight,
                               float pixels = 90.f);

// 画面上のビュー: 画面の矩形 (DIP。y が下向き) とカメラの行列。
struct Screen {
    Mat4 viewProjection;
    Mat4 inverse;  // viewProjection の逆
    float x = 0, y = 0, w = 1, h = 1;

    std::optional<Vec2> ToScreen(Vec3 p) const;  // nullopt: 目の後ろ
    Ray RayAt(Vec2 p) const;
};
std::optional<Screen> MakeScreen(const Mat4& viewProjection, float x, float y, float w, float h);

// --- 形 (描くのも当たり判定も同じ) -------------------------------------------------------------
inline constexpr float kHeadStart = 0.8f;                 // 矢印の頭: size の 0.8 から 1 まで
inline constexpr float kPlaneNear = 0.2f, kPlaneFar = 0.42f;  // 面の四角: size の 0.2 .. 0.42
Vec3 AxisEnd(const Frame& f, int axis);
std::array<Vec3, 4> PlaneQuad(const Frame& f, Handle plane);
// `axis` のまわりの回転ハンドルの輪を、目から背く所で切った折れ線にしたもの:
// front = 目の方を向いた半分 (強く描き、先につかむ)、それ以外は裏の半分 (薄く描く。どの輪も
// 1 つの円に読めるように。表の輪がもっと近くない所でだけつかむ)。
std::vector<std::vector<Vec3>> RingArcs(const Frame& f, int axis, Vec3 eye, bool front = true, int segments = 64);
inline constexpr float kBackPenalty = 5.f;  // ピクセル: 輪の裏の半分は、これだけ遠いものとしてつかむ
// ギズモのまわりの、目の方を向いた円。半径は `radius` x size (閉じている: 最後の点が最初の点)。
// kViewRing = 外側の輪 (Handle::View)。1 = 球の輪郭で、その内側のドラッグは自由に回す。
inline constexpr float kViewRing = 1.18f;
// 拡大縮小のドラッグ (1 軸でも 3 軸でも) は、物を、始めの大きさのこの倍数より小さくしない:
// 0 にせず (つかめなくなる、また反転する)、裏返しにもしない。
inline constexpr float kMinScaleFactor = 0.01f;
std::vector<Vec3> FacingCircle(const Frame& f, Vec3 eye, float radius, int segments = 64);
// ほぼ真っすぐ目の方を向いた軸は描かず、つかまない (点になってしまう)。
bool AxisShown(const Frame& f, const Screen& s, int axis);
// ... 真横からほぼ見ている面の四角も同じ。四角の角の画面上の位置を `out` に入れる。
bool PlaneShown(const Frame& f, const Screen& s, Handle plane, std::array<Vec2, 4>* out = nullptr);

// マウスの下のハンドル (線のまわり `tolerance` ピクセル)。無ければ None。
Handle HitTest(Tool tool, const Frame& f, const Screen& s, Vec3 eye, Vec2 mouse, float tolerance = 7.f);

// --- ドラッグ --------------------------------------------------------------------------------------
struct Drag {
    Tool tool = Tool::Move;
    Handle handle = Handle::None;
    Frame frame;
    Vec2 startMouse;
    Vec3 startPoint;      // 移動 / スケール: 軸や面をつかんだ所
    float startT = 0;     // 軸に沿った移動 / スケール: 軸に沿ってどれだけか
    Vec2 tangent;         // 回転: 前向きに回る画面上の向き (単位)
    Vec3 axis{0, 0, 0};   // 回転: 回す軸 (ワールド)。Free: 視線 (目の方へ)
    Vec3 right{0, 0, 0}, up{0, 0, 0};  // Free: 画面の右と上 (ワールド)
};
// ここからはそのハンドルをつかめない (軸や面にそって真っすぐ見ている) ときは nullopt。
std::optional<Drag> BeginDrag(Tool tool, Handle handle, const Frame& f, const Screen& s, Vec3 eye, Vec2 mouse);

// Ctrl を押している間: なめらかに変えず、段階で変える。
struct Snap {
    bool on = false;
    float move = 0.25f;    // ワールド単位
    float degrees = 15.f;
    float scale = 0.1f;    // 始めのスケールに対する割合
};

// ここまでにドラッグがしたこと (始めから見て)。
struct Change {
    Tool tool = Tool::Move;
    Vec3 move{0, 0, 0};  // 移動: どれだけ (ワールド)
    int axis = -1;       // 回転: フレームの軸のまわり (-1: View / Free)。スケール: その成分 (-1: 3 つ全部)
    Vec3 worldAxis{0, 0, 0};  // 回転: 回す軸。スケール: 拡大縮小する軸 (ワールド、単位)
    float degrees = 0;   // 回転
    float factor = 1;    // スケール
};
// マウスの光線が軸 / 面に当たらないときは nullopt (最後の変化を保つ)。
std::optional<Change> Update(const Drag& d, const Screen& s, Vec2 mouse, const Snap& snap);

// 変化のあとの物の Transform。start / startWorld: ドラッグが始まったときのその Transform と
// ワールド行列。parentWorld: その親のワールド行列 (複数のものを同時にドラッグできる)。
// `hint`: いま見せている回転 (角度が途切れないように)。`pivot` が無ければ、各自が自分の場所のまわりに
// 回る / 拡大縮小する。輪は自分の軸 (Local) かワールドの軸のまわり。`pivot` (Pivot::Center) があれば、
// 全部がその点を通るギズモの軸のまわりに回り、点から離れる向きに拡大縮小する (場所も動く)。
Transform Apply(const Transform& start, const Mat4& parentWorld, const Mat4& startWorld, const Change& c, Space space,
                Vec3 hint, const std::optional<Vec3>& pivot = std::nullopt);

// ギズモが要る計算を、単独でテストする。
// `origin` から `axis` (単位) に沿って、光線に一番近い点。平行なら nullopt。
std::optional<float> ClosestOnLine(const Ray& ray, Vec3 origin, Vec3 axis);
std::optional<Vec3> IntersectPlane(const Ray& ray, Vec3 point, Vec3 normal);
float DistanceToSegment(Vec2 p, Vec2 a, Vec2 b);

}  // namespace rm::space3d::gizmo
