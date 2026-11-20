#pragma once
// 2D 空間: 平面を見る - カメラ (どこを、どれだけ近くで見るか) と、画面の点が触れるもの。
// 純粋 (I/O も描画もしない)。tests/TestSpace2d.cpp でテストする。
//
// 画面は Y が下向きの DIP の長方形。平面は Y が上 (Transform2D.h)。カメラは `center` を長方形の
// 真ん中に置き、平面の 1 単位を `zoom` DIP にして、`rotation` だけ回す (カメラを反時計回りに回すと
// 平面は時計回りに回って見える。編集用のカメラは回らない。ゲームのカメラはエンティティと一緒に回る)。

#include "rm_engine/space2d/runtime/Transform2D.h"

namespace rm::space2d {

// 画面の長方形 (DIP)。ランタイムが UI のものを必要としないよう、専用の型にしてある。
struct ScreenRect {
    float x = 0, y = 0, w = 0, h = 0;
};

RM_STRUCT()
struct Camera2D {
    RM_PROPERTY() Vec2 center{0, 0};
    RM_PROPERTY(Min=0.5, Max=20000) float zoom = 100.f;  // 1 単位あたりの DIP (100: 1 単位 100 ピクセルのスプライトが 1:1 で見える)
    RM_PROPERTY() float rotation = 0;  // 度、反時計回り
};

inline constexpr float kMinZoom = 0.5f;
inline constexpr float kMaxZoom = 20000.f;

Vec2 ToScreen(const Camera2D& cam, const ScreenRect& r, Vec2 world);
Vec2 ToWorld(const Camera2D& cam, const ScreenRect& r, Vec2 screen);
// ワールド -> 画面を行列にしたもの (回転・拡大縮小した物を描くため)。
Mat3 ScreenMatrix(const Camera2D& cam, const ScreenRect& r);

// 絵がマウスについてくる: 画面上で (dx, dy) DIP 動いた。
void Pan(Camera2D& cam, float dx, float dy);
// 近く (notches > 0) か遠くへ。`screen` の下にある平面の点はそのまま動かさない。
void ZoomAt(Camera2D& cam, const ScreenRect& r, Vec2 screen, float notches);
// 平面の箱 (その中心と大きさの半分) がビューの約 2/3 を占めるように映す。
void Frame(Camera2D& cam, const ScreenRect& r, Vec2 center, Vec2 halfSize);

// 平面の単位での格子の刻み: 10 の累乗の 1、2、5 倍で、線の間が画面上で `minDips` 以上になる一番小さいもの。
// `major`: この本数ごとに 1 本を濃くする (いつも 5 か 10)。
struct GridStep {
    float step = 1;
    int major = 10;
};
GridStep GridFor(float zoom, float minDips = 12.f);

// 平面の点が、`world` で置かれた (動かし、回し、拡大縮小し、反転した) 箱 (物自身の空間の min .. max) の
// 中にあるか。
bool InsidePlacedBox(Vec2 point, const Mat3& world, Vec2 min, Vec2 max);
// その箱のワールドでの 4 つの角。(min.x, min.y) から反時計回り。
void PlacedBoxCorners(const Mat3& world, Vec2 min, Vec2 max, Vec2 out[4]);

}  // namespace rm::space2d
