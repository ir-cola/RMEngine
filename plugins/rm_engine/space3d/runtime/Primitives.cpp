#include "rm_engine/space3d/runtime/Primitives.h"

#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace rm::space3d {

namespace {

constexpr int kAround = 32;  // segments once around a round shape
constexpr int kArc = 8;      // rings over a quarter circle (a sphere: 2 x kArc rings from pole to pole)

// The box: 6 faces x 4 corners (each face its own normal), 2 triangles per face.
MeshData Box() {
    MeshData m;
    const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        // Two axes across the face.
        const int a = n[f][0] != 0 ? 1 : 0;
        const int b = n[f][2] != 0 ? 1 : 2;
        for (int k = 0; k < 4; ++k) {
            float p[3] = {n[f][0] * 0.5f, n[f][1] * 0.5f, n[f][2] * 0.5f};
            p[a] = (k == 1 || k == 2) ? 0.5f : -0.5f;
            p[b] = (k >= 2) ? 0.5f : -0.5f;
            m.vertices.push_back({{p[0], p[1], p[2]}, {n[f][0], n[f][1], n[f][2]}, {(k == 1 || k == 2) ? 1.f : 0.f, k >= 2 ? 1.f : 0.f}});
        }
        const uint32_t base = static_cast<uint32_t>(f * 4);
        for (const uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + k);
    }
    return m;
}

// One ring of a shape turned around Y: its distance from the axis, its height, and the normal there
// (its part away from the axis, its part up). v: the picture's V on it.
struct Ring {
    float r, y, nr, ny, v;
};

// The rings, top to bottom, turned once around Y (the seam has its vertices twice: U 0 and 1).
// A ring of radius 0 (a pole) gets no triangles that would have no area.
void Revolve(MeshData& m, const std::vector<Ring>& rings) {
    const uint32_t base = static_cast<uint32_t>(m.vertices.size());
    for (const Ring& ring : rings) {
        for (int j = 0; j <= kAround; ++j) {
            const float a = 2.f * std::numbers::pi_v<float> * static_cast<float>(j) / kAround;
            const float s = std::sin(a), c = std::cos(a);
            m.vertices.push_back({{ring.r * s, ring.y, ring.r * c}, {ring.nr * s, ring.ny, ring.nr * c}, {static_cast<float>(j) / kAround, ring.v}});
        }
    }
    const uint32_t row = kAround + 1;
    for (size_t i = 0; i + 1 < rings.size(); ++i) {
        for (uint32_t j = 0; j < kAround; ++j) {
            const uint32_t a = base + static_cast<uint32_t>(i) * row + j, b = a + row, c = b + 1, d = a + 1;
            if (rings[i + 1].r > 1e-6f) m.indices.insert(m.indices.end(), {a, b, c});
            if (rings[i].r > 1e-6f) m.indices.insert(m.indices.end(), {a, c, d});
        }
    }
}

// A flat round lid at height y facing up (up) or down.
void Lid(MeshData& m, float radius, float y, bool up) {
    const uint32_t center = static_cast<uint32_t>(m.vertices.size());
    const float ny = up ? 1.f : -1.f;
    m.vertices.push_back({{0, y, 0}, {0, ny, 0}, {0.5f, 0.5f}});
    for (int j = 0; j <= kAround; ++j) {
        const float a = 2.f * std::numbers::pi_v<float> * static_cast<float>(j) / kAround;
        const float s = std::sin(a), c = std::cos(a);
        m.vertices.push_back({{radius * s, y, radius * c}, {0, ny, 0}, {0.5f + 0.5f * s, 0.5f - 0.5f * c * ny}});
    }
    for (uint32_t j = 0; j < kAround; ++j) {
        const uint32_t p = center + 1 + j, q = p + 1;
        if (up) {
            m.indices.insert(m.indices.end(), {center, p, q});
        } else {
            m.indices.insert(m.indices.end(), {center, q, p});
        }
    }
}

// A quarter circle of rings: from angle `from` to `to` (0 = the top pole, pi/2 = the widest), radius
// 0.5, its centre at height `centerY`; V from v0 to v1.
void Arc(std::vector<Ring>& rings, float from, float to, float centerY, float v0, float v1, bool skipFirst) {
    for (int k = skipFirst ? 1 : 0; k <= kArc; ++k) {
        const float t = static_cast<float>(k) / kArc;
        const float a = from + (to - from) * t;
        const float s = std::sin(a), c = std::cos(a);
        rings.push_back({0.5f * s, centerY + 0.5f * c, s, c, v0 + (v1 - v0) * t});
    }
}

MeshData Sphere() {
    MeshData m;
    std::vector<Ring> rings;
    const float half = std::numbers::pi_v<float> * 0.5f;
    Arc(rings, 0, half, 0, 0, 0.5f, false);
    Arc(rings, half, 2 * half, 0, 0.5f, 1, true);
    Revolve(m, rings);
    return m;
}

MeshData Cylinder() {
    MeshData m;
    Revolve(m, {{0.5f, 0.5f, 1, 0, 0}, {0.5f, -0.5f, 1, 0, 1}});
    Lid(m, 0.5f, 0.5f, true);
    Lid(m, 0.5f, -0.5f, false);
    return m;
}

MeshData Capsule() {
    MeshData m;
    std::vector<Ring> rings;
    const float half = std::numbers::pi_v<float> * 0.5f;
    // (V over the height: the ends are a quarter each, the straight part half)
    Arc(rings, 0, half, 0.5f, 0, 0.25f, false);
    Arc(rings, half, 2 * half, -0.5f, 0.75f, 1, false);
    Revolve(m, rings);
    return m;
}

MeshData Plane() {
    constexpr float s = 5.f;
    MeshData m;
    for (const auto& [x, z] : {std::pair{-s, -s}, {s, -s}, {s, s}, {-s, s}}) m.vertices.push_back({{x, 0, z}, {0, 1, 0}, {x / (2 * s) + 0.5f, z / (2 * s) + 0.5f}});
    m.indices = {0, 2, 1, 0, 3, 2};
    return m;
}

}  // namespace

void FaceOut(MeshData& m) {
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const MeshVertex& a = m.vertices[m.indices[i]];
        const MeshVertex& b = m.vertices[m.indices[i + 1]];
        const MeshVertex& c = m.vertices[m.indices[i + 2]];
        if (Dot(Cross(b.position - a.position, c.position - a.position), a.normal + b.normal + c.normal) < 0) {
            std::swap(m.indices[i + 1], m.indices[i + 2]);
        }
    }
}

MeshData ShapeMesh(Shape shape) {
    MeshData m;
    switch (shape) {
        case Shape::Box: m = Box(); break;
        case Shape::Sphere: m = Sphere(); break;
        case Shape::Cylinder: m = Cylinder(); break;
        case Shape::Capsule: m = Capsule(); break;
        case Shape::Plane: m = Plane(); break;
    }
    FaceOut(m);  // (the box's corners are not laid out by their winding)
    ComputeBounds(m);
    return m;
}

}  // namespace rm::space3d
