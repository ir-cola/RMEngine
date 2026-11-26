#include "rm_engine/theme_gaming/editor/Gaming.h"

#include "editor/widgets/MovingBackdrop.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/theme_gaming/editor/Motion.h"
#include "rm_engine/theme_gaming/editor/Scenes.h"
#include "ui/Context.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

namespace rm::theme_gaming {

namespace {

using Microsoft::WRL::ComPtr;
using ui::Rect;

void DrawWindow(ui::Context& c, const Rect&, bool floating) { editor::w::DrawMovingBackdrop(c, scenes::kGaming, floating); }

void DrawSurface(ui::Context& c, const Rect& r, float corner, bool floating) {
    editor::w::DrawMovingBackdropOn(c, scenes::kGaming, r, corner, floating, scenes::kPanelStrength);
}

// 虹のグラデーションのブラシ (一回り。端の外はくり返す)。描く先 (デバイスコンテキスト) ごとに 1 つ。
ID2D1LinearGradientBrush* RainbowBrush(ID2D1DeviceContext* dc) {
    static ID2D1DeviceContext* owner = nullptr;  // (比べるだけ。持たない)
    static ComPtr<ID2D1Device> device;          // (GPU が作り直されたら、同じ場所に新しい描く先ができても作り直す)
    static ComPtr<ID2D1LinearGradientBrush> brush;
    ComPtr<ID2D1Device> now;
    dc->GetDevice(&now);
    if (owner == dc && device == now && brush) return brush.Get();
    brush.Reset();
    owner = dc;
    device = now;
    D2D1_GRADIENT_STOP stops[7];
    for (int i = 0; i < 7; ++i) {
        const Color k = motion::Rainbow(static_cast<float>(i) / 6.f);
        stops[i] = {static_cast<float>(i) / 6.f, D2D1::ColorF(k.r, k.g, k.b, 1.f)};
    }
    ComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(dc->CreateGradientStopCollection(stops, 7, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_WRAP, &collection)) ||
        FAILED(dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(1, 0)), collection.Get(), &brush))) {
        brush.Reset();
    }
    return brush.Get();
}

// パネルの縁を時計回りに流れる虹 (ウィンドウの縁の虹と同じ時計・同じ長さで一回り)。辺ごとに、その辺に沿って
// 色の変わるブラシで、角の丸い枠を、その辺の帯に切り取って描く (角の丸みも、となりの辺とつながる)。
void Panel(ui::Context& c, const Rect& r, float corner) {
    if (r.w < 8 || r.h < 8) return;
    ID2D1DeviceContext* dc = c.P().DC();
    ID2D1LinearGradientBrush* brush = RainbowBrush(dc);
    if (!brush) return;
    const double t = editor::w::MovingTime(c);
    const float band = std::min({std::max(corner, 2.f) + 4.f, r.w * 0.5f, r.h * 0.5f});
    // 辺: 時計回りの始まりの点、向き、切り取る帯。
    const struct Edge {
        D2D1_POINT_2F from;
        D2D1_POINT_2F dir;
        D2D1_RECT_F clip;
    } edges[4] = {
        {{r.x, r.y}, {1, 0}, {r.x, r.y, r.x + r.w, r.y + band}},
        {{r.x + r.w, r.y}, {0, 1}, {r.x + r.w - band, r.y + band, r.x + r.w, r.y + r.h - band}},
        {{r.x + r.w, r.y + r.h}, {-1, 0}, {r.x, r.y + r.h - band, r.x + r.w, r.y + r.h}},
        {{r.x, r.y + r.h}, {0, -1}, {r.x, r.y + band, r.x + band, r.y + r.h - band}},
    };
    const float period = scenes::kRainbowPeriod;
    for (int i = 0; i < 4; ++i) {
        const Edge& e = edges[i];
        // 色の位置が 0 になる所 (この辺の始まりからの長さ) から、一回りの長さ先までを、ブラシの 0 .. 1 にする。
        const float s0 = motion::PerimeterAt(r, i, 0.f);
        const float x0 = -motion::EdgeHue(s0, t, period, scenes::kRainbowSpeed) * period;
        const D2D1_POINT_2F g0{e.from.x + e.dir.x * x0, e.from.y + e.dir.y * x0};
        brush->SetStartPoint(g0);
        brush->SetEndPoint({g0.x + e.dir.x * period, g0.y + e.dir.y * period});
        dc->PushAxisAlignedClip(e.clip, D2D1_ANTIALIAS_MODE_ALIASED);
        brush->SetOpacity(0.16f);  // 内へにじむ光
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.x + 2.5f, r.y + 2.5f, r.x + r.w - 2.5f, r.y + r.h - 2.5f), std::max(corner - 2.5f, 0.f),
                                                   std::max(corner - 2.5f, 0.f)),
                                 brush, 5.f);
        brush->SetOpacity(0.85f);  // 縁の線
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.x + 0.75f, r.y + 0.75f, r.x + r.w - 0.75f, r.y + r.h - 0.75f), std::max(corner - 0.75f, 0.f),
                                                   std::max(corner - 0.75f, 0.f)),
                                 brush, 1.5f);
        dc->PopAxisAlignedClip();
    }
}

constexpr editor::w::OrnamentInfo kGaming{"gaming", &DrawWindow, &Panel, &DrawSurface};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) { registry.Contribute(editor::w::kOrnamentPoint, kGaming); }

}  // namespace rm::theme_gaming
