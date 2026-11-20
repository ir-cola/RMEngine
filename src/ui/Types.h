#pragma once
// 核の UI の基本の型 (「土台」: 色も見た目も無い)。

#include "base/Math.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace rm::ui {

using Color = rm::Color;  // データと描画で色の型は 1 つ (base/Math.h)

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    constexpr float Right() const { return x + w; }
    constexpr float Bottom() const { return y + h; }
    constexpr bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    constexpr Rect Inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    constexpr Rect Inset(float l, float t, float r, float b) const { return {x + l, y + t, w - l - r, h - t - b}; }
    Rect Intersect(const Rect& o) const {
        const float nx = std::max(x, o.x), ny = std::max(y, o.y);
        const float nr = std::min(Right(), o.Right()), nb = std::min(Bottom(), o.Bottom());
        return {nx, ny, std::max(0.f, nr - nx), std::max(0.f, nb - ny)};
    }
};

// r の片側から帯を切り取る (r は縮み、帯を返す)。
inline Rect TakeTop(Rect& r, float h) {
    Rect t{r.x, r.y, r.w, h};
    r.y += h;
    r.h -= h;
    return t;
}
inline Rect TakeBottom(Rect& r, float h) {
    r.h -= h;
    return {r.x, r.y + r.h, r.w, h};
}
inline Rect TakeLeft(Rect& r, float w) {
    Rect t{r.x, r.y, w, r.h};
    r.x += w;
    r.w -= w;
    return t;
}
inline Rect TakeRight(Rect& r, float w) {
    r.w -= w;
    return {r.x + r.w, r.y, w, r.h};
}

enum class Key : uint8_t {
    None, Left, Right, Up, Down, Home, End, PageUp, PageDown,
    Backspace, Delete, Enter, Escape, Tab, F2, F5,
    A, C, D, E, F, N, O, P, Q, R, S, V, W, X, Y, Z,
    Space,  // (押している間: 3D のシーンビューが上へ飛ぶ。入力した文字は、これまでどおり文字として届く)
    Count,  // キーの数 (KeyDown 用)
};

struct KeyEvent {
    Key key = Key::None;
    bool ctrl = false, shift = false, alt = false;
};

enum class Mouse : int { Left = 0, Right = 1, Middle = 2 };

// フレームとフレームの間にウィンドウが集めた入力。座標は DIP。
struct Input {
    float mouseX = -1e6f, mouseY = -1e6f;
    bool mouseInside = false;
    bool down[3] = {};
    bool pressed[3] = {};
    bool released[3] = {};
    bool doubleClicked = false;
    float wheel = 0.f;  // ノッチ数。+ はユーザーから遠ざかる向き
    // 前のフレームからマウスが動いた量 (DIP)。マウスをロックしている間 (Context::LockMouse) も数え続け、
    // そのとき mouseX / mouseY はドラッグを始めた所に留まる。
    float mouseDX = 0.f, mouseDY = 0.f;
    bool ctrl = false, shift = false, alt = false;

    bool keyDown[static_cast<int>(Key::Count)] = {};  // 今押している (フレームをまたいで保つ。ウィンドウがフォーカスを失うと消す)

    std::wstring chars;              // 入力された文字
    std::vector<KeyEvent> keys;      // キーの押下 (リピートつき)
    std::wstring imeResult;          // 確定した IME のテキスト
    std::wstring imeComposition;     // 変換中のテキスト (フレームをまたいで残る)
    int imeCursor = 0;

    // 1 フレームだけ生きるものを全部消す。
    void EndFrame() {
        for (int i = 0; i < 3; ++i) pressed[i] = released[i] = false;
        doubleClicked = false;
        wheel = 0.f;
        mouseDX = mouseDY = 0.f;
        chars.clear();
        keys.clear();
        imeResult.clear();
    }
};

enum class Cursor : uint8_t { Arrow, Hand, IBeam, SizeWE, SizeNS };
enum class HAlign : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Center, Bottom };

struct TextStyle {
    float size = 13.f;
    bool bold = false;
    HAlign h = HAlign::Left;
    VAlign v = VAlign::Center;
    bool wrap = false;
    bool ellipsis = true;  // 収まらないときは "…" (1 行のときだけ)
    Color color{1, 1, 1, 1};
};

}  // namespace rm::ui
