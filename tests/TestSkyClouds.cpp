// 地球の空の、場所ごとの雲 (docs/CLIMATE.md の「雲の地図」→「空の絵」) のテスト:
// 向きと雲の層の交わる所 (space3d::CloudMapAt)・空の絵へ渡す形 (LookOf の雲の地図)・天候が地図を
// 空へ渡すこと・GPU で描いた空が CloudMapAt と同じ所に雲を出すこと (シェーダーの CloudsHere と C++ の
// 式を結びつける)・地図が無いときと塗っていないときは今までと 1 ピクセルも変わらないこと。

#include "Test.h"

#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/SkyShader.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"
#include "rm_engine/weather/runtime/Weather.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")

using namespace rm;
using rm::space3d::CloudMapAt;
using rm::space3d::CloudMapPlace;
using rm::space3d::CloudMapPoint;
using rm::space3d::operator+;
using rm::space3d::operator*;

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

Vec3 Unit(Vec3 v) { return space3d::Normalize(v); }

// eye を真ん中にした、天候の地図と同じ大きさの地図 (kCloudMapCells マス、1 マス kCloudMapCell m)。
CloudMapPlace MapAround(Vec3 eye) {
    CloudMapPlace m;
    m.cells = weather::kCloudMapCells;
    m.cell = weather::kCloudMapCell;
    m.firstX = eye.x - (m.cells - 1) * 0.5f * m.cell;
    m.firstZ = eye.z - (m.cells - 1) * 0.5f * m.cell;
    return m;
}

game_x_3d::Sky Noon() {
    game_x_3d::Sky s;
    s.kind = game_x_3d::SkyKind::Earth;
    s.hour = 12;
    return s;
}

// 雲の量と降る量がどのマスも同じ地図 (SkyWeather の形)。
void FillMap(game_x_3d::SkyWeather& w, const CloudMapPlace& m, float cover, float rain) {
    const size_t count = static_cast<size_t>(m.cells) * m.cells;
    w.cloudCovers.assign(count, cover);
    w.cloudRains.assign(count, rain);
    w.cloudCells = m.cells;
    w.cloudCell = m.cell;
    w.cloudFirstX = m.firstX;
    w.cloudFirstZ = m.firstZ;
}

Microsoft::WRL::ComPtr<ID3D11Device> TestDevice(Microsoft::WRL::ComPtr<ID3D11DeviceContext>& ctx) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &ctx);
    if (FAILED(hr)) device.Reset();
    return device;
}

// 絵 (B8G8R8A8) の全部のピクセル。
std::vector<uint8_t> PixelsOf(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* picture) {
    D3D11_TEXTURE2D_DESC td{};
    picture->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) return {};
    ctx->CopyResource(staging.Get(), picture);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return {};
    std::vector<uint8_t> out(static_cast<size_t>(td.Width) * td.Height * 4);
    for (UINT y = 0; y < td.Height; ++y) {
        const auto* row = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch;
        std::copy(row, row + td.Width * 4, out.begin() + static_cast<ptrdiff_t>(y) * td.Width * 4);
    }
    ctx->Unmap(staging.Get(), 0);
    return out;
}

// 2 つの絵のピクセル (x, y) の色の差 (チャンネルの差の一番大きいもの)。
int Differ(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int size, int x, int y) {
    const size_t at = (static_cast<size_t>(y) * size + x) * 4;
    int most = 0;
    for (size_t k = 0; k < 3; ++k) most = std::max(most, std::abs(static_cast<int>(a[at + k]) - static_cast<int>(b[at + k])));
    return most;
}

}  // namespace

RM_TEST(Sky_CloudMapAt) {
    const Vec3 eye{100, 5, -200};
    const CloudMapPlace map = MapAround(eye);
    // 雨雲の層 (低い): 同じ向きでも、雲の層より近い所を読む。
    const CloudMapPoint lowSlant = CloudMapAt(Unit({1, 1, 0}), eye, map, space3d::kRainCloudHeight);
    CHECK(Near(lowSlant.u, 0.5f + space3d::kRainCloudHeight / map.cell / static_cast<float>(map.cells), 1e-3f));
    CHECK(CloudMapAt({0, 1, 0}, eye, map, 0.f).weight == 0.f);
    // 真上: 地図の真ん中 (カメラの真上)。地図の値をそのまま使う。
    CloudMapPoint p = CloudMapAt({0, 1, 0}, eye, map);
    CHECK(Near(p.u, 0.5f) && Near(p.v, 0.5f) && Near(p.weight, 1));
    // 斜め: 雲の層 (kCloudLayerHeight m 上) で x に 600 m、z に -300 m 先。
    const Vec3 slant = Unit({1, 1, -0.5f});
    p = CloudMapAt(slant, eye, map);
    const float n = static_cast<float>(map.cells);
    CHECK(Near(p.u, 0.5f + space3d::kCloudLayerHeight / map.cell / n, 1e-3f));
    CHECK(Near(p.v, 0.5f - 0.5f * space3d::kCloudLayerHeight / map.cell / n, 1e-3f));
    CHECK(Near(p.weight, 1));
    // 地図の縁の近く: 薄れる (縁までの kCloudFadeFrom から)。地図の縁と外: 0。
    const float half = n * 0.5f * map.cell;
    auto reachingX = [&](float distance) { return Unit({distance, space3d::kCloudLayerHeight, 0}); };
    CHECK(Near(CloudMapAt(reachingX(half * 0.6f), eye, map).weight, 1));
    const float mid = CloudMapAt(reachingX(half * 0.85f), eye, map).weight;
    CHECK(mid > 0.1f && mid < 0.9f);
    CHECK(CloudMapAt(reachingX(half * 0.95f), eye, map).weight < mid);
    CHECK(CloudMapAt(reachingX(half * 1.01f), eye, map).weight == 0.f);
    CHECK(CloudMapAt(reachingX(half * 50), eye, map).weight == 0.f);
    // 地平線と下・地図が無い・数でない向き: 使わない。
    CHECK(CloudMapAt({1, 0, 0}, eye, map).weight == 0.f);
    CHECK(CloudMapAt(Unit({1, -0.3f, 0}), eye, map).weight == 0.f);
    CHECK(CloudMapAt({0, 1, 0}, eye, CloudMapPlace{}).weight == 0.f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(CloudMapAt({nan, nan, nan}, eye, map).weight == 0.f);
    // カメラが地図から離れている (エディターのカメラ): 真上でも地図の外なら使わない。
    CHECK(CloudMapAt({0, 1, 0}, {eye.x + 5000, 5, eye.z}, map).weight == 0.f);
}

RM_TEST(Sky_CloudMapPacked) {
    const CloudMapPlace map = MapAround({0, 0, 0});
    game_x_3d::SkyWeather w;
    FillMap(w, map, 0.5f, 0.25f);
    w.cloudCovers[3] = 2.f;  // 0 .. 1 にそろえる
    w.cloudRains[4] = -1.f;
    w.cloudRains[5] = std::numeric_limits<float>::quiet_NaN();
    w.rain = 0.4f;
    w.snow = 0.25f;
    const game_x_3d::SkyLook look = game_x_3d::LookOf(Noon(), 0, w);
    const size_t count = static_cast<size_t>(map.cells) * map.cells;
    CHECK(look.cloudCells == static_cast<uint32_t>(map.cells) && look.clouds.size() == count * 4);
    CHECK(look.clouds[0] == 0.5f && look.clouds[1] == 0.25f && look.clouds[2] == 0.f && look.clouds[3] == 0.f);  // (近さが無い: 0)
    CHECK(look.clouds[3 * 4] == 1.f && look.clouds[4 * 4 + 1] == 0.f && look.clouds[5 * 4 + 1] == 0.f);
    {
        game_x_3d::SkyWeather withNears = w;
        withNears.cloudNears.assign(count, 0.3f);
        withNears.cloudNears[6] = 5.f;
        const game_x_3d::SkyLook nl = game_x_3d::LookOf(Noon(), 0, withNears);
        CHECK(nl.clouds[2] == 0.3f && nl.clouds[6 * 4 + 2] == 1.f);
    }
    const float* m = look.constants.param[6];
    CHECK(m[0] == map.firstX && m[1] == map.firstZ && m[2] == map.cell && m[3] == static_cast<float>(map.cells));
    // 雲の量 (cover) と雨雲 (param[7]) はシーン全体のもの (地図は向きごとにだけ効く)。
    CHECK(look.constants.param[1][0] == w.cover && look.constants.param[7][0] == 0.4f && look.constants.param[7][1] == 0.25f);
    // 数が合わない・マスの大きさが無い・地図が無い・地球の空でない: 地図は渡さない (今までの空)。
    game_x_3d::SkyWeather bad = w;
    bad.cloudCovers.pop_back();
    const game_x_3d::SkyLook none = game_x_3d::LookOf(Noon(), 0, bad);
    CHECK(none.clouds.empty() && none.cloudCells == 0 && none.constants.param[6][3] == 0.f);
    bad = w;
    bad.cloudCell = 0;
    CHECK(game_x_3d::LookOf(Noon(), 0, bad).clouds.empty());
    CHECK(game_x_3d::LookOf(Noon(), 0, game_x_3d::SkyWeather{}).clouds.empty());
    game_x_3d::Sky space;
    space.kind = game_x_3d::SkyKind::Space;
    CHECK(game_x_3d::LookOf(space, 0, w).clouds.empty());
}

// 広げる: 離れるほどなめらかに薄く (縦横)、外は 0、数が合わない: 空。
RM_TEST(Sky_SpreadMax) {
    std::vector<float> in(9 * 9, 0.f), out;
    in[4 * 9 + 4] = 1.f;
    weather::SpreadMax(in, 9, 3.f, out);
    CHECK(out.size() == in.size());
    CHECK(out[4 * 9 + 4] == 1.f && out[4 * 9 + 5] > out[4 * 9 + 6] && out[4 * 9 + 6] > 0.f && out[4 * 9 + 7] == 0.f);
    CHECK(Near(out[4 * 9 + 5], out[5 * 9 + 4]) && out[5 * 9 + 5] < out[4 * 9 + 5]);  // (縦も横も同じ・斜めは薄い)
    weather::SpreadMax(in, 9, 0.f, out);
    CHECK(out == in);
    weather::SpreadMax(std::vector<float>(5, 1.f), 9, 2.f, out);
    CHECK(out.empty());
}

RM_TEST(Sky_CloudMapFromWeather) {
    weather::Weather w;
    w.cloudAmount = 20;
    // 地図が無い: 渡さない。
    CHECK(weather::SkyWeatherOf(w, 0).cloudCovers.empty() && weather::SkyWeatherOf(w, 0).cloudCells == 0);
    const size_t count = static_cast<size_t>(weather::kCloudMapCells) * weather::kCloudMapCells;
    weather::PlaceClouds m;
    m.shares.assign(count, 0.f);
    m.amounts.assign(count, 0.8f);
    m.rains.assign(count, 0.f);
    m.shares[7] = 1.f, m.amounts[7] = 1.5f;     // 塗った雲 (0 .. 1 にそろえる)
    m.shares[8] = 0.5f, m.amounts[8] = 0.6f;    // 半分塗った
    m.firstX = -10;
    m.firstZ = 20;
    game_x_3d::SkyWeather s = weather::SkyWeatherOf(w, 0, &m);
    CHECK(s.cloudCells == weather::kCloudMapCells && s.cloudCell == weather::kCloudMapCell && s.cloudFirstX == -10 && s.cloudFirstZ == 20);
    CHECK(s.cloudCovers.size() == count && s.cloudRains.size() == count && s.cloudNears.size() == count && s.rain == 0.f);
    CHECK(s.cloudCovers[0] == s.cover && s.cloudCovers[7] == 1.f && Near(s.cloudCovers[8], s.cover + (0.6f - s.cover) * 0.5f));
    CHECK(s.cloudRains[0] == 0.f && s.cloudNears[0] == 0.f);  // (雨雲が無い: 近さ 0・雲はそのまま)
    // 描いた雨雲 (真ん中のマス、小雨 0.3): 雨雲はまわりへ広がり (描いた所より大きい)、そのまわりは白い雲が増えて
    // 近いほど少し暗い。遠い所 (地図の隅) は変わらない。
    const int n = weather::kCloudMapCells, mid = n / 2;
    auto at = [n](int i, int j) { return static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i); };
    m.rains[at(mid, mid)] = 0.3f;
    s = weather::SkyWeatherOf(w, 0, &m);
    const int spreadCells = static_cast<int>(weather::kRainCloudSpread / weather::kCloudMapCell);
    CHECK(Near(s.cloudRains[at(mid, mid)], 0.3f));
    CHECK(s.cloudRains[at(mid + 2, mid)] > 0.1f && s.cloudRains[at(mid, mid - 2)] > 0.1f);              // まわり 48 m にも雨雲
    CHECK(s.cloudRains[at(mid + spreadCells + 2, mid)] == 0.f);                                         // 広げる所の外は無し
    CHECK(s.cloudNears[at(mid, mid)] > 0.99f && s.cloudNears[at(mid + 8, mid)] > 0.1f);
    CHECK(s.cloudNears[at(mid + 8, mid)] > s.cloudNears[at(mid + 12, mid)]);                            // 近いほど暗い
    CHECK(s.cloudCovers[at(mid + 8, mid)] > s.cover + 0.1f && s.cloudCovers[at(mid + 8, mid)] <= weather::kRainHaloCover + 1e-4f);  // 白い雲が増える
    CHECK(s.cloudCovers[at(n - 1, n - 1)] == s.cover && s.cloudNears[at(n - 1, n - 1)] == 0.f);
    // シーン全体が降っている: どのマスにもその雨雲 (雨雲の所は多いほう)。
    w.precipitation = 50;
    s = weather::SkyWeatherOf(w, 0, &m);
    CHECK(Near(s.rain, 0.5f) && Near(s.cloudRains[0], 0.5f) && Near(s.cloudRains[at(mid, mid)], 0.5f));
    m.rains[at(mid, mid)] = 0.9f;
    CHECK(Near(weather::SkyWeatherOf(w, 0, &m).cloudRains[at(mid, mid)], 0.9f));
    // 数が違う: 渡さない。
    m.rains.pop_back();
    CHECK(weather::SkyWeatherOf(w, 0, &m).cloudCells == 0);
    // カメラのいる所だけ降る雨雲は、空全体の暗さを変えない (雨雲の層は地図の所だけ)。
    weather::Weather local;
    const float darkBefore = weather::SkyWeatherOf(local, 0).dark;
    local.placeSnow = 0;
    local.placeMore = 1;
    local.placeLocal = 1;
    CHECK(weather::SkyWeatherOf(local, 0).dark == darkBefore && weather::SkyWeatherOf(local, 0).rain == 0.f);
    CHECK(weather::SkyWeatherOf(local, 0).through < weather::SkyWeatherOf(weather::Weather{}, 0).through);  // (日の光は弱まる)
}

// 空を描いた絵でも、手前のモデルが出る (空の雲の地図と、モデルの things は同じ t6: 空のあとに結び直す)。
RM_TEST(Sky_MeshInFrontOfSky) {
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    const auto device = TestDevice(ctx);
    CHECK(device);
    if (!device) return;
    space3d::Renderer r;
    std::string error;
    CHECK(r.Init(device.Get(), &error));
    if (!r.Ready()) return;
    space3d::SkyDraw draw;
    draw.program = r.MakeSky(space3d::AssembleSky(space3d::BuiltinSkyCode(space3d::BuiltinSky::Earth), "地球の空"), &error);
    CHECK(draw.program);
    if (!draw.program) return;
    const game_x_3d::SkyLook l = game_x_3d::LookOf(Noon(), 0, game_x_3d::SkyWeather{});
    draw.constants = l.constants;

    constexpr int kSize = 32;
    const Vec3 eye{0, 1, -5};
    space3d::RenderFrame f;
    f.width = f.height = kSize;
    f.view = space3d::LookAt(eye, {0, 1, 0}, {0, 1, 0});
    f.projection = space3d::Perspective(60, 1, 0.1f, 100);
    f.sky = &draw;
    space3d::Transform t;
    t.position = {0, 1, 0};
    t.scale = {2, 2, 2};
    const space3d::Box box{space3d::LocalMatrix(t), Color{1, 0, 0, 1}, false};
    auto render = [&](bool withBox) {
        f.boxes = withBox ? std::span<const space3d::Box>(&box, 1) : std::span<const space3d::Box>();
        ID3D11Texture2D* picture = r.Render(ctx.Get(), f);
        return picture ? PixelsOf(device.Get(), ctx.Get(), picture) : std::vector<uint8_t>{};
    };
    const std::vector<uint8_t> skyOnly = render(false), withBox = render(true);
    CHECK(skyOnly.size() == static_cast<size_t>(kSize) * kSize * 4 && withBox.size() == skyOnly.size());
    if (withBox.size() != skyOnly.size() || skyOnly.empty()) return;
    // 真ん中のピクセル: 赤い箱 (B8G8R8A8: 赤が一番強い)。
    const size_t at = (static_cast<size_t>(kSize / 2) * kSize + kSize / 2) * 4;
    CHECK(Differ(withBox, skyOnly, kSize, kSize / 2, kSize / 2) > 30);
    CHECK(withBox[at + 2] > withBox[at] + 20 && withBox[at + 2] > withBox[at + 1] + 20);
}

RM_TEST(Sky_CloudMapOnGpu) {
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    const auto device = TestDevice(ctx);
    CHECK(device);
    if (!device) return;
    space3d::Renderer r;
    std::string error;
    CHECK(r.Init(device.Get(), &error));
    if (!r.Ready()) return;
    space3d::SkyDraw draw;
    draw.program = r.MakeSky(space3d::AssembleSky(space3d::BuiltinSkyCode(space3d::BuiltinSky::Earth), "地球の空"), &error);
    CHECK(draw.program);
    if (!draw.program) return;

    constexpr int kSize = 64;
    const Vec3 eye{100, 5, -200};
    const Vec3 look = Unit({0.3f, 1, 0.4f});  // 上の方を見る
    space3d::RenderFrame f;
    f.width = f.height = kSize;
    f.view = space3d::LookAt(eye, eye + look * 10.f, {0, 0, 1});
    f.projection = space3d::Perspective(60, 1, 0.1f, 100);
    f.sky = &draw;
    // ピクセル (x, y) が見ている向き (シェーダーと同じ: カメラの回転だけの逆行列)。
    space3d::Mat4 turn = f.view;
    turn.m[3][0] = turn.m[3][1] = turn.m[3][2] = 0;
    const auto inv = space3d::Inverse(f.projection * turn);
    CHECK(inv.has_value());
    if (!inv) return;
    auto dirOf = [&](const space3d::Mat4& inverse, int x, int y) {
        const float nx = (static_cast<float>(x) + 0.5f) / kSize * 2 - 1, ny = 1 - (static_cast<float>(y) + 0.5f) / kSize * 2;
        return space3d::ScreenRay(inverse, nx, ny).dir;
    };
    auto dirAt = [&](int x, int y) { return dirOf(*inv, x, y); };
    auto render = [&](const game_x_3d::SkyWeather& w) {
        const game_x_3d::SkyLook l = game_x_3d::LookOf(Noon(), 0, w);
        draw.constants = l.constants;
        draw.clouds = l.clouds;
        draw.cloudCells = l.cloudCells;
        ID3D11Texture2D* picture = r.Render(ctx.Get(), f);
        return picture ? PixelsOf(device.Get(), ctx.Get(), picture) : std::vector<uint8_t>{};
    };

    // 晴れ (雲 0)。地図なし・塗っていない地図 (どのマスもシーン全体と同じ値) は、まったく同じ絵。
    game_x_3d::SkyWeather clear;
    clear.cover = 0;
    clear.dark = 0;
    clear.haze = 0;
    const std::vector<uint8_t> plain = render(clear);
    CHECK(plain.size() == static_cast<size_t>(kSize) * kSize * 4);
    if (plain.empty()) return;
    const CloudMapPlace map = MapAround(eye);
    game_x_3d::SkyWeather unpainted = clear;
    FillMap(unpainted, map, 0.f, 0.f);
    CHECK(render(unpainted) == plain);

    // 真ん中のピクセルが見る所 (CloudMapAt) のまわりの 5 x 5 マスだけに、厚い雲 (100 %) を塗る。
    const int cx = kSize / 2, cy = kSize / 2;
    const CloudMapPoint hit = CloudMapAt(dirAt(cx, cy), eye, map);
    CHECK(hit.weight > 0.99f);
    const int ci = static_cast<int>(std::floor(hit.u * map.cells)), cj = static_cast<int>(std::floor(hit.v * map.cells));
    game_x_3d::SkyWeather painted = clear;
    FillMap(painted, map, 0.f, 0.f);
    for (int j = cj - 2; j <= cj + 2; ++j) {
        for (int i = ci - 2; i <= ci + 2; ++i) {
            if (i < 0 || j < 0 || i >= map.cells || j >= map.cells) continue;
            painted.cloudCovers[static_cast<size_t>(j) * map.cells + i] = 1;
        }
    }
    const std::vector<uint8_t> cloudy = render(painted);
    CHECK(cloudy.size() == plain.size());
    if (cloudy.size() != plain.size()) return;
    // 塗った所を見るピクセルは雲になり、塗った所から 4 マスより離れた所を見るピクセルは晴れのまま。
    CHECK(Differ(cloudy, plain, kSize, cx, cy) > 20);
    int away = 0, awaySame = 0;
    for (int y = 0; y < kSize; y += 4) {
        for (int x = 0; x < kSize; x += 4) {
            const CloudMapPoint q = CloudMapAt(dirAt(x, y), eye, map);
            const float di = std::fabs(q.u * map.cells - (ci + 0.5f)), dj = std::fabs(q.v * map.cells - (cj + 0.5f));
            if (std::max(di, dj) < 4) continue;
            ++away;
            if (Differ(cloudy, plain, kSize, x, y) == 0) ++awaySame;
        }
    }
    CHECK(away > 20 && awaySame == away);

    // 雨雲: 真ん中のピクセルが雨雲の層 (kRainCloudHeight m 上) で見る所のまわりに、降る量だけ置く (雲の量 0)。
    // 降る量が多いほど厚く暗い (小雨は切れ切れの雲、嵐は切れ目のない黒ずんだ層)。
    const CloudMapPoint rainHit = CloudMapAt(dirAt(cx, cy), eye, map, space3d::kRainCloudHeight);
    CHECK(rainHit.weight > 0.99f);
    const int ri = static_cast<int>(std::floor(rainHit.u * map.cells)), rj = static_cast<int>(std::floor(rainHit.v * map.cells));
    auto rainAt = [&](float rain) {
        game_x_3d::SkyWeather wet = clear;
        FillMap(wet, map, 0.f, 0.f);
        for (int j = rj - 2; j <= rj + 2; ++j) {
            for (int i = ri - 2; i <= ri + 2; ++i) {
                if (i >= 0 && j >= 0 && i < map.cells && j < map.cells) wet.cloudRains[static_cast<size_t>(j) * map.cells + i] = rain;
            }
        }
        return render(wet);
    };
    const std::vector<uint8_t> drizzle = rainAt(0.2f), storm = rainAt(1.f);
    CHECK(drizzle.size() == plain.size() && storm.size() == plain.size());
    if (drizzle.size() == plain.size() && storm.size() == plain.size()) {
        const size_t at = (static_cast<size_t>(cy) * kSize + cx) * 4;
        auto bright = [&](const std::vector<uint8_t>& p) { return static_cast<int>(p[at]) + p[at + 1] + p[at + 2]; };
        auto change = [&](const std::vector<uint8_t>& p) {
            int sum = 0;
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) sum += Differ(p, plain, kSize, x, y);
            }
            return sum;
        };
        CHECK(Differ(storm, plain, kSize, cx, cy) > 20 && bright(storm) < bright(plain));  // 嵐: 真上が黒ずんだ厚い雲
        CHECK(change(drizzle) > 0 && change(storm) > change(drizzle));                    // 小雨でも雲が出る・嵐ほど変わる
        // 雲の量 (cover) の地図には何も塗っていない: 上の雲の層は変わらない (変わったのは雨雲の層)。
    }

    // 地平線の近く (層と交わる所が地図の外): 地図ぜんぶに厚い雲を塗っても、雲の量 (cover) のまま。
    f.view = space3d::LookAt(eye, eye + Unit({0, 0.03f, 1}) * 10.f, {0, 1, 0});
    turn = f.view;
    turn.m[3][0] = turn.m[3][1] = turn.m[3][2] = 0;
    const auto invLow = space3d::Inverse(f.projection * turn);
    CHECK(invLow.has_value());
    if (!invLow) return;
    const Vec3 low = dirOf(*invLow, cx, cy);
    CHECK(CloudMapAt(low, eye, map).weight == 0.f);
    const std::vector<uint8_t> lowPlain = render(clear);
    game_x_3d::SkyWeather everywhere = clear;
    FillMap(everywhere, map, 1.f, 1.f);
    const std::vector<uint8_t> lowCloudy = render(everywhere);
    CHECK(lowPlain.size() == lowCloudy.size());
    if (lowPlain.size() == lowCloudy.size()) CHECK(Differ(lowCloudy, lowPlain, kSize, cx, cy) == 0);
}
