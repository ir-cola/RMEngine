// GPU スキニング (plugins/rm_engine/space3d/runtime/Skinning.h と Renderer3D の骨のあるメッシュ) のテスト:
// CPU の式 (シェーダーと同じ) を手で計算した値と比べる・骨ごとの箱の和・レンダラーが骨のあるメッシュを
// GPU に載せて、休みの姿勢・動かした骨・違う骨の数・インスタンス描画・影の地図の使い回しで描くこと。
// (シェーダーのコンパイルは TestWeather.cpp の Weather_ShadersCompile も見る。)

#include "Test.h"

#include "rm_engine/space3d/runtime/Primitives.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Skinning.h"
#include "rm_engine/space3d/runtime/View.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace rm;
using namespace rm::space3d;

namespace {

bool NearV(Vec3 a, Vec3 b, float eps = 1e-4f) { return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.z - b.z) < eps; }

Mat4 Translation(Vec3 t) {
    Mat4 m;
    m.m[3][0] = t.x, m.m[3][1] = t.y, m.m[3][2] = t.z;
    return m;
}

SkinVertex Influence(uint16_t b0, uint16_t w0, uint16_t b1 = 0, uint16_t w1 = 0) {
    SkinVertex v;
    v.bone = {b0, b1, 0, 0};
    v.weight = {w0, w1, 0, 0};
    return v;
}

// 骨 2 本のスキン: 骨 0 は根 (原点)、骨 1 はその子で、バインドしたときに y = 2 にある。
SkinData TwoBones() {
    SkinData s;
    s.skeleton.bones = {{"根", -1, {}}, {"先", 0, {}}};
    s.inverseBind = {Mat4{}, Translation({0, -2, 0})};
    return s;
}

}  // namespace

RM_TEST(Skinning_CpuMatchesHandComputed) {
    // 骨 0: 動かない。骨 1: Z のまわりに 90 度 (x -> y, y -> -x) してから x に +1。
    const BoneMatrix skin[2] = {ToBoneMatrix(Mat4{}), ToBoneMatrix(Translation({1, 0, 0}) * AxisAngle({0, 0, 1}, 90))};
    const Vec3 p{1, 2, 3};
    CHECK(NearV(BonePoint(skin[1], p), {-1, 1, 3}));
    CHECK(NearV(SkinPosition(Influence(0, 65535), skin, p), p));
    CHECK(NearV(SkinPosition(Influence(1, 65535), skin, p), {-1, 1, 3}));
    // 半分ずつ (32768 + 32767 = 65535): 2 つの位置の真ん中 (x 0、y 1.5)。
    CHECK(NearV(SkinPosition(Influence(0, 32768, 1, 32767), skin, p), {(32768.f - 32767.f) / 65535.f, (2 * 32768.f + 32767.f) / 65535.f, 3}));
    CHECK(NearV(SkinPosition(Influence(0, 32768, 1, 32767), skin, p), {0, 1.5f, 3}, 1e-3f));
    // 法線は 3x3 だけ (移動は効かない)、長さ 1 に直す。
    CHECK(NearV(SkinNormal(Influence(1, 65535), skin, {1, 0, 0}), {0, 1, 0}));
    const BoneMatrix big[1] = {ToBoneMatrix(AxisAngle({0, 0, 1}, 90) * TRSMatrix({{0, 0, 0}, {}, {2, 2, 2}}))};
    CHECK(NearV(SkinPosition(Influence(0, 65535), big, {1, 0, 0}), {0, 2, 0}));
    CHECK(NearV(SkinNormal(Influence(0, 65535), big, {1, 0, 0}), {0, 1, 0}));
    // 骨の行列が無い: 休みの姿勢 (そのまま)。範囲の外の骨は 0 として混ざる (GPU のバッファーの外と同じ)。
    CHECK(NearV(SkinPosition(Influence(1, 65535), {}, p), p));
    CHECK(NearV(SkinPosition(Influence(7, 65535), skin, p), {0, 0, 0}));
    // スキンの行列が全部単位行列 (休みの姿勢): ウェイトがどうでも動かない。
    const BoneMatrix rest[2] = {};
    CHECK(NearV(SkinPosition(Influence(0, 10000, 1, 55535), rest, p), p));
    CHECK(NearV(SkinNormal(Influence(0, 10000, 1, 55535), rest, {0, 0, 1}), {0, 0, 1}));
}

RM_TEST(Skinning_BoundsFollowBones) {
    SkinData s = TwoBones();
    // 骨 0 が動かす点: 箱 [-0.5, 0.5] x [0, 1] x [-0.5, 0.5]。骨 1: [-0.5, 0.5] x [2, 3] x [-0.5, 0.5]。
    std::vector<Vec3> points;
    for (float y : {0.f, 1.f, 2.f, 3.f}) {
        for (float x : {-0.5f, 0.5f}) {
            for (float z : {-0.5f, 0.5f}) {
                points.push_back({x, y, z});
                s.vertices.push_back(Influence(y < 1.5f ? 0 : 1, 65535));
            }
        }
    }
    ComputeBoneBounds(s, points);
    const std::vector<BoneBox> boxes = BoneBoxes(s);
    CHECK(boxes.size() == 2 && !boxes[0].empty && !boxes[1].empty);
    CHECK(NearV(boxes[1].center, {0, 2.5f, 0}) && NearV(boxes[1].half[1], {0, 0.5f, 0}));  // (バインドしたときのモデルの空間)
    // 休みの姿勢: メッシュの範囲そのもの。
    const BoneMatrix rest[2] = {};
    const std::optional<Bounds> still = SkinnedBounds(rest, boxes);
    CHECK(still && NearV(still->min, {-0.5f, 0, -0.5f}) && NearV(still->max, {0.5f, 3, 0.5f}));
    // 骨 1 を関節 (y = 2) で Z のまわりに 90 度: 骨 1 の点は x [-1, 0]、y [1.5, 2.5] に倒れる。
    const Mat4 bone1 = Translation({0, 2, 0}) * AxisAngle({0, 0, 1}, 90);
    const BoneMatrix posed[2] = {ToBoneMatrix(Mat4{}), ToBoneMatrix(bone1 * s.inverseBind[1])};
    const std::optional<Bounds> bent = SkinnedBounds(posed, boxes);
    CHECK(bent && NearV(bent->min, {-1, 0, -0.5f}) && NearV(bent->max, {0.5f, 2.5f, 0.5f}));
    // 動いた点はどれも和の中にある。
    bool inside = true;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec3 q = SkinPosition(s.vertices[i], posed, points[i]);
        inside = inside && q.x >= bent->min.x - 1e-4f && q.y >= bent->min.y - 1e-4f && q.z >= bent->min.z - 1e-4f &&
                 q.x <= bent->max.x + 1e-4f && q.y <= bent->max.y + 1e-4f && q.z <= bent->max.z + 1e-4f;
    }
    CHECK(inside);
    // 点を動かさない骨は入らない。範囲のある骨が無い: nullopt (呼ぶ側はメッシュの範囲)。逆にできない
    // 逆バインド行列の骨も入らない。
    SkinData lone = TwoBones();
    lone.vertices.assign(points.size(), Influence(0, 65535));
    ComputeBoneBounds(lone, points);
    const std::vector<BoneBox> loneBoxes = BoneBoxes(lone);
    CHECK(!loneBoxes[0].empty && loneBoxes[1].empty);
    CHECK(!SkinnedBounds({}, loneBoxes));
    Mat4 flat;
    flat.m[1][1] = 0;
    lone.inverseBind[0] = flat;
    CHECK(BoneBoxes(lone)[0].empty);
}

namespace {

// テスト用の GPU (無ければソフトのもの)。
Microsoft::WRL::ComPtr<ID3D11Device> TestDevice(Microsoft::WRL::ComPtr<ID3D11DeviceContext>& ctx) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &ctx);
    if (FAILED(hr)) device.Reset();
    return device;
}

// 絵 (B8G8R8A8) の 1 ピクセルのアルファ (0: 何も描かれていない)。
int AlphaAt(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* picture, int x, int y) {
    D3D11_TEXTURE2D_DESC td{};
    picture->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) return -1;
    ctx->CopyResource(staging.Get(), picture);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return -1;
    const auto* row = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch;
    const int a = row[x * 4 + 3];
    ctx->Unmap(staging.Get(), 0);
    return a;
}

// ワールドの点が絵のどのピクセルに来るか。
std::pair<int, int> PixelOf(const Mat4& viewProj, Vec3 p, int size) {
    float c[4];
    for (int r = 0; r < 4; ++r) c[r] = viewProj.m[0][r] * p.x + viewProj.m[1][r] * p.y + viewProj.m[2][r] * p.z + viewProj.m[3][r];
    const float x = c[0] / c[3], y = c[1] / c[3];
    return {static_cast<int>((x * 0.5f + 0.5f) * static_cast<float>(size)), static_cast<int>((0.5f - y * 0.5f) * static_cast<float>(size))};
}

}  // namespace

RM_TEST(Skinning_RendererDrawsPoses) {
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    const auto device = TestDevice(ctx);
    CHECK(device);
    if (!device) return;
    Renderer r;
    std::string error;
    CHECK(r.Init(device.Get(), &error));
    if (!r.Ready()) return;

    // 単位立方体 (-0.5 .. 0.5)。全部の頂点が骨 0 に付く (骨 1 は何も動かさない)。
    MeshData cube = ShapeMesh(Shape::Box);
    SkinData skin = TwoBones();
    skin.vertices.assign(cube.vertices.size(), Influence(0, 65535));
    std::vector<Vec3> positions;
    for (const MeshVertex& v : cube.vertices) positions.push_back(v.position);
    ComputeBoneBounds(skin, positions);
    cube.skin = skin;
    const auto gpu = r.Upload(cube, &error);
    CHECK(gpu && gpu->skinned && gpu->boneCount == 2 && gpu->boneBoxes.size() == 2);
    if (!gpu) return;
    // 骨の無いメッシュは今のまま。
    MeshData plain = ShapeMesh(Shape::Box);
    const auto plainGpu = r.Upload(plain, &error);
    CHECK(plainGpu && !plainGpu->skinned && plainGpu->boneCount == 0);
    // 壊れたスキンは載せない (シェーダーがその描画の骨の外を読まないように)。
    MeshData bad = cube;
    bad.skin->vertices[3].bone[1] = 2;
    CHECK(!r.Upload(bad, &error) && !error.empty());
    bad = cube;
    bad.skin->vertices.pop_back();
    CHECK(!r.Upload(bad, &error));

    constexpr int kSize = 64;
    RenderFrame f;
    f.width = f.height = kSize;
    f.view = LookAt({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    f.projection = Perspective(60, 1, 0.1f, 100);
    f.clear = {0, 0, 0, 0};
    const Mat4 viewProj = f.projection * f.view;
    const auto [cx, cy] = PixelOf(viewProj, {0, 0, 0}, kSize);
    const auto [mx, my] = PixelOf(viewProj, {1.2f, 0, 0}, kSize);
    MeshDraw d;
    d.mesh = gpu.get();
    f.meshes = std::span<const MeshDraw>(&d, 1);

    // 休みの姿勢 (骨の行列なし): そのまま真ん中に出る。
    ID3D11Texture2D* picture = r.Render(ctx.Get(), f);
    CHECK(picture);
    CHECK(r.LastStats().skinnedDraws == 1 && r.LastStats().drawn == 1 && r.LastStats().bonesUploaded == 0 && r.LastStats().wrongBoneCounts == 0);
    if (picture) CHECK(AlphaAt(device.Get(), ctx.Get(), picture, cx, cy) == 255 && AlphaAt(device.Get(), ctx.Get(), picture, mx, my) == 0);

    // 骨 0 を x に +1.2: GPU が頂点を動かす (真ん中は空き、x = 1.2 に出る)。
    BoneMatrix moved[2] = {ToBoneMatrix(Translation({1.2f, 0, 0})), {}};
    d.bones = moved;
    picture = r.Render(ctx.Get(), f);
    CHECK(picture);
    CHECK(r.LastStats().drawn == 1 && r.LastStats().bonesUploaded == 2);
    if (picture) CHECK(AlphaAt(device.Get(), ctx.Get(), picture, cx, cy) == 0 && AlphaAt(device.Get(), ctx.Get(), picture, mx, my) == 255);

    // 回した骨も描ける。
    moved[0] = ToBoneMatrix(AxisAngle({1, 1, 0}, 45));
    picture = r.Render(ctx.Get(), f);
    CHECK(picture && r.LastStats().drawn == 1);
    if (picture) CHECK(AlphaAt(device.Get(), ctx.Get(), picture, cx, cy) == 255);

    // 見えるかの判定は動いた形の範囲で: 骨で遠くへ動かしたものは描かない (メッシュの範囲は見えていても)。
    moved[0] = ToBoneMatrix(Translation({50, 0, 0}));
    CHECK(r.Render(ctx.Get(), f) && r.LastStats().drawn == 0 && r.LastStats().things == 1);

    // 骨の数が違う: 休みの姿勢で描き、数える (落ちない)。骨の無いメッシュに渡した骨も同じ。
    d.bones = std::span<const BoneMatrix>(moved, 1);
    picture = r.Render(ctx.Get(), f);
    CHECK(picture && r.LastStats().wrongBoneCounts == 1 && r.LastStats().bonesUploaded == 0 && r.LastStats().drawn == 1);
    if (picture) CHECK(AlphaAt(device.Get(), ctx.Get(), picture, cx, cy) == 255);
    MeshDraw p2;
    p2.mesh = plainGpu.get();
    p2.bones = moved;
    f.meshes = std::span<const MeshDraw>(&p2, 1);
    CHECK(r.Render(ctx.Get(), f) && r.LastStats().wrongBoneCounts == 1 && r.LastStats().skinnedDraws == 0);

    // 同じメッシュの違う姿勢 2 つ: 1 回のインスタンス描画にまとまる (骨の先頭は描画ごと)。
    const BoneMatrix left[2] = {ToBoneMatrix(Translation({-1.2f, 0, 0})), {}};
    const BoneMatrix right[2] = {ToBoneMatrix(Translation({1.2f, 0, 0})), {}};
    MeshDraw two[2];
    two[0].mesh = two[1].mesh = gpu.get();
    two[0].bones = left;
    two[1].bones = right;
    f.meshes = two;
    picture = r.Render(ctx.Get(), f);
    CHECK(picture && r.LastStats().drawCalls == 1 && r.LastStats().bonesUploaded == 4 && r.LastStats().skinnedDraws == 2);
    if (picture) {
        const auto [lx, ly] = PixelOf(viewProj, {-1.2f, 0, 0}, kSize);
        CHECK(AlphaAt(device.Get(), ctx.Get(), picture, lx, ly) == 255 && AlphaAt(device.Get(), ctx.Get(), picture, mx, my) == 255 &&
              AlphaAt(device.Get(), ctx.Get(), picture, cx, cy) == 0);
    }

    // 影の地図: 姿勢の版があれば、同じ姿勢のままでは描き直さない。版が無い (0) と毎フレーム描く。
    f.shadows = true;
    d.bones = right;
    d.poseVersion = NextPoseVersion();
    f.meshes = std::span<const MeshDraw>(&d, 1);
    CHECK(r.Render(ctx.Get(), f) && r.LastStats().mapsDrawn >= 1);
    CHECK(r.Render(ctx.Get(), f) && r.LastStats().mapsDrawn == 0 && r.LastStats().mapsKept >= 1);
    d.poseVersion = NextPoseVersion();  // 姿勢が変わった
    CHECK(r.Render(ctx.Get(), f) && r.LastStats().mapsDrawn >= 1);
    d.poseVersion = 0;
    CHECK(r.Render(ctx.Get(), f) && r.LastStats().mapsDrawn >= 1);
    CHECK(r.Render(ctx.Get(), f) && r.LastStats().mapsDrawn >= 1);
    // 休みの姿勢の骨のあるメッシュは、動かないものと同じ (使い回す)。
    d.bones = {};
    CHECK(r.Render(ctx.Get(), f));
    CHECK(r.Render(ctx.Get(), f) && r.LastStats().mapsDrawn == 0);
    // 向きの地図の形 (MapShape) は骨のあるメッシュでも休みの姿勢で入る (落ちない)。
    const MapShape shape{gpu.get(), Mat4{}, false};
    const Vec3 down{0, -1, 0};
    f.mapShapes = std::span<const MapShape>(&shape, 1);
    f.mapDirections = std::span<const Vec3>(&down, 1);
    CHECK(r.Render(ctx.Get(), f));
}
