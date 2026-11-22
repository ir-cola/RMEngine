#pragma once
// 2D 空間: 平面上のものがどこにあるかと、その計算 (見るのは View2D.h、描くのは
// Renderer2D.h - 同じプラグイン)。2D の空間を持つソフトすべてで共有する (ゲームはコネクター
// 「ゲーム制作 × 2D」を通して、イラストのソフトはあとで) - 設計 RME_PLATFORM 4-b。
// 純粋: 描画もファイルもない - tests/TestSpace2d.cpp でテストする。
//
// 平面は X が右、Y が上 (Unity の 2D や数学と同じ)。正の角度は反時計回り。
// 画面の Y は下向き: 反転するのはカメラ (View2D.h) だけ。
// 奥行きの代わりに、物は 並び順 (order) を持つ: 大きいほうが手前。子の order は
// 親の order に足される (3D の子の Z が親の Z に足されるのと同じ)。だからグループはまとめて手前に出る。

#include "core/reflect/Reflect.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace rm::space2d {

RM_STRUCT(Label="Transform 2D (位置・回転・大きさ・並び順)")
struct Transform2D {
    RM_PROPERTY(Edit, Label="位置", Icon="Position") Vec2 position{0, 0};
    RM_PROPERTY(Edit, Label="回転 (度)", Icon="Rotation", Tooltip="反時計回りが + です") float rotation = 0;
    RM_PROPERTY(Edit, Label="大きさ", Icon="Scale") Vec2 scale{1, 1};
    RM_PROPERTY(Edit, Label="並び順", Tooltip="大きいほど手前に描きます (奥行きの代わり)。子は親の並び順に足した所に描きます", Min=-100000, Max=100000)
    int order = 0;
};

Vec2 operator+(Vec2 a, Vec2 b);
Vec2 operator-(Vec2 a, Vec2 b);
Vec2 operator*(Vec2 a, float s);
float Dot(Vec2 a, Vec2 b);
float Cross(Vec2 a, Vec2 b);  // a.x * b.y - a.y * b.x: b が a から見て反時計回りのとき > 0
float Length(Vec2 v);
Vec2 Normalize(Vec2 v);  // (0,0) は (0,0) のまま
Vec2 Rotate(Vec2 v, float degrees);

// 2D のアフィン行列 (列ベクトル): p' = (a*x + c*y + tx, b*x + d*y + ty)。
// (a, b) は X 軸の行き先、(c, d) は Y 軸、(tx, ty) は原点。
struct Mat3 {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};

Mat3 operator*(const Mat3& m, const Mat3& n);  // n の後に m
Vec2 TransformPoint(const Mat3& m, Vec2 p);
Vec2 TransformDirection(const Mat3& m, Vec2 v);  // 動かさない
float Determinant(const Mat3& m);                // < 0: 反転している
std::optional<Mat3> Inverse(const Mat3& m);      // 逆行列にできないとき nullopt (拡大率 0)

Mat3 Translation(Vec2 t);
Mat3 Rotation(float degrees);
Mat3 Scaling(Vec2 s);

// 拡大縮小、回転、移動の順: T * R * S。
Mat3 LocalMatrix(const Transform2D& t);
// 親の空間の中: parentWorld * LocalMatrix(t) (一番上の親は単位行列)。
inline Mat3 WorldMatrix(const Mat3& parentWorld, const Transform2D& t) { return parentWorld * LocalMatrix(t); }

// 物がワールドのどこにあるか (親も含む) - Transform 2D の インスペクターの ワールド の表示。
RM_STRUCT(Label="ワールドでの値 (親を含む)")
struct WorldPlacement2D {
    RM_PROPERTY(Edit, Label="位置", Icon="Position") Vec2 position{0, 0};
    RM_PROPERTY(Edit, Label="回転 (度)", Icon="Rotation", Tooltip="反時計回りが + です") float rotation = 0;
    RM_PROPERTY(Edit, Label="大きさ", Icon="Scale", Tooltip="親の大きさが軸ごとに違い、しかも回っているときは、ゆがみが出るので近い値です") Vec2 scale{1, 1};
    RM_PROPERTY(Edit, Label="並び順", Tooltip="親の並び順を足した値です", Min=-100000, Max=100000) int order = 0;
};
// ワールド行列を位置・回転・拡大縮小に分解する。回転は `hintDegrees` に近いもの (360 の整数倍で選ぶ。
// ドラッグ中に数字が飛ばないように)。反転した行列は Y の拡大率が負になる。`order` は行列にないので 0。
WorldPlacement2D Decompose(const Mat3& world, float hintDegrees = 0);
// 親が `parentWorld` にあり order が `parentOrder` のとき、物をワールドの `want` に置く Transform 2D。
// `current` の今の状態から `want` が変えるものだけを変える (ワールドの X 位置を打ち込んでも、動くだけで回らない)。
// 親が不均等に拡大されて回転しているとき以外は正確 (その場合は一番近いもの)。
Transform2D FromWorld(const Transform2D& current, const Mat3& parentWorld, int parentOrder, const WorldPlacement2D& want);

// --- 何が何の手前に描かれるか ---------------------------------------------------------
// 描く物 1 つ: ワールドの order (Transform 2D のもの、親のを足したもの)、自分の描画の
// order (同じワールド order の中でのスプライトの 並び順)、ツリーでの位置 (最後の決め手:
// 階層 で後ろのほうが手前、Unity と同じ)。
struct DrawKey {
    int order = 0;
    int subOrder = 0;
    size_t tree = 0;
};
// `keys` の番号を奥 (先に描く) から手前へ並べたもの。
std::vector<size_t> DrawOrder(std::span<const DrawKey> keys);

}  // namespace rm::space2d
