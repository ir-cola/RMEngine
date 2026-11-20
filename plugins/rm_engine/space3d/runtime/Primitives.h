#pragma once
// 3D 空間 - ファイル無しに数から作る単純な形: 箱, 球, 円柱, カプセル, 平面。箱は、モデルの無いもの
// (Box) にレンダラーが描くもの。ゲーム制作 × 3D の メッシュ は、どれでも描く (その 形)。どれも
// (0, 0, 0) を中心に、Y が上:
//
//   Box       1 x 1 x 1
//   Sphere    直径 1 (半径 0.5)
//   Cylinder  直径 1、高さ 1 (両端にふた)
//   Capsule   直径 1、高さ 2 (両端に半球)
//   Plane     10 x 10、y = 0 で平ら、表が上 (地面: Unity の plane と同じ)
//
// 法線は外を向く (丸い面はスムーズ。箱の面、ふた、平面は平ら)。各三角形の表は、外から見て
// 反時計回り。UV は各面で 0..1 (丸い面: 1 周で)。パーツは 1 つで、マテリアル無し (描画の色で描く)。
// 純粋 - tests/TestSpace3d.cpp でテストする。

#include "rm_engine/space3d/runtime/Mesh.h"

namespace rm::space3d {

enum class Shape { Box, Sphere, Cylinder, Capsule, Plane };

MeshData ShapeMesh(Shape shape);

// すべての三角形を、表 (反時計回り) が角の法線の向く側を向くように回す。
void FaceOut(MeshData& mesh);

}  // namespace rm::space3d
