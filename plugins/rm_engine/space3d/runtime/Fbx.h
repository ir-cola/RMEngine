#pragma once
// 3D 空間 - reading FBX (Autodesk's format, written by Blender, Maya, 3ds Max, many asset stores).
// Pure: the bytes are given - tested in tests/TestSpace3d.cpp. Written here without the FBX SDK
// (its licence does not allow it to ship inside the engine freely, and only the shape is needed).
//
//   Binary FBX 7.x (7100 .. 7700; what Blender and the FBX SDK write) and ASCII FBX 7.x. Both are a
//   tree of nodes (a name, properties, children); the big arrays of binary files are often zlib-
//   compressed (Inflate.h).
//   Read: every mesh (Geometry) placed by its models (Model: Lcl Translation / Rotation / Scaling,
//   pre / post rotation, pivots, rotation order, the geometric offset) and their parents, turned to
//   this engine's axes (GlobalSettings: UpAxis / FrontAxis / CoordAxis) and metres (UnitScaleFactor
//   is centimetres per unit); normals and the first UV set (by polygon vertex / by control point,
//   direct / by index); materials (DiffuseColor x DiffuseFactor, Opacity, EmissiveColor x
//   EmissiveFactor - taken as linear light, as Blender writes them) and the picture on their diffuse
//   colour (a file next to the model, or the picture written inside: Video's Content).
//   Polygons are cut into triangles from their first corner.
//   Not read: skins, blend shapes, animation, cameras and lights, NURBS, FBX 6 and older.

#include "rm_engine/space3d/runtime/Mesh.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::space3d {

std::optional<MeshData> ParseFbx(std::span<const uint8_t> bytes, std::string* error);

// The node tree (both kinds of FBX read into this), tested on its own.
struct FbxProperty {
    char type = 0;               // binary's letter: Y C I L (whole numbers) F D (numbers) S R (text / bytes) f d i l b (arrays)
    int64_t integer = 0;         // a whole number (ids are 64-bit)
    double number = 0;           // any number (whole numbers too)
    std::string text;            // S (a name "Model::Cube": binary's "Cube\0\1Model" is turned around) and R (bytes)
    std::vector<double> numbers; // arrays
};

struct FbxNode {
    std::string name;
    std::vector<FbxProperty> props;
    std::vector<FbxNode> children;

    const FbxNode* Child(std::string_view n) const;
};

// The top-level nodes; nullopt (and *error, Japanese) when the file is broken. `version`: 7400 etc.
std::optional<std::vector<FbxNode>> ParseFbxNodes(std::span<const uint8_t> bytes, int* version, std::string* error);

}  // namespace rm::space3d
