#pragma once
// 3D 空間: the sky behind everything (スカイボックス) as a pixel shader. Every sky - the ones that come
// with the engine (地球の空, 宇宙, a panorama picture, six pictures) and one the user writes (a .rmsky
// file) - is the same thing: HLSL code with a function
//
//   float3 Sky(SkyInput i)   // the light seen looking `i.dir` (linear, as a light's colour)
//
// put together with the engine's part (AssembleSky) and drawn by the Renderer behind everything
// (RenderFrame::sky). What the code can use (SkyInput, the helpers) is written in kSkyPrelude
// (SkyShader.cpp) and in the file a new sky starts from (NewSkyCode).
//
// The numbers it gets (SkyConstants, the constant buffer b3) are set by whoever draws the sky (the
// game's 空: game_x_3d/runtime/Sky.h). Pure (text only) - tests/TestSky.cpp compiles every sky.

#include <cstddef>
#include <string>
#include <string_view>

namespace rm::space3d {

// The constant buffer `SkyFrame` (b3) as the GPU reads it. The Renderer fills invViewProj (the
// camera's turn only: clip space -> a direction in the world); the rest is the sky's.
struct SkyConstants {
    float invViewProj[16];
    float sun[4];       // xyz: towards the sun (length 1); w: seconds (SkyInput::time)
    float moon[4];      // xyz: towards the moon; w: the hour 0..24 (SkyInput::hour)
    float info[4];      // x: brightness (Sky's result is multiplied by it); yzw: not used
    float param[8][4];  // the sky's own numbers (a built-in sky's colours, a user's 値 and 色)
};
static_assert(sizeof(SkyConstants) % 16 == 0);
inline constexpr size_t kSkyParams = 8;
inline constexpr size_t kSkyTextures = 6;  // sky0 .. sky5 (t0 .. t5)

// Light <-> the screen's numbers (sRGB), the same in the renderer's shaders and the sky's.
extern const char kColorHlsl[];

// The whole HLSL of a sky: the engine's part, then `code` (with `#line 1 "<fileName>"`: the compiler's
// messages name the file and its own lines), then the vertex shader SkyVS and the pixel shader SkyPS
// (ps_4_0 / vs_4_0).
std::string AssembleSky(std::string_view code, std::string_view fileName);

// The skies that come with the engine (code of the same form as a .rmsky).
//   Earth     param[0] rgb: the sun's light; a: day (0 night .. 1)
//             param[1] x: clouds' cover 0..1, y: their darkness, z: haze (霧), w: 1 = lightning (雷雨)
//             param[2] rgb: the sky straight up; a: the stars (0 .. 1)
//             param[3] rgb: the sky at the horizon; a: the moon's light
//             param[4] rgb: the ground below the horizon; a: the sunset's glow (0 .. 1)
//   Space     param[0] x: the stars, y: the nebulae, z: the Milky Way, w: 1 = the sun is drawn
//             param[1] rgb: the nebulae's first colour; param[2] rgb: their second colour
//   Panorama  sky0: one picture around (equirectangular: 360 x 180 degrees); param[0] x: turned (radians)
//   SixFaces  sky0 .. sky5: +X (right), -X (left), +Y (up), -Y (down), +Z (behind), -Z (in front);
//             param[0] x: turned (radians)
enum class BuiltinSky { Earth, Space, Panorama, SixFaces };
std::string_view BuiltinSkyCode(BuiltinSky sky);
const char* BuiltinSkyName(BuiltinSky sky);  // the "file name" its messages give ("地球の空")

// What a new .rmsky file holds: a short sky to start from, with what can be used in comments.
std::string_view NewSkyCode();
inline constexpr char kSkyExtension[] = ".rmsky";

}  // namespace rm::space3d
