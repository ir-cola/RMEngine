#pragma once
// Basic types of the core UI (the "foundation": no colors, no look).

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace rm::ui {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    static constexpr Color Hex(uint32_t rgb, float alpha = 1.f) {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha};
    }
    constexpr Color Alpha(float na) const { return {r, g, b, na}; }
};

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

// Cut a strip off one side of r (r shrinks, the strip is returned).
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
    A, C, N, O, S, V, X, Y, Z,
};

struct KeyEvent {
    Key key = Key::None;
    bool ctrl = false, shift = false, alt = false;
};

enum class Mouse : int { Left = 0, Right = 1, Middle = 2 };

// Input collected by the window between two frames. Coordinates are DIPs.
struct Input {
    float mouseX = -1e6f, mouseY = -1e6f;
    bool mouseInside = false;
    bool down[3] = {};
    bool pressed[3] = {};
    bool released[3] = {};
    bool doubleClicked = false;
    float wheel = 0.f;  // notches, + = away from the user
    bool ctrl = false, shift = false, alt = false;

    std::wstring chars;              // typed characters
    std::vector<KeyEvent> keys;      // key presses (with repeat)
    std::wstring imeResult;          // committed IME text
    std::wstring imeComposition;     // text being composed (persists across frames)
    int imeCursor = 0;

    // Clears everything that only lives for one frame.
    void EndFrame() {
        for (int i = 0; i < 3; ++i) pressed[i] = released[i] = false;
        doubleClicked = false;
        wheel = 0.f;
        chars.clear();
        keys.clear();
        imeResult.clear();
    }
};

enum class Cursor : uint8_t { Arrow, Hand, IBeam };
enum class HAlign : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Center, Bottom };

struct TextStyle {
    float size = 13.f;
    bool bold = false;
    HAlign h = HAlign::Left;
    VAlign v = VAlign::Center;
    bool wrap = false;
    bool ellipsis = true;  // "…" when it does not fit (single line only)
    Color color{1, 1, 1, 1};
};

}  // namespace rm::ui
