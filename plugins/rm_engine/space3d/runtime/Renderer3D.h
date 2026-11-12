#pragma once
// 3D 空間 - drawing: 3D lines, lit boxes and models with Direct3D 11 into a picture of its own (a
// texture), lit by up to 8 lights (Lighting.h) with the shadows of one directional light. The editor shows that picture in a panel (the
// scene view); the exported game will draw the same way to its window. Knows nothing of games or
// editors. (The space and its drawing are one plugin: one is never used without the other -
// decided 2026-11-05.)
//
//   space3d::Renderer r;   r.Init(device, &error);
//   auto robot = r.Upload(meshData, &error);          // once: the shape stays on the GPU
//   space3d::RenderFrame f{width, height, view, projection, clearColor, lines, boxes};
//   f.meshes = draws;   f.shadows = true;             // MeshDraw{robot.get(), world, color, ..., materials}
//   ID3D11Texture2D* picture = r.Render(context, f);
//
// Colour (physically based, metal / roughness as glTF): the light is worked out in linear light and
// turned into the screen's numbers (sRGB) at the end of each pixel. A surface has a colour (draw's
// colour x the part's material colour x its picture), a metalness and a roughness (x the metal /
// rough picture), a normal (bent by the normal map: the tangents are made from the screen's
// derivatives of the position and the UV - no tangents in the vertices) and an emission (added).
// Each light gives a diffuse part (Lambert, none on metal) and a highlight (GGX / Smith / Schlick);
// the sky gives a little of both. A model's parts (MeshPart) are drawn one by one with their own
// material (DrawMaterial: the numbers and the pictures on the GPU - UploadTexture). Colour and
// emission pictures are sRGB (the GPU turns them into linear light when it reads them); metal /
// rough and normal pictures are numbers. The selected thing is shown by a glow at its edges
// (MeshDraw::highlight), which keeps the material's colours.
// The colours given to a draw (MeshDraw::color, Box::color, highlight, lines, the floor's shadow)
// are the screen's colours, as everywhere in the UI; a material's are linear light (Mesh.h).
//
// The shaders are written in Renderer3D.cpp in HLSL and compiled when the renderer starts
// (D3DCompile, which every Windows 10 / 11 has). Drawn with 4x multisampling when the GPU can.
//
// Shadows: shadow maps - everything that casts a shadow is first drawn from the light into depth
// pictures (Lighting.h: the sun's one map fitted to what casts or its cascades along the view, a
// spot's one, a point light's six); each pixel then looks there to see whether something is between
// it and the light, at 3 x 3 places with the GPU's comparing filter (PCF: soft edges).
// The floor (y = 0) is an invisible plane that only shows the shadows falling on it.
//
// What it costs the CPU (tools/bench3d): draws with the same mesh, part and pictures become one
// instanced draw (DrawOrder.h sorts them: shaders, pictures, mesh); each shadow map is tested against
// its own view (a light's, a cascade's: Frustum) and drawn again only when what it holds changed. A
// map with things that move (Box / MeshDraw::moving) keeps a layer of what stays put and lays it back
// before drawing only the moving ones.

#include "base/Math.h"
#include "platform/Image.h"
#include "rm_engine/space3d/runtime/DrawOrder.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::space3d {

struct LineVertex {
    Vec3 position;
    Color color;  // alpha blends (a fading grid)
};

// The unit cube (-0.5 .. 0.5) placed by `world`, lit from one direction.
struct Box {
    Mat4 world;
    Color color;
    bool castShadow = true;
    // It has moved lately (dragged, played): drawn into the shadow maps over a kept layer of what
    // stays put. Only a hint - a thing that moves without it is still drawn right, the maps it is in
    // are just drawn whole again.
    bool moving = false;
};

// A model's shape on the GPU (Renderer::Upload). Whoever draws it keeps it; it lives as long as
// that (the renderer does not need it between frames).
class GpuMesh {
public:
    uint64_t serial = 0;              // never the same for two meshes (what a shadow map holds is told by it)
    Vec3 min{0, 0, 0}, max{0, 0, 0};  // the bounds in the model's own space
    uint32_t indexCount = 0;
    std::vector<MeshPart> parts;      // one draw each, by material (at least one)
    std::vector<Vec3> partCenters;    // the middle of each part's bounds (see-through parts are sorted by it)

private:
    friend class Renderer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices, indices;
};

// A picture on the GPU (Renderer::UploadTexture).
class GpuTexture {
public:
    uint32_t width = 0, height = 0;
    bool srgb = false;  // read as colours (sRGB -> linear) or as numbers
    bool sharp = false;  // drawn with the nearest pixel (no smoothing): the image's import setting
    size_t bytes = 0;    // on the GPU, its mipmaps included
    TextureFormat format = TextureFormat::Rgba8;  // how it is kept (BC: compressed)
    uint64_t madeBy = 0;  // the Renderer::Id that made it (another one - a GPU made again - cannot use it)

private:
    friend class Renderer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
};

// How one material of a model is painted (MeshMaterial on the GPU; a null picture: the numbers only).
// The pictures are shared: a material keeps its pictures alive while it is drawn, even if the cache
// reads a file again (and lets go of the old picture) meanwhile.
using TextureRef = std::shared_ptr<const GpuTexture>;
struct DrawMaterial {
    Color color{1, 1, 1, 1};  // linear light
    TextureRef texture;       // the colour (sRGB)
    AlphaMode alpha = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;  // Mask: less than this is not drawn
    float metallic = 0.f, roughness = 0.8f;
    TextureRef metalRough;  // blue: metal, green: rough
    TextureRef normal;      // tangent space
    float normalScale = 1.f;
    Color emissive{0, 0, 0, 1};  // linear light, may be more than 1
    TextureRef emission;         // sRGB
};

struct MeshDraw {
    const GpuMesh* mesh = nullptr;
    Mat4 world;
    Color color{1, 1, 1, 1};  // times every material (white: as the model is)
    bool castShadow = true;
    std::span<const DrawMaterial> materials;  // by MeshPart::material; a part without one here is white
    Color highlight{0, 0, 0, 0};              // a glow at the edges (selected): its colour, alpha = how strong
    bool moving = false;                      // as Box::moving
};

struct RenderFrame {
    uint32_t width = 0, height = 0;  // pixels
    Mat4 view, projection;
    Color clear{0, 0, 0, 1};
    std::span<const LineVertex> lines;  // pairs: 0-1, 2-3, ...
    // Pairs drawn last, over everything: as given where nothing is in front of them, faint where
    // something is (a game's rm::Debug lines while it plays). One copy to the GPU, two draws.
    std::span<const LineVertex> debugLines;
    std::span<const Box> boxes;
    std::span<const MeshDraw> meshes;
    // The lights (Lighting.h: at most kMaxLights). None: one white directional light going
    // `lightDirection` (from above, a little from the front).
    std::span<const Light> lights;
    Vec3 lightDirection{-0.45f, -1.f, -0.3f};
    Ambient ambient;       // the light every face gets (環境光)
    bool shadows = false;  // the lights' shadows (Lighting.h says which lights have them)
    Color shadowColor{0, 0, 0, 0.4f};  // the shadows on the floor (alpha: how dark)
};

class Renderer {
public:
    static constexpr uint32_t kShadowMapSize = 2048;

    bool Init(ID3D11Device* device, std::string* error);
    bool Ready() const { return shared_ != nullptr; }
    // Different for every Init (of any renderer): what a GpuTexture remembers as madeBy.
    uint64_t Id() const { return id_; }
    // The shape goes to the GPU once; null (and *error, Japanese) when it cannot.
    std::shared_ptr<const GpuMesh> Upload(const MeshData& mesh, std::string* error);
    // A picture (PixelOrder::Rgba) goes to the GPU once, with its mipmaps; null (and *error) when it
    // cannot. srgb: a colour picture (read as linear light); false: numbers (metal / rough, normals).
    // sharp: drawn with the nearest pixel (dot pictures: the image's import setting) instead of smoothed.
    std::shared_ptr<const GpuTexture> UploadTexture(const platform::Image& rgba, bool srgb, std::string* error, bool sharp = false);
    // The same with the mipmaps already made (the quick part: only the copy to the GPU).
    std::shared_ptr<const GpuTexture> UploadTexture(const TexturePixels& pixels, std::string* error, bool sharp = false);
    // Whether this GPU reads BC7 (feature level 11.0): without it colours with alpha are kept as BC3.
    bool SupportsBC7() const;
    // Draws the frame and returns the finished picture (B8G8R8A8, usable by Direct2D); null on failure.
    // The picture is kept until the next Render (its size follows the frame's).
    ID3D11Texture2D* Render(ID3D11DeviceContext* ctx, const RenderFrame& f);

    // What the last Render did with the boxes and models. Culling is always on: a thing wholly outside
    // what the camera sees (View.h's BoxInView on its world bounds) is not drawn - but still casts its
    // shadow (it may fall inside the picture).
    struct Stats {
        size_t things = 0;     // boxes and models given
        size_t drawn = 0;      // ... drawn in the picture (the rest were culled)
        size_t mapsDrawn = 0;  // shadow maps drawn
        size_t mapsKept = 0;   // ... and not drawn again: they already held the same (nothing moved)
        size_t layersDrawn = 0;  // kept layers (what stays put, under the moving things) drawn again
        size_t drawCalls = 0;    // draws given to the GPU (instancing: one per run of the same mesh, part and pictures)
    };
    // Per device (shared by every renderer on it: the scene view, the camera preview, the
    // thumbnails): the shaders and states, the cube and the floor, the constant buffers and the
    // shadow maps with what each map holds now. Renderer3D.cpp.
    struct Shared;
    const Stats& LastStats() const { return stats_; }

private:
    struct Item {
        const GpuMesh* mesh;
        const Mat4* world;
        Color color;
        bool castShadow;
        std::span<const DrawMaterial> materials;
        Color highlight;
        bool seen = true;  // inside what the camera sees (frustum culling); drawn into the shadow maps either way
        Bounds bounds;     // in the world
        uint32_t thing = 0;          // where its numbers are in things_ (in float4s)
        uint32_t firstMaterial = 0;  // ... and its parts' materials, one after another
        bool moving = false;         // Box / MeshDraw::moving
        uint64_t hash = 0;           // its mesh and place (what a shadow map remembers of it)
    };
    // One draw: some triangles of a mesh, with the numbers of a thing and a material (`slot`: its
    // place in draws_, read by the shader as the instance's data).
    struct Cmd {
        const GpuMesh* mesh;
        uint32_t firstIndex, indexCount;
        uint32_t slot;
        const DrawMaterial* material;  // its pictures (null: none - a shadow map, the floor)
        uint32_t instances = 1;        // slots slot .. slot + instances - 1: one instanced draw
    };

    bool Resize(uint32_t width, uint32_t height);
    bool LineCapacity(size_t vertices);
    // The frame's numbers on the GPU (made larger when needed).
    bool UploadFrame(ID3D11DeviceContext* ctx);
    uint32_t AddThing(const Mat4& world, Color color, Color highlight);
    uint32_t AddMaterial(const DrawMaterial& m);
    uint32_t AddDraw(uint32_t thing, uint32_t material);
    // A draw added to cmds_ - or, when it is the last one's next instance (the same mesh, part and
    // pictures, the next slot), added to it. Never merged with draws before mergeFloor_.
    void Emit(const GpuMesh* mesh, uint32_t firstIndex, uint32_t indexCount, uint32_t slot, const DrawMaterial* material);
    // A number for each different set of pictures (0: none), for DrawKey::material.
    uint32_t PictureSet(const DrawMaterial* material);

    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    std::shared_ptr<Shared> shared_;
    Ptr<ID3D11Device> device_;
    uint64_t id_ = 0;
    Ptr<ID3D11Buffer> lineVb_;
    size_t lineCapacity_ = 0;
    // Every thing's and material's numbers, and every draw's (thing, material), written once a frame.
    std::vector<std::array<float, 4>> things_;
    std::vector<uint32_t> draws_;  // pairs
    Ptr<ID3D11Buffer> thingsBuffer_, drawsBuffer_;
    Ptr<ID3D11ShaderResourceView> thingsView_;
    size_t thingsCapacity_ = 0, drawsCapacity_ = 0;
    std::vector<Item> items_;
    std::vector<Cmd> cmds_;
    size_t mergeFloor_ = 0;
    std::vector<DrawKey> keys_;
    std::vector<uint32_t> order_, casterOrder_, still_, moving_;
    std::vector<std::pair<uint32_t, uint32_t>> refs_;  // (item, part) of each key
    struct PictureSetHash {
        size_t operator()(const std::array<const void*, 5>& a) const noexcept {
            size_t h = 0;
            for (const void* p : a) h = h * 1000003u ^ std::hash<const void*>()(p);
            return h;
        }
    };
    std::unordered_map<std::array<const void*, 5>, uint32_t, PictureSetHash> pictureSets_;  // this frame's (4 pictures, the sampler's kind)
    std::vector<std::pair<size_t, const MeshPart*>> seeThrough_;  // (item, part) drawn last, back to front
    std::vector<Vec3> centers_;
    Stats stats_;

    UINT samples_ = 1;
    uint32_t width_ = 0, height_ = 0;
    Ptr<ID3D11Texture2D> picture_;      // what is returned (resolved)
    Ptr<ID3D11Texture2D> multisample_;  // drawn into when samples_ > 1
    Ptr<ID3D11RenderTargetView> rtv_;
    Ptr<ID3D11Texture2D> depthTex_;
    Ptr<ID3D11DepthStencilView> dsv_;
};

}  // namespace rm::space3d

