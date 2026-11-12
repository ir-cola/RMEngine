#include "rm_engine/space3d/runtime/BlockCompress.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace rm::space3d {

using asset::TextureCompression;

PictureTraits TraitsOf(const platform::Image& rgba) {
    PictureTraits t;
    t.gray = true;
    const size_t n = std::min(rgba.pixels.size() / 4, static_cast<size_t>(rgba.width) * rgba.height);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = &rgba.pixels[i * 4];
        if (p[3] < 255) t.alpha = true;
        if (p[0] != p[1] || p[1] != p[2]) t.gray = false;
    }
    return t;
}

TextureFormat ChooseFormat(TextureUse use, TextureCompression setting, PictureTraits traits, bool canBC7, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || width % 4 != 0 || height % 4 != 0) return TextureFormat::Rgba8;
    auto automatic = [&] {
        switch (use) {
            case TextureUse::Normal: return TextureFormat::BC5;
            case TextureUse::Data: return traits.gray ? TextureFormat::BC4 : TextureFormat::BC1;
            case TextureUse::Color: break;
        }
        return traits.alpha ? (canBC7 ? TextureFormat::BC7 : TextureFormat::BC3) : TextureFormat::BC1;
    };
    // What the use can read: a colour needs three channels; a normal map two; numbers one only when grey.
    auto fits = [&](TextureFormat f) {
        switch (f) {
            case TextureFormat::BC4: return use == TextureUse::Data && traits.gray;
            case TextureFormat::BC5: return use == TextureUse::Normal;
            default: return true;
        }
    };
    TextureFormat f = TextureFormat::Rgba8;
    switch (setting) {
        case TextureCompression::None: return TextureFormat::Rgba8;
        case TextureCompression::Auto: return automatic();
        case TextureCompression::BC1: f = TextureFormat::BC1; break;
        case TextureCompression::BC3: f = TextureFormat::BC3; break;
        case TextureCompression::BC4: f = TextureFormat::BC4; break;
        case TextureCompression::BC5: f = TextureFormat::BC5; break;
        case TextureCompression::BC7: f = canBC7 ? TextureFormat::BC7 : TextureFormat::BC3; break;
    }
    return fits(f) ? f : automatic();
}

// --- BC1 (and the colour half of BC3) ----------------------------------------------------------

namespace {

uint16_t Pack565(const float c[3]) {
    const auto q = [](float v, int most) { return static_cast<uint32_t>(std::clamp(std::lround(v / 255.f * most), 0L, static_cast<long>(most))); };
    return static_cast<uint16_t>((q(c[0], 31) << 11) | (q(c[1], 63) << 5) | q(c[2], 31));
}

void Unpack565(uint16_t v, int out[3]) {
    const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[0] = (r << 3) | (r >> 2);
    out[1] = (g << 2) | (g >> 4);
    out[2] = (b << 3) | (b >> 2);
}

// The 4 colours of a block with c0 > c1 (or 3 + black when c0 <= c1: BC1's other mode).
void Palette(uint16_t c0, uint16_t c1, bool fourAlways, int pal[4][4]) {
    int a[3], b[3];
    Unpack565(c0, a);
    Unpack565(c1, b);
    for (int k = 0; k < 3; ++k) {
        pal[0][k] = a[k];
        pal[1][k] = b[k];
        if (c0 > c1 || fourAlways) {
            pal[2][k] = (2 * a[k] + b[k] + 1) / 3;
            pal[3][k] = (a[k] + 2 * b[k] + 1) / 3;
        } else {
            pal[2][k] = (a[k] + b[k]) / 2;
            pal[3][k] = 0;
        }
    }
    pal[0][3] = pal[1][3] = pal[2][3] = 255;
    pal[3][3] = (c0 > c1 || fourAlways) ? 255 : 0;
}

int Distance2(const int a[3], const uint8_t* b) {
    const int dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return dr * dr + dg * dg + db * db;
}

// Indices of the 16 texels against two 565 end colours (4-colour mode); the squared error.
long long Fit(const uint8_t rgba[64], uint16_t c0, uint16_t c1, uint32_t& indices) {
    int pal[4][4];
    Palette(c0, c1, true, pal);
    indices = 0;
    long long error = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, bestD = Distance2(pal[0], rgba + i * 4);
        for (int k = 1; k < 4; ++k) {
            const int d = Distance2(pal[k], rgba + i * 4);
            if (d < bestD) best = k, bestD = d;
        }
        indices |= static_cast<uint32_t>(best) << (2 * i);
        error += bestD;
    }
    return error;
}

void Put16(uint8_t* out, uint16_t v) {
    out[0] = static_cast<uint8_t>(v & 0xFF);
    out[1] = static_cast<uint8_t>(v >> 8);
}

void Put32(uint8_t* out, uint32_t v) {
    for (int k = 0; k < 4; ++k) out[k] = static_cast<uint8_t>(v >> (8 * k));
}

// The colour block: end points along the colours' main direction (the covariance's strongest axis),
// pulled in a little, then once more by least squares on the chosen indices. Always 4-colour mode
// (c0 > c1; equal: one colour), as BC3 needs too.
void EncodeColorBlock(const uint8_t rgba[64], uint8_t out[8]) {
    float mean[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) {
        for (int k = 0; k < 3; ++k) mean[k] += rgba[i * 4 + k] / 16.f;
    }
    float cov[6] = {0, 0, 0, 0, 0, 0};  // rr rg rb gg gb bb
    for (int i = 0; i < 16; ++i) {
        const float d[3] = {rgba[i * 4] - mean[0], rgba[i * 4 + 1] - mean[1], rgba[i * 4 + 2] - mean[2]};
        cov[0] += d[0] * d[0], cov[1] += d[0] * d[1], cov[2] += d[0] * d[2];
        cov[3] += d[1] * d[1], cov[4] += d[1] * d[2], cov[5] += d[2] * d[2];
    }
    float axis[3] = {0.577f, 0.577f, 0.577f};
    for (int it = 0; it < 8; ++it) {  // power iteration
        const float x = cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2];
        const float y = cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2];
        const float z = cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2];
        const float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-6f) break;
        axis[0] = x / len, axis[1] = y / len, axis[2] = z / len;
    }
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 16; ++i) {
        const float t = (rgba[i * 4] - mean[0]) * axis[0] + (rgba[i * 4 + 1] - mean[1]) * axis[1] + (rgba[i * 4 + 2] - mean[2]) * axis[2];
        lo = std::min(lo, t), hi = std::max(hi, t);
    }
    const float inset = (hi - lo) / 16.f;
    lo += inset, hi -= inset;
    float e0[3], e1[3];
    for (int k = 0; k < 3; ++k) e0[k] = mean[k] + axis[k] * hi, e1[k] = mean[k] + axis[k] * lo;
    uint16_t c0 = Pack565(e0), c1 = Pack565(e1);
    if (c0 < c1) std::swap(c0, c1);
    uint32_t indices = 0;
    long long error = c0 == c1 ? 0 : Fit(rgba, c0, c1, indices);
    // Least squares: the end colours that best explain the chosen indices (weights 1, 0, 2/3, 1/3).
    if (c0 != c1) {
        float aa = 0, bb = 0, ab = 0, ax[3] = {0, 0, 0}, bx[3] = {0, 0, 0};
        static constexpr float kW[4] = {1.f, 0.f, 2.f / 3.f, 1.f / 3.f};
        for (int i = 0; i < 16; ++i) {
            const float w = kW[(indices >> (2 * i)) & 3], v = 1 - w;
            aa += w * w, bb += v * v, ab += w * v;
            for (int k = 0; k < 3; ++k) ax[k] += w * rgba[i * 4 + k], bx[k] += v * rgba[i * 4 + k];
        }
        const float det = aa * bb - ab * ab;
        if (std::abs(det) > 1e-6f) {
            float n0[3], n1[3];
            for (int k = 0; k < 3; ++k) {
                n0[k] = std::clamp((ax[k] * bb - bx[k] * ab) / det, 0.f, 255.f);
                n1[k] = std::clamp((bx[k] * aa - ax[k] * ab) / det, 0.f, 255.f);
            }
            uint16_t d0 = Pack565(n0), d1 = Pack565(n1);
            if (d0 < d1) std::swap(d0, d1);
            if (d0 != d1) {
                uint32_t idx2 = 0;
                const long long e2 = Fit(rgba, d0, d1, idx2);
                if (e2 < error) c0 = d0, c1 = d1, indices = idx2, error = e2;
            }
        }
    }
    if (c0 == c1) indices = 0;
    Put16(out, c0);
    Put16(out + 2, c1);
    Put32(out + 4, indices);
}

void DecodeColorBlock(const uint8_t in[8], bool fourAlways, uint8_t rgba[64]) {
    const uint16_t c0 = static_cast<uint16_t>(in[0] | (in[1] << 8)), c1 = static_cast<uint16_t>(in[2] | (in[3] << 8));
    int pal[4][4];
    Palette(c0, c1, fourAlways, pal);
    const uint32_t indices = static_cast<uint32_t>(in[4]) | (static_cast<uint32_t>(in[5]) << 8) | (static_cast<uint32_t>(in[6]) << 16) |
                             (static_cast<uint32_t>(in[7]) << 24);
    for (int i = 0; i < 16; ++i) {
        const int k = (indices >> (2 * i)) & 3;
        for (int c = 0; c < 4; ++c) rgba[i * 4 + c] = static_cast<uint8_t>(pal[k][c]);
    }
}

// --- BC4 (one channel; BC3's alpha and each half of BC5) ---------------------------------------

// The 8 values of a block: a0 > a1 - 8 steps between; otherwise 6 steps and 0 and 255.
void Values(uint8_t a0, uint8_t a1, int out[8]) {
    out[0] = a0, out[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) out[i + 1] = static_cast<int>(std::lround(((7 - i) * a0 + i * a1) / 7.0));
    } else {
        for (int i = 1; i <= 4; ++i) out[i + 1] = static_cast<int>(std::lround(((5 - i) * a0 + i * a1) / 5.0));
        out[6] = 0, out[7] = 255;
    }
}

}  // namespace

void EncodeBC4(const uint8_t values[16], uint8_t out[8]) {
    uint8_t lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i) lo = std::min(lo, values[i]), hi = std::max(hi, values[i]);
    out[0] = hi, out[1] = lo;
    uint64_t bits = 0;
    if (hi > lo) {  // 8-step mode
        int pal[8];
        Values(hi, lo, pal);
        for (int i = 0; i < 16; ++i) {
            int best = 0, bestD = 1 << 30;
            for (int k = 0; k < 8; ++k) {
                const int d = std::abs(pal[k] - values[i]);
                if (d < bestD) best = k, bestD = d;
            }
            bits |= static_cast<uint64_t>(best) << (3 * i);
        }
    }
    for (int k = 0; k < 6; ++k) out[2 + k] = static_cast<uint8_t>(bits >> (8 * k));
}

void DecodeBC4(const uint8_t in[8], uint8_t values[16]) {
    int pal[8];
    Values(in[0], in[1], pal);
    uint64_t bits = 0;
    for (int k = 0; k < 6; ++k) bits |= static_cast<uint64_t>(in[2 + k]) << (8 * k);
    for (int i = 0; i < 16; ++i) values[i] = static_cast<uint8_t>(pal[(bits >> (3 * i)) & 7]);
}

void EncodeBC1(const uint8_t rgba[64], uint8_t out[8]) { EncodeColorBlock(rgba, out); }
void DecodeBC1(const uint8_t in[8], uint8_t rgba[64]) { DecodeColorBlock(in, false, rgba); }

namespace {

void Channel(const uint8_t rgba[64], int c, uint8_t out[16]) {
    for (int i = 0; i < 16; ++i) out[i] = rgba[i * 4 + c];
}

}  // namespace

void EncodeBC5(const uint8_t rgba[64], uint8_t out[16]) {
    uint8_t ch[16];
    Channel(rgba, 0, ch);
    EncodeBC4(ch, out);
    Channel(rgba, 1, ch);
    EncodeBC4(ch, out + 8);
}

void DecodeBC5(const uint8_t in[16], uint8_t rgba[64]) {
    uint8_t r[16], g[16];
    DecodeBC4(in, r);
    DecodeBC4(in + 8, g);
    for (int i = 0; i < 16; ++i) rgba[i * 4] = r[i], rgba[i * 4 + 1] = g[i], rgba[i * 4 + 2] = 0, rgba[i * 4 + 3] = 255;
}

void EncodeBC3(const uint8_t rgba[64], uint8_t out[16]) {
    uint8_t alpha[16];
    Channel(rgba, 3, alpha);
    EncodeBC4(alpha, out);
    EncodeColorBlock(rgba, out + 8);
}

void DecodeBC3(const uint8_t in[16], uint8_t rgba[64]) {
    uint8_t alpha[16];
    DecodeBC4(in, alpha);
    DecodeColorBlock(in + 8, true, rgba);
    for (int i = 0; i < 16; ++i) rgba[i * 4 + 3] = alpha[i];
}

// --- BC7, mode 6 -------------------------------------------------------------------------------
// One of BC7's eight modes: one line through RGBA (two end colours of 7 bits + a shared lowest bit
// each) and a 4-bit place on it for every texel (16 steps). It keeps alpha as well as colour, which
// is what BC7 is used for here (colours with see-through parts); the other modes (two or three lines
// per block) would be finer on blocks of very different colours.

namespace {

constexpr int kWeights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

// Writes `count` bits of `v` at bit `at` (LSB first), as BC7 lays its fields.
void PutBits(uint8_t out[16], int& at, uint32_t v, int count) {
    for (int i = 0; i < count; ++i, ++at) {
        if ((v >> i) & 1) out[at >> 3] |= static_cast<uint8_t>(1u << (at & 7));
    }
}

uint32_t GetBits(const uint8_t in[16], int& at, int count) {
    uint32_t v = 0;
    for (int i = 0; i < count; ++i, ++at) v |= static_cast<uint32_t>((in[at >> 3] >> (at & 7)) & 1) << i;
    return v;
}

// An end colour as 7 bits + the shared lowest bit, whichever bit lands nearer.
void Quantize7p(const float e[4], int q[4], int& p) {
    int best = 1 << 30;
    for (int bit = 0; bit < 2; ++bit) {
        int err = 0, qq[4];
        for (int k = 0; k < 4; ++k) {
            qq[k] = std::clamp(static_cast<int>(std::lround((e[k] - bit) / 2.f)), 0, 127);
            const int d = (qq[k] * 2 + bit) - static_cast<int>(std::lround(e[k]));
            err += d * d;
        }
        if (err < best) {
            best = err;
            p = bit;
            for (int k = 0; k < 4; ++k) q[k] = qq[k];
        }
    }
}

// The 16 colours between two quantized end points; the indices of the texels and the squared error.
long long Fit7(const uint8_t rgba[64], const int q0[4], int p0, const int q1[4], int p1, uint8_t idx[16]) {
    int pal[16][4];
    for (int k = 0; k < 4; ++k) {
        const int a = q0[k] * 2 + p0, b = q1[k] * 2 + p1;
        for (int i = 0; i < 16; ++i) pal[i][k] = ((64 - kWeights4[i]) * a + kWeights4[i] * b + 32) >> 6;
    }
    long long error = 0;
    for (int t = 0; t < 16; ++t) {
        int best = 0, bestD = 1 << 30;
        for (int i = 0; i < 16; ++i) {
            int d = 0;
            for (int k = 0; k < 4; ++k) {
                const int x = pal[i][k] - rgba[t * 4 + k];
                d += x * x;
            }
            if (d < bestD) best = i, bestD = d;
        }
        idx[t] = static_cast<uint8_t>(best);
        error += bestD;
    }
    return error;
}

}  // namespace

void EncodeBC7(const uint8_t rgba[64], uint8_t out[16]) {
    // The end points: along the strongest direction of the 16 RGBA values (power iteration on the
    // covariance), pulled in a little; then once more by least squares on the chosen indices.
    float mean[4] = {0, 0, 0, 0};
    for (int t = 0; t < 16; ++t) {
        for (int k = 0; k < 4; ++k) mean[k] += rgba[t * 4 + k] / 16.f;
    }
    float cov[4][4] = {};
    for (int t = 0; t < 16; ++t) {
        float d[4];
        for (int k = 0; k < 4; ++k) d[k] = rgba[t * 4 + k] - mean[k];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) cov[i][j] += d[i] * d[j];
        }
    }
    float axis[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    for (int it = 0; it < 8; ++it) {
        float v[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) v[i] += cov[i][j] * axis[j];
        }
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
        if (len < 1e-6f) break;
        for (int k = 0; k < 4; ++k) axis[k] = v[k] / len;
    }
    float lo = 1e9f, hi = -1e9f;
    for (int t = 0; t < 16; ++t) {
        float proj = 0;
        for (int k = 0; k < 4; ++k) proj += (rgba[t * 4 + k] - mean[k]) * axis[k];
        lo = std::min(lo, proj), hi = std::max(hi, proj);
    }
    const float inset = (hi - lo) / 32.f;
    lo += inset, hi -= inset;
    float e0[4], e1[4];
    for (int k = 0; k < 4; ++k) {
        e0[k] = std::clamp(mean[k] + axis[k] * lo, 0.f, 255.f);
        e1[k] = std::clamp(mean[k] + axis[k] * hi, 0.f, 255.f);
    }
    int q0[4], q1[4], p0 = 0, p1 = 0;
    Quantize7p(e0, q0, p0);
    Quantize7p(e1, q1, p1);
    uint8_t idx[16];
    long long error = Fit7(rgba, q0, p0, q1, p1, idx);
    {
        float aa = 0, bb = 0, ab = 0, ax[4] = {0, 0, 0, 0}, bx[4] = {0, 0, 0, 0};
        for (int t = 0; t < 16; ++t) {
            const float w = kWeights4[idx[t]] / 64.f, v = 1 - w;  // texel = v * e0 + w * e1
            aa += v * v, bb += w * w, ab += v * w;
            for (int k = 0; k < 4; ++k) ax[k] += v * rgba[t * 4 + k], bx[k] += w * rgba[t * 4 + k];
        }
        const float det = aa * bb - ab * ab;
        if (std::abs(det) > 1e-6f) {
            float n0[4], n1[4];
            for (int k = 0; k < 4; ++k) {
                n0[k] = std::clamp((ax[k] * bb - bx[k] * ab) / det, 0.f, 255.f);
                n1[k] = std::clamp((bx[k] * aa - ax[k] * ab) / det, 0.f, 255.f);
            }
            int r0[4], r1[4], s0 = 0, s1 = 0;
            Quantize7p(n0, r0, s0);
            Quantize7p(n1, r1, s1);
            uint8_t idx2[16];
            const long long e2 = Fit7(rgba, r0, s0, r1, s1, idx2);
            if (e2 < error) {
                std::copy(r0, r0 + 4, q0), std::copy(r1, r1 + 4, q1), p0 = s0, p1 = s1;
                std::copy(idx2, idx2 + 16, idx);
            }
        }
    }
    // The first texel's index has only 3 bits: when it is 8 or more, the ends are swapped.
    if (idx[0] >= 8) {
        std::swap_ranges(q0, q0 + 4, q1);
        std::swap(p0, p1);
        for (uint8_t& i : idx) i = static_cast<uint8_t>(15 - i);
    }
    std::memset(out, 0, 16);
    int at = 0;
    PutBits(out, at, 1u << 6, 7);  // mode 6
    for (int k = 0; k < 4; ++k) {
        PutBits(out, at, static_cast<uint32_t>(q0[k]), 7);
        PutBits(out, at, static_cast<uint32_t>(q1[k]), 7);
    }
    PutBits(out, at, static_cast<uint32_t>(p0), 1);
    PutBits(out, at, static_cast<uint32_t>(p1), 1);
    for (int t = 0; t < 16; ++t) PutBits(out, at, idx[t], t == 0 ? 3 : 4);
}

void DecodeBC7(const uint8_t in[16], uint8_t rgba[64]) {
    int at = 0;
    const uint32_t mode = GetBits(in, at, 7);
    if (mode != (1u << 6)) {  // only mode 6 is read (the one this engine writes): the rest are shown magenta
        for (int t = 0; t < 16; ++t) rgba[t * 4] = 255, rgba[t * 4 + 1] = 0, rgba[t * 4 + 2] = 255, rgba[t * 4 + 3] = 255;
        return;
    }
    int q0[4], q1[4];
    for (int k = 0; k < 4; ++k) {
        q0[k] = static_cast<int>(GetBits(in, at, 7));
        q1[k] = static_cast<int>(GetBits(in, at, 7));
    }
    const int p0 = static_cast<int>(GetBits(in, at, 1)), p1 = static_cast<int>(GetBits(in, at, 1));
    for (int t = 0; t < 16; ++t) {
        const int w = kWeights4[GetBits(in, at, t == 0 ? 3 : 4)];
        for (int k = 0; k < 4; ++k) {
            const int a = q0[k] * 2 + p0, b = q1[k] * 2 + p1;
            rgba[t * 4 + k] = static_cast<uint8_t>(((64 - w) * a + w * b + 32) >> 6);
        }
    }
}

// --- Whole pictures -----------------------------------------------------------------------------

namespace {

// One level's blocks.
platform::Image CompressLevel(const platform::Image& level, TextureFormat f) {
    platform::Image out;
    out.width = level.width;
    out.height = level.height;
    out.pixels.resize(LevelBytes(f, level.width, level.height));
    const uint32_t bw = std::max(1u, (level.width + 3) / 4), bh = std::max(1u, (level.height + 3) / 4);
    const size_t bytes = BlockBytes(f);
    uint8_t block[64];
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            for (uint32_t y = 0; y < 4; ++y) {
                for (uint32_t x = 0; x < 4; ++x) {
                    // (past the edge: the last row / column again)
                    const uint32_t sx = std::min(bx * 4 + x, level.width - 1), sy = std::min(by * 4 + y, level.height - 1);
                    std::memcpy(block + (y * 4 + x) * 4, &level.pixels[(static_cast<size_t>(sy) * level.width + sx) * 4], 4);
                }
            }
            uint8_t* dst = &out.pixels[(static_cast<size_t>(by) * bw + bx) * bytes];
            switch (f) {
                case TextureFormat::BC1: EncodeBC1(block, dst); break;
                case TextureFormat::BC3: EncodeBC3(block, dst); break;
                case TextureFormat::BC4: {
                    uint8_t r[16];
                    Channel(block, 0, r);
                    EncodeBC4(r, dst);
                    break;
                }
                case TextureFormat::BC5: EncodeBC5(block, dst); break;
                case TextureFormat::BC7: EncodeBC7(block, dst); break;
                case TextureFormat::Rgba8: break;
            }
        }
    }
    return out;
}

}  // namespace

TexturePixels Compress(const TexturePixels& rgba, TextureFormat format) {
    if (format == TextureFormat::Rgba8 || rgba.format != TextureFormat::Rgba8) return rgba;
    TexturePixels out;
    out.srgb = rgba.srgb;
    out.format = format;
    for (const platform::Image& l : rgba.levels) out.levels.push_back(CompressLevel(l, format));
    return out;
}

platform::Image Decompress(const platform::Image& level, TextureFormat f) {
    if (!IsBlockCompressed(f)) return level;
    platform::Image out;
    out.width = level.width;
    out.height = level.height;
    out.pixels.resize(static_cast<size_t>(level.width) * level.height * 4);
    const uint32_t bw = std::max(1u, (level.width + 3) / 4), bh = std::max(1u, (level.height + 3) / 4);
    const size_t bytes = BlockBytes(f);
    if (level.pixels.size() < static_cast<size_t>(bw) * bh * bytes) return out;
    uint8_t block[64];
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* src = &level.pixels[(static_cast<size_t>(by) * bw + bx) * bytes];
            switch (f) {
                case TextureFormat::BC1: DecodeBC1(src, block); break;
                case TextureFormat::BC3: DecodeBC3(src, block); break;
                case TextureFormat::BC4: {
                    uint8_t r[16];
                    DecodeBC4(src, r);
                    for (int i = 0; i < 16; ++i) block[i * 4] = r[i], block[i * 4 + 1] = 0, block[i * 4 + 2] = 0, block[i * 4 + 3] = 255;
                    break;
                }
                case TextureFormat::BC5: DecodeBC5(src, block); break;
                case TextureFormat::BC7: DecodeBC7(src, block); break;
                case TextureFormat::Rgba8: break;
            }
            for (uint32_t y = 0; y < 4; ++y) {
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t dx = bx * 4 + x, dy = by * 4 + y;
                    if (dx < level.width && dy < level.height) {
                        std::memcpy(&out.pixels[(static_cast<size_t>(dy) * level.width + dx) * 4], block + (y * 4 + x) * 4, 4);
                    }
                }
            }
        }
    }
    return out;
}

}  // namespace rm::space3d
