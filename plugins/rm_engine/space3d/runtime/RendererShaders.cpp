// レンダラーのシェーダー (HLSL のテキスト): RendererShaders.h を見ること。

#include "rm_engine/space3d/runtime/RendererShaders.h"

#include "rm_engine/space3d/runtime/SkyShader.h"

namespace rm::space3d {

namespace {

// 行列は列ごとに (m[column][row]) 並べて持つ。これは HLSL の標準の詰め方なので、そのまま
// GPU に渡せて、mul(matrix, vector) は matrix * vector の意味になる。
// (それぞれ別のリテラルにする: 1 つは最大 16 KB まで)
constexpr char kShadersFirst[] = R"(
cbuffer Frame : register(b0) {
    float4x4 viewProj;
    float4x4 lightViewProj;  // ShadowVS: ワールド -> 描いている途中のシャドウマップ (x, y -1..1、深度 0..1)
    float4 eye;              // xyz: カメラの位置 (ワールド)
    float4 fog;              // rgb: 色 (線形)、a: 濃さ (1 m あたり。0: 無し) - RenderFrame::fog
};
// 霧 (RenderFrame::fog): 遠いほどその色 (線形。ToScreen の前) が増える。
float3 Fogged(float3 c, float3 wpos) {
    if (fog.a <= 0) return c;
    return lerp(fog.rgb, c, exp(-fog.a * distance(wpos, eye.xyz)));
}
// フレームの全部の物と材質の数値。1 フレームに 1 回書く (Renderer3D.cpp の things_)。
// 描画は、その物と材質がどこにあるかを持つ (DRAW: x, y。float4 単位):
//   物:   world (4 列)、normalWorld (3 列: world の逆行列の転置 - 法線が直角のまま保たれる)、
//         color (画面の色)、highlight (縁の光: rgb、a = 強さ)
//   材質: material (その色。線形: `color` を掛ける)、paint (x: 1 = 色の絵あり、y: アルファ
//         0 不透明 / 1 マスク / 2 ブレンド、z: マスクのしきい値、w: 1 = 法線マップあり)、
//         pbr (x: 金属度、y: 粗さ、z: 法線マップの強さ、w: 1 = 金属 / 粗さの絵あり)、
//         emissive (rgb: 出す光。線形、w: 1 = 発光の絵あり)
Buffer<float4> things : register(t6);
static float4x4 world;
static float3x3 normalWorld;
static float4 color, highlight, material, paint, pbr, emissive;
void LoadPlace(uint at) {
    world = transpose(float4x4(things.Load(at), things.Load(at + 1), things.Load(at + 2), things.Load(at + 3)));
    normalWorld = transpose(float3x3(things.Load(at + 4).xyz, things.Load(at + 5).xyz, things.Load(at + 6).xyz));
}
void LoadLook(uint2 draw) {
    color = things.Load(draw.x + 7);
    highlight = things.Load(draw.x + 8);
    material = things.Load(draw.y);
    paint = things.Load(draw.y + 1);
    pbr = things.Load(draw.y + 2);
    emissive = things.Load(draw.y + 3);
}
// フレームの光 (Lighting.h の LightBlock: 1 フレームに 1 回入れる)。
cbuffer Lights : register(b1) {
    float4 lightInfo;         // x: 数
    float4 ambientSky;        // rgb: 上を向いた面の環境光
    float4 ambientGround;     // rgb: ... 下を向いた面の
    float4 lightPos[8];       // xyz: 位置 (点光源・スポット)、w: 0 向きのある光、1 点光源、2 スポット
    float4 lightDir[8];       // xyz: 光が進む向き、w: 届く距離
    float4 lightColor[8];     // rgb: 色 x 強さ、w: 1 = その影は太陽のマップ
    float4 lightSpot[8];      // x: 円錐の縁の cos、y: 弱まり始める所の cos
    float4 lightShadow[8];    // 点光源 / スポット: x: 最初のマップ (-1: 無し)、y: 距離あたりの法線のずらし、z: 距離 1 での深度のバイアス
};
// フレームのシャドウマップ (Renderer3D.cpp の ShadowConstants)。
cbuffer Shadows : register(b2) {
    float4x4 sunViewProj[3];    // ワールド -> 太陽のマップ (1 枚、またはカスケード)
    float4x4 localViewProj[8];  // ワールド -> 点光源 / スポットのマップ
    float4 sunInfo[3];          // x: 目の前でマップが終わる所、y: 法線のずらし (ワールド)、z: 深度のバイアス
    float4 shadowInfo;          // x: 太陽のマップの数 (0: 無し)、y: 太陽のマップの 1 テクセル (uv)、z: ... 他のもの
    float4 viewForward;         // xyz: カメラが向く方向
    float4 atlas;               // マップがテクスチャの中で並ぶ様子。xy: 太陽の列、行。zw: 他のものの列、行
};
// シャドウマップは種類ごとに、1 枚のテクスチャの中に並べる (タイル)。(マップの配列だと、
// サンプラーで比較するのに ps_4_1 が要る。)
Texture2D sunMaps : register(t0);
Texture2D localMaps : register(t5);
SamplerComparisonState shadowSampler : register(s0);
Texture2D baseMap : register(t1);        // sRGB: 線形として読む
Texture2D metalRoughMap : register(t2);  // b: 金属、g: 粗さ
Texture2D normalMap : register(t3);      // 接線空間 (x 右、y 上、z 手前)
Texture2D emissionMap : register(t4);    // sRGB
SamplerState baseSampler : register(s1);

static const float PI = 3.14159265;

struct LineIn  { float3 pos : POSITION; float4 col : COLOR; };
struct LineOut { float4 pos : SV_Position; float4 col : COLOR; };
LineOut LineVS(LineIn i) {
    LineOut o;
    o.pos = mul(viewProj, float4(i.pos, 1));
    o.col = i.col;
    return o;
}
float4 LinePS(LineOut i) : SV_Target { return i.col; }

struct MeshIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; uint2 draw : DRAW; };
struct MeshOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float3 wpos : TEXCOORD2; nointerpolation uint2 draw : DRAW; };
MeshOut MeshVS(MeshIn i) {
    LoadPlace(i.draw.x);
    MeshOut o;
    float4 w = mul(world, float4(i.pos, 1));
    o.pos = mul(viewProj, w);
    o.nrm = mul(normalWorld, i.nrm);
    o.wpos = w.xyz;
    o.uv = i.uv;
    o.draw = i.draw;
    return o;
}

// 1 = 光が当たる、0 = 影: `maps` のマップ `tile` (タイルは `grid` 列 x 行に並ぶ) の p での値
// (割り算の後: x, y -1..1、深度 0..1)。3 x 3 回見る。1 回ごとに 2 x 2 テクセルをすでに混ぜている
// (PCF)。タイルの中に収める。マップの外: 光が当たる。
float ShadowLook(Texture2D maps, float2 grid, float3 p, float tile, float texel, float bias) {
    float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) return 1;
    float d = min(p.z, 1) - bias;  // マップの深度より奥: 全部の後ろ
    float2 corner = float2(tile - grid.x * floor(tile / grid.x), floor(tile / grid.x));
    float sum = 0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 t = clamp(uv + float2(x, y) * texel, texel, 1 - texel);
            sum += maps.SampleCmpLevelZero(shadowSampler, (corner + t) / grid, d);
        }
    }
    return sum / 9;
}

// 太陽: 点が目の前にある範囲まで届く、最初のマップ (1 枚か、カスケード: 近いものはくっきり)。
// 面から少しずらして引く (法線のずらし): 面が自分に影を落とさないように。最後のマップより奥:
// 光が当たる。
float SunShadow(float3 wpos, float3 n) {
    int count = (int)shadowInfo.x;
    float ahead = dot(wpos - eye.xyz, viewForward.xyz);
    [loop] for (int c = 0; c < count; ++c) {
        if (ahead <= sunInfo[c].x) {
            float4 p = mul(sunViewProj[c], float4(wpos + n * sunInfo[c].y, 1));
            return ShadowLook(sunMaps, atlas.xy, p.xyz / p.w, c, shadowInfo.y, sunInfo[c].z);
        }
    }
    return 1;
}

// 点光源 (点がある立方体の面: |x| |y| |z| の一番大きいもの。順は +X -X +Y -Y +Z -Z) か、
// スポット (マップ 1 枚)。透視のマップの深度は均等ではない: バイアスは距離とともに縮む。
float LocalShadow(int l, float3 wpos, float3 n) {
    float slice = lightShadow[l].x;
    if (slice < 0) return 1;
    float3 from = wpos - lightPos[l].xyz;
    if (lightPos[l].w < 1.5) {
        float3 a = abs(from);
        if (a.x >= a.y && a.x >= a.z) slice += from.x > 0 ? 0 : 1;
        else if (a.y >= a.z) slice += from.y > 0 ? 2 : 3;
        else slice += from.z > 0 ? 4 : 5;
    }
    float4 p = mul(localViewProj[(int)slice], float4(wpos + n * (lightShadow[l].y * length(from)), 1));
    if (p.w <= 1e-4) return 1;
    return ShadowLook(localMaps, atlas.zw, p.xyz / p.w, slice, shadowInfo.z, lightShadow[l].z / p.w);
}

float ShadowOf(int l, float3 wpos, float3 n) { return lightColor[l].w > 0.5 ? SunShadow(wpos, n) : LocalShadow(l, wpos, n); }
)";

constexpr char kShadersLast[] = R"(
// 点で、光がどれだけ残っているか (距離による弱まり / 円錐の外)と、どこから来るか (toLight)。
float FadeAt(int l, float3 wpos, out float3 toLight) {
    toLight = -lightDir[l].xyz;
    float fade = 1;
    if (lightPos[l].w > 0.5) {
        float3 d = lightPos[l].xyz - wpos;
        float dist = length(d);
        toLight = d / max(dist, 1e-4);
        float x = saturate(1 - dist / lightDir[l].w);
        fade = x * x;
        if (lightPos[l].w > 1.5) fade *= smoothstep(lightSpot[l].x, lightSpot[l].y, dot(-toLight, lightDir[l].xyz));
    }
    return fade;
}

// 点での光 1 つ: 届くもの (色 x 強さ x 弱まり) と、どこから来るか (toLight)。光の数値は
// Lighting.h が作る。
float3 LightAt(int l, float3 wpos, out float3 toLight) { return lightColor[l].rgb * FadeAt(l, wpos, toLight); }

// 面から目へ向かう光 (線形): 環境光 (全方向から。空のものは上から - 影の中でも) と、各光
// (拡散 + GGX のハイライト。影があればその影)。
float3 Shade(float3 albedo, float metal, float rough, float3 n, float3 v, float3 wpos) {
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);  // 正面から見たとき、面がどれだけ映すか
    float3 diffuse = albedo * (1 - metal);                       // 金属に拡散の色はない
    float a = rough * rough;
    float a2 = a * a;
    float nv = max(dot(n, v), 1e-4);
    float k = (rough + 1) * (rough + 1) / 8;
    // 空: 法線の側からの拡散と、そのうす暗い反射 (角度による Fresnel)。
    float sky = 0.5 + 0.5 * n.y;
    float skyMirror = 0.5 + 0.5 * reflect(-v, n).y;
    float3 fSky = f0 + (max(float3(1 - rough, 1 - rough, 1 - rough), f0) - f0) * pow(1 - nv, 5);
    // 環境光 (線形): 下からの地面のもの .. 上からの空のもの。
    float3 c = diffuse * lerp(ambientGround.rgb, ambientSky.rgb, sky) + fSky * lerp(ambientGround.rgb, ambientSky.rgb, skyMirror) * (1 - 0.6 * rough);
    int count = (int)lightInfo.x;
    [loop] for (int l = 0; l < count; ++l) {
        float3 toLight;
        float3 e = LightAt(l, wpos, toLight);
        float nl = saturate(dot(n, toLight));
        if (nl <= 0 || all(e <= 0)) continue;
        e *= ShadowOf(l, wpos, n);  // 影の無い光は 1
        float3 h = normalize(toLight + v);
        float nh = saturate(dot(n, h));
        float vh = saturate(dot(v, h));
        float dd = nh * nh * (a2 - 1) + 1;
        float D = a2 / (PI * dd * dd);                                     // GGX: h を向く微小な面がどれだけあるか
        float G = nl / (nl * (1 - k) + k) * (nv / (nv * (1 - k) + k));     // Smith: 隠れていないものの割合
        float3 F = f0 + (1 - f0) * pow(1 - vh, 5);                         // Schlick: 斜めから見るほどよく映す
        float3 spec = D * G * F / max(4 * nl * nv, 1e-4);
        // 強さ 1 の光が、白い面に正面から当たると 0.85 (光が入る前のシーンの見た目)。
        c += ((1 - F) * diffuse + PI * spec) * e * (nl * 0.85);
    }
    return c;
}

// 法線マップで曲げた法線。接線は、位置と UV がピクセルごとにどう変わるかから作る
// (「余接フレーム」): 頂点に接線は要らない。
float3 Bend(float3 n, MeshOut i) {
    float3 t = normalMap.Sample(baseSampler, i.uv).xyz * 2 - 1;  // (どの分岐よりも前にサンプルする)
    if (paint.w > 1.5) t.z = sqrt(saturate(1 - dot(t.xy, t.xy)));  // 2 チャンネル (BC5): z は x と y から作る
    float3 dp1 = ddx(i.wpos), dp2 = ddy(i.wpos);
    float2 du1 = ddx(i.uv), du2 = ddy(i.uv);
    if (paint.w < 0.5) return n;
    float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    float3 T = dp2perp * du1.x + dp1perp * du2.x;
    float3 B = dp2perp * du1.y + dp1perp * du2.y;  // V が増える向き: 絵の下へ
    float m = max(dot(T, T), dot(B, B));
    if (m < 1e-20) return n;  // 追うべき UV が無い
    float s = rsqrt(m);
    t.xy *= pbr.z;
    return normalize(T * (s * t.x) - B * (s * t.y) + n * t.z);  // マップの +Y は絵の上
}

// 面自身の色: 描く色 x 材質の色 x 絵。アルファは材質のモードで決まる
// (opaque: 絵のアルファで穴は開かない。mask: しきい値より下は穴。blend: 透ける)。
float4 Paint(MeshOut i) {
    float4 c = float4(ToLinear(color.rgb), color.a) * material;
    if (paint.x > 0.5) c *= baseMap.Sample(baseSampler, i.uv);
    if (paint.y > 0.5 && paint.y < 1.5) clip(c.a - paint.z);  // mask: 穴にするか、しないか
    if (paint.y < 1.5) c.a = color.a;                          // blend だけが、アルファで透ける
    return c;
}

float4 MeshPS(MeshOut i) : SV_Target {
    LoadLook(i.draw);
    float4 base = Paint(i);
    // 見える側は、目に向いている側 (開いたモデルの裏側は、その側の面として光を受ける)。
    // 画面上の三角形の巻き方ではなく目から判断するので、鏡写しのモデルや反転した射影でも
    // 逆の側に光が当たることはない。
    float3 n = normalize(i.nrm);
    float3 toEye = normalize(eye.xyz - i.wpos);
    if (dot(n, toEye) < 0) n = -n;
    float rim = pow(1 - saturate(dot(n, toEye)), 2.5) * highlight.a;  // (形から。法線マップは使わない)
    float3 shape = n;  // (プラグインの手も、形から判断する)
    n = Bend(n, i);
    float4 mr = metalRoughMap.Sample(baseSampler, i.uv);
    if (pbr.w > 1.5) mr = mr.rrrr;  // 1 チャンネル (BC4): 灰色の値が両方になる
    float metal = pbr.x * (pbr.w > 0.5 ? mr.b : 1);
    float rough = clamp(pbr.y * (pbr.w > 0.5 ? mr.g : 1), 0.04, 1);
    Surface(base.rgb, rough, metal, i.wpos, shape);  // プラグインが手を加える所 (RenderFrame::surface: 天候 の 濡れ)
    rough = clamp(rough, 0.04, 1);
    float3 glow = emissive.rgb * (emissive.w > 0.5 ? emissionMap.Sample(baseSampler, i.uv).rgb : float3(1, 1, 1));
    float3 rgb = ToScreen(Fogged(Shade(base.rgb, metal, rough, n, toEye, i.wpos) + glow, i.wpos));
    // 選択中: 縁 (面が目から背を向ける所) が光り、色は変わらない。
    return float4(lerp(rgb, highlight.rgb, rim), base.a);
}

// 床: 自分の色は無く、影だけ (上から見た) - 届いている光のうち一番暗いもの。
float4 FloorPS(MeshOut i) : SV_Target {
    LoadLook(i.draw);
    if (eye.y < 0) discard;  // 床の下から見ている
    float3 up = float3(0, 1, 0);
    float dark = 0;
    int count = (int)lightInfo.x;
    [loop] for (int l = 0; l < count; ++l) {
        if (lightColor[l].w < 0.5 && lightShadow[l].x < 0) continue;  // 影なし
        float3 toLight;
        float fade = FadeAt(l, i.wpos, toLight);
        float r = saturate(saturate(dot(up, toLight)) * fade * 4);
        if (r > 0) dark = max(dark, (1 - ShadowOf(l, i.wpos, up)) * r);
    }
    return float4(color.rgb, color.a * dark);
}

float4 ShadowVS(MeshIn i) : SV_Position {
    LoadPlace(i.draw.x);
    return mul(lightViewProj, mul(world, float4(i.pos, 1)));
}

// シャドウマップのタイルを、残しておいた層 (動かないもの: 別のテクスチャの同じタイル) から
// そのまま敷き戻す。動くものを上に描く前に行う。ClearVS の三角形で描く。
Texture2D layerMap : register(t7);
float CopyDepthPS(float4 pos : SV_Position) : SV_Depth { return layerMap.Load(int3(pos.xy, 0)).r; }

// シャドウマップのタイルを消す: ビューポート全体を覆う三角形を深度 1 で描く (深度テストは
// オフ)。ClearDepthStencilView だと全タイルが消えてしまう。
float4 ClearVS(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 1, 1);
}
)";

// プラグインの手は無し: 面はそのまま。
constexpr char kNoSurface[] = R"(
void Surface(inout float3 albedo, inout float rough, float metal, float3 wpos, float3 n) {}
)";

}  // namespace

const char kDirectionMapHlsl[] = R"(
// 向きの地図 (RenderFrame::mapDirections): 隠れた形 (RenderFrame::mapShapes) の深度を、各向きから
// 見たもの。タイルを 1 枚のテクスチャに並べる (Lighting.h の PlanCover、RendererShaders.h の
// DirectionMapConstants)。
cbuffer DirectionMaps : register(b6) {
    float4x4 mapViewProj[8];  // ワールド -> 地図 (x, y -1..1 は向きに直角、深度 0..1 は向きに沿う)
    float4 mapInfo[8];        // x: 1 = ある、y, z: そのタイルの左、上 (ピクセル)、w: 深度の範囲 (ワールド)
    float4 mapSize;           // x: 地図 1 枚の大きさ (ピクセル)、y: 数
};
Texture2D directionMaps : register(t8);

// 地図 `k` の線に沿って、最初の隠れた形から点がどれだけ先にあるか (ワールドの単位。< 0: その
// 手前)。`p`: 地図の行列を点に掛けたもの。線上に何もない、または地図の外: ずっと手前。
float MapPastAt(int k, float4 p) {
    float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0 || uv.x >= 1 || uv.y < 0 || uv.y >= 1) return -1e30;
    float d = directionMaps.Load(int3(uv * mapSize.x + mapInfo[k].yz, 0)).r;
    return d >= 1 ? -1e30 : (p.z - d) * mapInfo[k].w;
}
float MapPast(int k, float3 w) {
    if (k < 0 || mapInfo[k].x < 0.5) return -1e30;
    return MapPastAt(k, mul(mapViewProj[k], float4(w, 1)));
}
)";

const char kSurfaceHlsl[] = R"(
// プラグインが全部の面に加える手 (RenderFrame::surface)。光を当てる前に働く。そのコードが
//   void Surface(inout float3 albedo, inout float rough, float metal, float3 wpos, float3 n)
// を定義する
// (albedo: 線形、rough 0.04 .. 1、wpos: ワールドでの位置、n: 面の法線 (形から))
// し、数値はここから読む。SunShadow(wpos, n)・MapPast(k, w)・shadowInfo が使える。
cbuffer SurfaceData : register(b5) {
    float4 surfaceData[16];
};
)";

std::string RendererShaderText(std::string_view surface) {
    std::string text = std::string(kColorHlsl) + kShadersFirst + kDirectionMapHlsl + kSurfaceHlsl;
    text += surface.empty() ? std::string_view(kNoSurface) : surface;
    text += "\n";
    text += kShadersLast;
    return text;
}

}  // namespace rm::space3d
