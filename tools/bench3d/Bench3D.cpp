// 3D レンダラーが 1 フレームに CPU で使う時間 (space3d::Renderer::Render): 1000 個の箱
// (影を落とす) と 8 個の光 (太陽・点光源・スポット、影つき) を、動くカメラで見る。エンジンにも
// そのテストにも入れない (GPU が要る): 手で作って走らせる -
//
//   powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench3d\run.ps1 [-Frames 300] [-Case boxes|mesh]
//
//   boxes: 1000 個の箱 (Box: レンダラーの立方体)
//   mesh:  同じメッシュのモデル 1000 個 (MeshDraw: 三角形 480 個の球、マテリアル 1 つ、アップロードは 1 回)
//   drag:  1000 個の箱を近づけて並べる (太陽の地図は 1 枚、カスケードなし)。カメラは動かさず、
//          いちばん外の箱を毎フレーム少しずつ外へ引っぱる (動くものとして印をつける): 引っぱる費用
//
// Render に使った CPU 時間の平均と中央値を出す (GPU はフレームの間に待つ。計る時間の外)。

#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/View.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::space3d;

int main(int argc, char** argv) {
    const int frames = argc > 1 && std::atoi(argv[1]) > 0 ? std::atoi(argv[1]) : 300;
    const bool meshCase = argc > 2 && std::string(argv[2]) == "mesh";
    const bool dragCase = argc > 2 && std::string(argv[2]) == "drag";
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    // ほかの引数の後ならどこでもよい: "fl10" = フィーチャーレベル 10.0 だけのデバイス (エンジンが
    // 要る最低限)。"debug" = Direct3D のデバッグ層つき (そのメッセージはデバッガーの出力へ行く)。
    bool fl10 = false, debug = false;
    for (int i = 1; i < argc; ++i) {
        fl10 = fl10 || std::string(argv[i]) == "fl10";
        debug = debug || std::string(argv[i]) == "debug";
    }
    const D3D_FEATURE_LEVEL all[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const D3D_FEATURE_LEVEL only10[] = {D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | (debug ? D3D11_CREATE_DEVICE_DEBUG : 0),
                                 fl10 ? only10 : all, fl10 ? 1 : 3, D3D11_SDK_VERSION, &device, &got, &ctx))) {
        std::printf("no Direct3D 11 device%s\n", debug ? " (with the debug layer: is it installed?)" : "");
        return 1;
    }
    if (fl10 || debug) std::printf("feature level %x%s\n", static_cast<unsigned>(got), debug ? ", debug layer" : "");
    Microsoft::WRL::ComPtr<ID3D11InfoQueue> info;
    if (debug) device.As(&info);
    Renderer r;
    std::string error;
    if (!r.Init(device.Get(), &error)) {
        std::printf("Init failed: %s\n", error.c_str());
        return 1;
    }

    // 40 x 25 の格子 (2 間隔) に 1000 個の箱、いくつかは積む。
    std::vector<Box> boxes;
    for (int i = 0; i < 1000; ++i) {
        Transform t;
        t.position = {static_cast<float>(i % 40) * 2.f - 40.f, 0.5f + static_cast<float>(i % 3), static_cast<float>(i / 40) * 2.f - 25.f};
        t.rotation = {0, static_cast<float>(i * 13 % 90), 0};
        boxes.push_back({LocalMatrix(t), {0.7f, 0.7f, 0.75f, 1}, true});
    }
    // 同じ 1000 か所に 1 つのメッシュのモデルを置く: 球 (16 x 15 の四角)、マテリアル 1 つ。
    MeshData ball;
    constexpr int kAround = 16, kUp = 15;
    for (int j = 0; j <= kUp; ++j) {
        for (int i = 0; i <= kAround; ++i) {
            const float u = static_cast<float>(i) / kAround * 6.2831853f, v = static_cast<float>(j) / kUp * 3.1415926f;
            const Vec3 n{std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u)};
            ball.vertices.push_back({n * 0.5f, n, {static_cast<float>(i) / kAround, static_cast<float>(j) / kUp}});
        }
    }
    for (int j = 0; j < kUp; ++j) {
        for (int i = 0; i < kAround; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * (kAround + 1) + i), b = a + 1, c = a + kAround + 1, d = c + 1;
            ball.indices.insert(ball.indices.end(), {a, c, b, b, c, d});
        }
    }
    ComputeBounds(ball);
    const auto ballGpu = r.Upload(ball, &error);
    if (!ballGpu) {
        std::printf("Upload failed: %s\n", error.c_str());
        return 1;
    }
    std::vector<DrawMaterial> ballMaterials(1);
    ballMaterials[0].color = {0.8f, 0.5f, 0.3f, 1};
    ballMaterials[0].roughness = 0.4f;
    std::vector<MeshDraw> meshes;
    for (const Box& b : boxes) {
        MeshDraw m;
        m.mesh = ballGpu.get();
        m.world = b.world;
        m.materials = ballMaterials;
        meshes.push_back(m);
    }
    // drag: 同じ 1000 個の箱を 32 列に 1 間隔で並べる (全部が太陽の地図 1 枚の中)。最初の 1 個が動く。
    std::vector<Box> close = boxes;
    for (size_t i = 0; i < close.size(); ++i) {
        close[i].world.m[3][0] = static_cast<float>(i % 32) - 15.5f;
        close[i].world.m[3][2] = static_cast<float>(i / 32) - 15.5f;
    }
    close[0].moving = true;
    const std::span<const Box> drawBoxes = meshCase ? std::span<const Box>() : dragCase ? std::span<const Box>(close) : std::span<const Box>(boxes);
    const std::span<const MeshDraw> drawMeshes = meshCase ? std::span<const MeshDraw>(meshes) : std::span<const MeshDraw>();

    // 8 個の光: 太陽、点光源 1 個、スポット 6 個。すべて影を落とす。
    std::vector<Light> lights(8);
    lights[0].type = LightType::Directional;
    lights[0].direction = {-0.45f, -1.f, -0.3f};
    lights[1].type = LightType::Point;
    lights[1].position = {0, 4, 0};
    lights[1].range = 15;
    for (int i = 2; i < 8; ++i) {
        lights[static_cast<size_t>(i)].type = LightType::Spot;
        lights[static_cast<size_t>(i)].position = {static_cast<float>(i) * 6.f - 25.f, 6, static_cast<float>(i % 2) * 10.f - 5.f};
        lights[static_cast<size_t>(i)].direction = {0, -1, 0.2f};
        lights[static_cast<size_t>(i)].range = 12;
        lights[static_cast<size_t>(i)].spotAngle = 50;
    }

    // Bench3D.exe image <file.ppm>: 固定した 1 フレーム (太陽、点光源 1 個、スポット 2 個: 順番に
    // 関係なく、どの光も影の地図を持つ) を保存して、2 つの版の絵を見比べる。
    // Bench3D.exe image <file.ppm> [mesh]: 箱の代わりにモデルを使う。
    if (argc > 2 && std::string(argv[1]) == "image") {
        lights.resize(4);
        RenderFrame f;
        f.width = 640;
        f.height = 360;
        f.clear = {0.75f, 0.75f, 0.78f, 1};  // 明るい色: 床の影が見える
        f.view = LookAt({0, 18, 30}, {0, 0, 0}, {0, 1, 0});
        f.projection = Perspective(60, 16.f / 9.f, 0.1f, 500);
        // [moving]: 残りの中の箱を 1 つおきに動かす (影の地図は残りの層を持つ。太陽の地図は動かない
        // ものに合わせてあり、それでも持っている)。1 つを動かして戻すので、最後のフレームは持っていた
        // 層を戻して動くものだけを描く - 絵は動かさないときと同じでなければならない。
        const bool moving = argc > 3 && std::string(argv[3]) == "moving";
        std::vector<Box> still = boxes;
        size_t firstMoving = 0;
        if (moving) {
            for (size_t i = 1; i < still.size(); i += 2) {
                const size_t col = i % 40, row = i / 40;
                if (col == 0 || col == 39 || row == 0 || row == 24 || i % 3 == 2) continue;  // 端でなく、いちばん高くない
                still[i].moving = true;
                if (firstMoving == 0) firstMoving = i;
            }
        }
        if (argc > 3 && std::string(argv[3]) == "mesh") {
            f.meshes = meshes;
        } else {
            f.boxes = still;
        }
        f.lights = lights;
        f.shadows = true;
        if (moving) {
            r.Render(ctx.Get(), f);
            std::vector<Box> moved = still;
            moved[firstMoving].world.m[3][0] += 0.5f;
            f.boxes = moved;
            r.Render(ctx.Get(), f);
            f.boxes = still;
            const Renderer::Stats before = r.LastStats();
            r.Render(ctx.Get(), f);
            std::printf("moving: layers drawn %zu (frame 2), maps drawn %zu kept %zu layers drawn %zu (frame 3)\n", before.layersDrawn,
                        r.LastStats().mapsDrawn, r.LastStats().mapsKept, r.LastStats().layersDrawn);
        }
        r.Render(ctx.Get(), f);
        ID3D11Texture2D* picture = r.Render(ctx.Get(), f);  // 2 回: 2 回目は残せる影の地図を残す
        if (!picture) return 1;
        D3D11_TEXTURE2D_DESC td{};
        picture->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        device->CreateTexture2D(&td, nullptr, &staging);
        ctx->CopyResource(staging.Get(), picture);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return 1;
        FILE* out = std::fopen(argv[2], "wb");
        std::fprintf(out, "P6\n%u %u\n255\n", td.Width, td.Height);
        for (UINT y = 0; y < td.Height; ++y) {
            const auto* row = static_cast<const uint8_t*>(map.pData) + y * map.RowPitch;
            for (UINT x = 0; x < td.Width; ++x) {
                const uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]};
                std::fwrite(rgb, 1, 3, out);
            }
        }
        std::fclose(out);
        ctx->Unmap(staging.Get(), 0);
        std::printf("saved %s\n", argv[2]);
        if (info) {
            // デバッグ層が言ったこと (警告とエラー)。あれば。
            size_t told = 0;
            for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
                SIZE_T size = 0;
                info->GetMessage(i, nullptr, &size);
                std::vector<char> buffer(size);
                auto* m = reinterpret_cast<D3D11_MESSAGE*>(buffer.data());
                if (FAILED(info->GetMessage(i, m, &size)) || m->Severity > D3D11_MESSAGE_SEVERITY_WARNING) continue;
                if (++told <= 10) std::printf("debug layer: %.*s\n", static_cast<int>(m->DescriptionByteLength), m->pDescription);
            }
            std::printf("debug layer: %zu warnings / errors\n", told);
        }
        return 0;
    }

    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    Microsoft::WRL::ComPtr<ID3D11Query> done;
    device->CreateQuery(&qd, &done);
    // フレームの GPU 自身の時間も (Render の前後のタイムスタンプ)。
    D3D11_QUERY_DESC dq{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, tq{D3D11_QUERY_TIMESTAMP, 0};
    Microsoft::WRL::ComPtr<ID3D11Query> disjoint, t0, t1;
    device->CreateQuery(&dq, &disjoint);
    device->CreateQuery(&tq, &t0);
    device->CreateQuery(&tq, &t1);
    std::vector<double> ms, gpu;
    size_t layersAll = 0;
    for (int frame = 0; frame < frames + 20; ++frame) {
        // カメラは真ん中のまわりをゆっくり回る (カスケードはついてくる。光と箱は動かない)。
        const float a = static_cast<float>(frame) * 0.01f;
        RenderFrame f;
        f.width = 1280;
        f.height = 720;
        f.view = dragCase ? LookAt({0, 25, 35}, {0, 0, 0}, {0, 1, 0}) : LookAt({std::sin(a) * 30.f, 18, std::cos(a) * 30.f}, {0, 0, 0}, {0, 1, 0});
        if (dragCase) close[0].world.m[3][0] -= 0.01f;  // 外へ引っぱる、1 フレームに 1 cm
        f.projection = Perspective(60, 16.f / 9.f, 0.1f, 500);
        f.boxes = drawBoxes;
        f.meshes = drawMeshes;
        f.lights = lights;
        f.shadows = true;
        ctx->Begin(disjoint.Get());
        ctx->End(t0.Get());
        const auto start = std::chrono::steady_clock::now();
        r.Render(ctx.Get(), f);
        const auto end = std::chrono::steady_clock::now();
        ctx->End(t1.Get());
        ctx->End(disjoint.Get());
        if (frame >= 20) layersAll += r.LastStats().layersDrawn;
        // 次のフレームの前に GPU がフレームを終える (計る時間の外)。
        ctx->End(done.Get());
        while (ctx->GetData(done.Get(), nullptr, 0, 0) == S_FALSE) {
        }
        if (frame >= 20) ms.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 a0 = 0, a1 = 0;
        while (ctx->GetData(disjoint.Get(), &dj, sizeof(dj), 0) == S_FALSE) {
        }
        if (frame >= 20 && !dj.Disjoint && ctx->GetData(t0.Get(), &a0, sizeof(a0), 0) == S_OK && ctx->GetData(t1.Get(), &a1, sizeof(a1), 0) == S_OK &&
            dj.Frequency > 0) {
            gpu.push_back(static_cast<double>(a1 - a0) * 1000.0 / static_cast<double>(dj.Frequency));
        }
    }
    double sum = 0;
    for (const double v : ms) sum += v;
    std::sort(ms.begin(), ms.end());
    std::printf("frames %zu  average %.3f ms  median %.3f ms  (%s, 8 lights, shadows)\n", ms.size(), sum / static_cast<double>(ms.size()),
                ms[ms.size() / 2], meshCase ? "1000 models of the same mesh" : dragCase ? "1000 boxes, the outermost dragged" : "1000 boxes");
    if (!gpu.empty()) {
        std::sort(gpu.begin(), gpu.end());
        std::printf("GPU: median %.3f ms\n", gpu[gpu.size() / 2]);
    }
    const Renderer::Stats& st = r.LastStats();
    std::printf("last frame: %zu drawn of %zu, %zu draws, shadow maps drawn %zu kept %zu, layers drawn %zu (all frames: %zu)\n", st.drawn,
                st.things, st.drawCalls, st.mapsDrawn, st.mapsKept, st.layersDrawn, layersAll);
    return 0;
}
