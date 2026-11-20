#pragma once
// パーティクル - エミッター 1 つのシェーダーを、そのモジュールから組み立てたもの (純粋なテキスト:
// tests/TestParticles.cpp でテストし、その結果をそこでコンパイルもする)。
//
//   struct Particle { 標準の値 ...、各モジュールの attr }
//   namespace M0 { static <params>; <モジュール 0 のコード> }    各モジュールが自分の名前空間に入る: 名前が
//   namespace M1 { ... }                                   他のものとぶつからない ("first" が 2 つあってもよい)
//   SpawnMain   (cs_5_0)  新しい粒: 標準の始め方、そのあと各モジュールの Spawn を順に
//   UpdateMain  (cs_5_0)  各モジュールの Update を順に、そのあとエンジンが動かし (速度、回転)、年を取らせ、
//                         トレイルを持つ (TrailPoints 個の場所、リング)
//   ParticleVS / ParticlePS  カメラに向いた四角 (または速度の向きに長く: Stretch)、点の形、その色
//   TrailVS / TrailPS     トレイルの場所を通る帯。端へ向かって細く薄くなる
//
// モジュールはエンジンの CollideShapes (コライダーの形: CollisionShape) と CollideDepth (絵の深度) を
// 呼んでよい - 組み込みの 当たる モジュールはそうしている。
//
// モジュールの param は定数バッファー 1 つに入る (param ごとに float4 を 1 つ、順に): 値が変わっても
// バッファーが変わるだけ (PackParams) - シェーダーを作り直すのはモジュールが変わった時だけ。

#include "rm_engine/particles/runtime/ModuleSource.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rm::particles {

struct ModuleInput {
    const ModuleSource* source = nullptr;
    std::string file;  // コンパイラーのメッセージ用 ("Wind.rmfx"、"initial")
    std::span<const ParticleValue> values;
};

struct EmitterShader {
    std::string hlsl;
    uint32_t stride = 0;      // Particle 1 つのバイト数
    uint32_t paramSlots = 0;  // cbuffer Params の float4 の数 (最低 1)
    std::vector<ModuleError> errors;  // 同じ名前で型が違う attr が 2 つ
};

EmitterShader AssembleShader(std::span<const ModuleInput> modules);
// cbuffer Params: param ごとに float 4 個、モジュールの順に (与えられていない値: その param の初期値)。
std::vector<float> PackParams(std::span<const ModuleInput> modules);

inline constexpr uint32_t kThreads = 64;  // 2 つのカーネルの [numthreads]

// シェーダーが読む通りの定数バッファー (b0 / b2)。
struct EmitterConstants {
    float world[16];     // エミッターのワールド行列 (列優先、space3d の Mat4 と同じ)
    float position[3];
    float time;          // エフェクトが始まってからの秒数
    float dt;
    uint32_t spawnStart;  // 新しい粒が入る最初の枠
    uint32_t spawnCount;
    uint32_t capacity;
    uint32_t spawnSerial;  // それまでに生まれた数 (乱数の種)
    float age;             // このラウンドに入ってからの秒数 (長さ)
    uint32_t shape;        // ParticleShape
    float pad = 0;
    uint32_t trailPoints;  // 0: トレイル無し
    float trailInterval;   // 場所と場所の間の秒数
    float trailWidth;      // 粒の大きさの何倍か
    float stretch;         // 秒: 粒の長さは、速さにこれをかけたもの (0: 四角)
    uint32_t hasImage;     // 点がエミッターの絵を見せる
    uint32_t sheetCols, sheetRows;  // その絵のフレーム
    uint32_t sheetMode;    // ParticleSheet
};
static_assert(sizeof(EmitterConstants) == 144);

struct ViewConstants {
    float viewProj[16];  // 列優先: mul(ViewProj, v)
    float right[3];      // ワールドでのカメラの右と上 (四角はそちらを向く)
    float pad0 = 0;
    float up[3];
    float pad1 = 0;
    float eye[3];  // カメラのある所
    float pad2 = 0;
};
static_assert(sizeof(ViewConstants) == 112);

// 粒が当たる形 (CollideShapes)。ワールドにあり、シェーダーが読む通りの形で持つ。
enum class CollisionType : uint32_t { Sphere = 0, Box = 1, Capsule = 2, Plane = 3 };
struct CollisionShape {
    float center[3];
    CollisionType type;
    float axisX[3];  // 箱の軸 (単位); カプセルの線は axisY に沿う; 平面の法線は axisY
    float sizeX;     // 箱の半分の大きさ; 球 / カプセルの半径
    float axisY[3];
    float sizeY;     // カプセル: 線の長さの半分
    float axisZ[3];
    float sizeZ;
};
static_assert(sizeof(CollisionShape) == 64);

// 絵の直近のフレームが描いたもの (CollideDepth がその深度を読む)。
struct CollideConstants {
    float viewProj[16];
    float inverse[16];  // クリップ -> ワールド
    float eye[3];
    uint32_t count;     // 形の数
    float size[2];      // ピクセル
    uint32_t depthMode;  // 0 無し、1 サンプル 1 つ、2 マルチサンプル
    float pad = 0;
};
static_assert(sizeof(CollideConstants) == 160);

}  // namespace rm::particles
