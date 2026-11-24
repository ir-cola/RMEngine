#pragma once
// 3D 空間: 空間を見る - ベクトル、カメラの行列、レイと、それが当たるもの。
// 純粋 - tests/TestSpace3d.cpp でテストする。
//
// ワールドは左手系で Y が上 (X が右、Y が上、Z が奥 - 2026-11-20 から。前は右手系)。カメラは自分の +Z を
// 向く。
// 射影行列は深度を 0 (近) .. 1 (遠) に写す。Direct3D の求める形。

#include "rm_engine/space3d/runtime/Transform.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace rm::space3d {

Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, float s);
float Dot(Vec3 a, Vec3 b);
Vec3 Cross(Vec3 a, Vec3 b);
float Length(Vec3 v);
Vec3 Normalize(Vec3 v);  // (0,0,0) は (0,0,0) のまま

// p' = M * (p, w): 点は w = 1 (動く)、方向は w = 0 (動かない)。
Vec3 TransformDirection(const Mat4& m, Vec3 d);
// 逆行列にできないときは nullopt: スケール 0 か、列が (ほぼ) 平面にある場合。列の長さと比べて
// 判断するので、ごく小さいもの (一様スケール 1e-6) でも逆にできる。
std::optional<Mat4> Inverse(const Mat4& m);

Mat4 Perspective(float fovYDegrees, float aspect, float nearZ, float farZ);
// ビュー空間のボックス (x 左..右、y 下..上、z nearZ..farZ: 目の前) -> -1..1、-1..1、0..1。
Mat4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ);
Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up);
// `world` に置かれたカメラのビュー行列: 自分の +Z を向き、+Y が上 (編集用カメラや LookAt と
// 同じ)。`world` のスケールは除く (絵が伸びてしまうため)。
Mat4 ViewFromWorld(const Mat4& world);
// ビュー射影が見ているものの 8 つの角 (ワールド): ニアプレーンが 0..3、ファープレーンが 4..7、
// それぞれ左下、右下、右上、左上。逆にできないときは nullopt。
std::optional<std::array<Vec3, 8>> FrustumCorners(const Mat4& viewProjection);

struct Ray {
    Vec3 origin;
    Vec3 dir;  // 正規化済み
};
// 画面の 1 点を通るレイ: ndc の x / y は -1 (左 / 下) から 1 (右 / 上)。
Ray ScreenRay(const Mat4& inverseViewProjection, float ndcX, float ndcY);
// レイの原点からボックスまでの、レイに沿った距離。外れたら nullopt。
std::optional<float> IntersectBox(const Ray& ray, Vec3 min, Vec3 max);
// `world` で置いた (動かし、回し、拡大した) 単位立方体 (-0.5 .. 0.5): ワールド単位の距離。
std::optional<float> IntersectPlacedCube(const Ray& ray, const Mat4& world);
// ものの自分の空間のボックス (モデルの境界) を `world` で置いたもの: ワールド単位の距離。
std::optional<float> IntersectPlacedBox(const Ray& ray, const Mat4& world, Vec3 min, Vec3 max);

// ものの自分の空間のボックスを `world` で置いたものを含む、ワールドの軸に沿ったボックス。
struct Bounds {
    Vec3 min{0, 0, 0}, max{0, 0, 0};
};
Bounds WorldBounds(const Mat4& world, Vec3 min, Vec3 max);

// 視錐台カリング: ワールドのボックスが `viewProjection` を通して見えるかもしれないか。8 つの角が
// 全部、見える範囲の同じ側 (左、右、下、上、ニアかファーのプレーン) の外にあるときだけ false。
// クリップ空間で調べる (そこでは各側が平面になる) ので、目の後ろの角も正しく数える。
// 見えるボックスには決して false を返さない。視錐台の角のすぐ外のボックスは true のこともある。
bool BoxInView(const Mat4& viewProjection, const Bounds& box);

// たくさんのボックスを 1 つのビューに対して同じように調べる: 6 つの面を平面として 1 度だけ
// 計算し (内側: a x + b y + c z + d >= 0)、各ボックスは各平面で一番内側にある角で調べる -
// 8 つの角を行列に通す代わりに、内積 6 回。BoxInView と同じ答えになる (ボックスが外なのは、
// 一番良い角 - だから全部の角 - が 1 つの面の外にあるとき)。
struct Frustum {
    std::array<std::array<float, 4>, 6> planes{};
};
Frustum FrustumOf(const Mat4& viewProjection);
bool BoxInFrustum(const Frustum& frustum, const Bounds& box);

// 透ける物を描く順: 目から一番遠いものを先に (それぞれの色は、後ろにあるものの上に重なる)。
// `centers` の番号を返す。距離が同じなら、与えられた順のまま。
std::vector<size_t> BackToFront(std::span<const Vec3> centers, Vec3 eye);

// 影マップ用の平行光源のビュー射影: `direction` (光が進む向き) を向き、ボックス `casters` (影を
// 落とすもの全部) を -1..1 x -1..1、深度 0..1 に収める。影は光に沿って落ちるので、影が落ちる
// 場所も同じ x / y の中にある。ボックスより遠い点は深度 1 を越える (シェーダーは 1 に留める:
// どの影の主よりも後ろ)。
Mat4 DirectionalShadowMatrix(Vec3 direction, const Bounds& casters);

// 点が画面のどこにあるか: ndc の x / y (-1 .. 1、y が上) と深度 (0 近 .. 1 遠)。
// 目の後ろにあるときは nullopt。
std::optional<Vec3> ProjectToNdc(const Mat4& viewProjection, Vec3 p);

// 見ている点 (ピボット) のまわりを回る編集用カメラ。エディターが覚えておけるよう reflect して
// ある (ユーザーの見方であって、プロジェクトのデータではない)。インスペクターには出さない。
//
// 平行投影 (`orthographic`: シーンビューの 2D): 奥へ行っても小さくならない。見える縦の半分は
// OrthoHalfHeight (= ピボットの距離で遠近法が見せる大きさ - 切り替えても絵の大きさが変わらない)。
// `distance` は大きさを決めるだけで、目は見えるもの全部の手前 (EyeDistance) に下がる。
RM_STRUCT()
struct OrbitView {
    RM_PROPERTY() Vec3 pivot{0, 0, 0};
    RM_PROPERTY(Min=-100000, Max=100000) float yaw = 35.f;  // Y まわりの度。0 は +Z を向く (増やすと左へ)
    RM_PROPERTY(Min=-89, Max=89) float pitch = 25.f;        // 度。正は下を向く
    RM_PROPERTY(Min=0.05, Max=10000) float distance = 9.f;  // 目からピボットまで (平行投影: 見える大きさ)
    RM_PROPERTY(Min=10, Max=120) float fovY = 60.f;         // 度
    RM_PROPERTY() bool orthographic = false;                // 平行投影

    Vec3 Forward() const;  // 目が見る向き
    Vec3 Right() const;
    Vec3 Up() const;
    // 目からピボットまで: 遠近法は `distance`。平行投影は、ピボットの前後の物が切れないよう遠くに下がる。
    float EyeDistance() const;
    Vec3 Eye() const { return pivot - Forward() * EyeDistance(); }
    // 点 p を見る目 (p から目への向きを取るもの: ギズモの輪)。遠近法は Eye()。平行投影はどの点も
    // 真正面から見るので、p のちょうど手前。
    Vec3 EyeFor(Vec3 p) const;
    // 平行投影で見える縦の半分 (ワールドの長さ)。
    float OrthoHalfHeight() const;
    Mat4 ViewMatrix() const { return LookAt(Eye(), pivot, {0, 1, 0}); }
    Mat4 ProjectionMatrix(float aspect) const;
    // `viewHeight` ピクセルの高さの絵の 1 ピクセルが、点 p でどれだけの長さか (距離に関係なく画面で
    // 同じ大きさに描くもの: ギズモ)。p が目の後ろなら 0。平行投影はどこでも同じ。
    float WorldPerPixel(Vec3 p, float viewHeight) const;
};

// カメラの動き (どのマウス / キーでどれをするかはエディターが決める):
void Look(OrbitView& v, float dYaw, float dPitch);       // 首を振る: 目はそのまま、ピボットが動く
void Orbit(OrbitView& v, float dYaw, float dPitch);      // ピボットのまわりを回る: ピボットはそのまま
void Pan(OrbitView& v, float dxPixels, float dyPixels, float viewHeightPixels);  // 絵と一緒にずらす
void Dolly(OrbitView& v, float notches);                 // ピボットへ近づく (> 0)、または離れる
// ホイール (平行投影): 見える大きさを変え、マウスの下の点 (ndc の x / y: -1 .. 1、y が上) は画面の
// 同じ所に残る。遠近法では Dolly と同じ。
void ZoomAt(OrbitView& v, float notches, float ndcX, float ndcY, float aspect);
void Fly(OrbitView& v, Vec3 move);                       // x = 右、y = 上 (ワールド)、z = 前: 目もピボットも動く
void Frame(OrbitView& v, Vec3 target, float radius);     // ものが収まる距離から、そのものを見る

}  // namespace rm::space3d
