#include "rm_engine/weather/runtime/Falling.h"

#include "rm_engine/space3d/runtime/Lighting.h"

#include <algorithm>
#include <cmath>

namespace rm::weather {

const char kFallHlsl[] = R"(
// 雨と雪 (Falling.h の Precipitation): 1 粒につき 6 つの角 (三角形 2 つ)。それぞれ番号から
// 作る - 目のまわりの箱の中のどこにあるか、どれだけ速く落ちるか - ハッシュで (バッファなし)。
cbuffer Fall : register(b4) {
    float4x4 viewProj;   // ワールド -> 絵
    float4 eye;          // xyz: カメラの位置
    float4 precipInfo;   // x: 0 雨 / 1 雪、y: 秒、z: 箱の一辺、w: その 向きの地図 (-1: なし)
    float4 precipLight;  // rgb: それらを照らす光 (リニア)
    float4 camRight;     // xyz: カメラの右 (ワールド)、w: 雨の強さ (0 .. 1)
    float4 camUp;        // xyz: カメラの上
    float4 precipWind;   // xy: 今の風 (m/s、ワールドの x z)、zw: 風が運んだ距離
    float4 precipFall;   // x: 最小の落下 (m/s)、y: それぞれがさらに速くなれる量、z: 風がどれだけ運ぶか (Falling.h の FallOf)
    float4 precipPlace;  // 場所ごとの降り方: x: 格子の間隔 (m)、y: 一辺の点の数、z: 1 = 使う。w: 積もった雪の深さ (m)
    float4 precipPlaceSnow[13];  // 格子の点ごとの雪の割合 (4 つずつ。行が z、列が x)
};
float PlaceCell(int k) { return precipPlaceSnow[k >> 2][k & 3]; }
// 目からの横のずれ `rel` (x z) の場所で、降るもののうち雪の割合 (格子を線形に補間)。
float PlaceSnowAt(float2 rel) {
    float n = precipPlace.y;
    float2 g = clamp(rel / precipPlace.x + (n - 1) * 0.5, 0, n - 1.001);
    int2 i = (int2)floor(g);
    float2 f = g - i;
    int w = (int)n;
    float a = lerp(PlaceCell(i.y * w + i.x), PlaceCell(i.y * w + i.x + 1), f.x);
    float b = lerp(PlaceCell((i.y + 1) * w + i.x), PlaceCell((i.y + 1) * w + i.x + 1), f.x);
    return lerp(a, b, f.y);
}
uint PrecipHash(uint x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}
float PrecipRand(uint id, uint k) { return (PrecipHash(id * 4 + k) & 0xFFFFFF) / 16777216.0; }
// splash: 0 落下中、1 雨粒の水しぶき、2 落ちた所に横たわる雪片 (melt: 0 .. 1、seed: 自分の値)
struct PrecipOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float fade : TEXCOORD1; float splash : TEXCOORD2; float melt : TEXCOORD3; float seed : TEXCOORD4; };
PrecipOut FallVS(uint vid : SV_VertexID) {
    static const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    uint id = vid / 6;
    float2 q = corners[vid % 6];
    bool snow = precipInfo.x > 0.5;
    float t = precipInfo.y, box = precipInfo.z;
    float3 seed = float3(PrecipRand(id, 0), PrecipRand(id, 1), PrecipRand(id, 2));
    float fall = precipFall.x + (snow ? seed.x : seed.y) * precipFall.y;  // m/s
    // どれも、当たるものの地図と同じ線に沿って動く (Falling.h の FallDirection: 落ちる向きの
    // 平均)。速く落ちるものは、その分だけ速く運ばれる - だから当たった場所は動かない
    // (2026-11-19: 前は雪片が面の下を滑っていき、止まる場所が動いた)。
    float carried = precipFall.z * fall / (precipFall.x + precipFall.y * 0.5);
    float3 v = float3(precipWind.x * carried, -fall, precipWind.y * carried);  // 今の動き
    float speed = length(v);
    float3 dir = v / speed;
    float3 p = seed * box * 7.31;  // ワールドのどこにあるか (箱は繰り返す)
    p.y -= t * fall;
    p.xz += precipWind.zw * carried;  // ここまでの風
    float3 rel = (frac((p - eye.xyz) / box + 0.5) - 0.5) * box;  // 目に一番近いコピー
    float3 w = eye.xyz + rel;
    // 場所ごとの降り方 (WeatherPlace.h): その場所で雪の割合が低ければ雪片は少なく、雨粒は多く (境目は両方: みぞれ)。
    if (precipPlace.z > 0.5) {
        float snowHere = PlaceSnowAt(rel.xz);
        if (PrecipRand(id, 3) >= (snow ? snowHere : 1 - snowHere)) {
            PrecipOut none = (PrecipOut)0;
            none.pos = float4(0, 0, -1, 1);  // (6 つの角すべて: 何も描かない)
            return none;
        }
    }
    // 雪片は揺れる (そう描く。当たるものは落ちる線で決まる)。
    float3 sway = snow ? float3(sin(t * 0.9 + seed.z * 40), 0, cos(t * 0.7 + seed.x * 40)) * 0.2 : float3(0, 0, 0);
    float dist = length(rel + sway);
    PrecipOut o;
    o.uv = q;
    o.splash = 0;
    o.melt = 0;
    o.seed = seed.z;
    o.fade = saturate((dist - 0.4) / 1.2) * saturate((box * 0.5 - dist) / (box * 0.25));
    // 行く手にあるもの (落ちる向きの地図を、風で傾けたもの): 今それをどれだけ過ぎているか。
    // 過ぎていれば、当たった (屋根、壁、地面)。そのあとそこには何も落ちない。
    float past = MapPast((int)precipInfo.w, w);
    // 雪片は積もった雪の上に落ちる (当たるものの地図は雪の下の面を見ている): 雪の深さぶん上で当たる。
    // (2026-11-24: 前は雪の中へ入って、瞬間的に消えた。今は上に横たわり、縮んで溶ける)
    if (snow && past > -1e29) past += precipPlace.w / max(-dir.y, 0.3);
    float since = past / speed;  // 当たってからの秒
    float3 hit = w - dir * past;  // 当たった場所
    if (snow) {
        float size = 0.014 + seed.z * 0.017;  // (2026-11-20: 3 分の 1 小さく)
        if (past > 0) {
            // 触れた所で止まり、そこに平らに横たわる (少し上: 中には入らない)。しばらくそのままで、
            // やがて目玉焼きのように溶ける: 濡れた縁が薄く不均一に広がり、白い真ん中は縮み、全体が
            // 薄れていく (FallPS)。
            if (since > 3.4) {
                o.pos = float4(0, 0, -1, 1);  // (6 つの角すべて: 何も描かない)
                return o;
            }
            o.melt = saturate((since - 1.4) / 2.0);
            o.splash = 2;
            float3 n = -dir;  // (来た側: 上。風で傾く)
            float3 a = normalize(abs(n.z) < 0.99 ? cross(n, float3(0, 0, 1)) : cross(n, float3(1, 0, 0)));
            float3 b = cross(n, a);
            float3 swayThen = float3(sin((t - since) * 0.9 + seed.z * 40), 0, cos((t - since) * 0.7 + seed.x * 40)) * 0.2;
            w = hit + n * 0.01 + swayThen + (a * q.x + b * q.y) * size * 2.6;
        } else {
            w += sway + (camRight.xyz * q.x + camUp.xyz * q.y) * size;
        }
    } else if (past > 0) {
        // 当たった: そこにしばらく水しぶき (広がって薄れる輪) が出て、そのあとは何もない。
        if (since > 0.14) {
            o.pos = float4(0, 0, -1, 1);
            return o;
        }
        float k = since / 0.14;
        float r = (0.02 + 0.06 * k) * (0.6 + 0.8 * camRight.w);  // (小さい: 雨粒の水しぶきは風があってもなくても。強い雨では大きい)
        w = hit - dir * 0.015 + (camRight.xyz * q.x + float3(0, 0.45, 0) * (q.y + 1)) * r;
        o.fade *= 1 - k;
        o.splash = 1;
    } else {
        // 落ちる道に沿って、後ろに引く筋 (風で傾く)。目のほうを向く。見え方は雨の強さで変わる: 大きな
        // 粒が少し (ぽつぽつ)、短く細いもの (小雨)、もっと長いもの (雨)、長くて太い筋 (どしゃぶり、嵐)。
        float heavy = camRight.w;
        float big = 1 - smoothstep(0.03, 0.12, heavy);
        float3 side = normalize(cross(dir, rel + float3(1e-4, 0, 1e-4)));
        float len = lerp(0.14 + 0.95 * heavy * heavy, 0.16, big) + seed.x * (0.1 + 0.35 * heavy);
        float width = lerp(0.0025 + 0.0035 * heavy, 0.006, big);
        w += side * q.x * (width + dist * 0.0006) - dir * (q.y * 0.5 + 0.5) * len;
    }
    o.pos = mul(viewProj, float4(w, 1));
    return o;
}
float4 FallPS(PrecipOut i) : SV_Target {
    bool snow = precipInfo.x > 0.5;
    float a;
    if (i.splash > 1.5) {
        // 落ちた所に横たわる雪片 (四角形は大きさの 2.6 倍): 落ちてきたときはまず丸い。溶けると、縁が
        // 薄く不均一に広がり (卵白)、真ん中は縮み (黄身)、全体が薄れて消える。
        float r = length(i.uv);
        float ang = atan2(i.uv.y, i.uv.x);
        float m = i.melt;
        const float core = 1.0 / 2.6;
        float yolk = core * (1 - 0.7 * m);
        float white = core * (1 + 1.4 * m) * (1 + (0.12 * sin(ang * 5 + i.seed * 40) + 0.08 * sin(ang * 3 + i.seed * 17)) * m);
        float aYolk = (1 - smoothstep(yolk * 0.55, yolk, r)) * 0.9;
        float aWhite = (1 - smoothstep(white * 0.7, white, r)) * lerp(0.9, 0.3, m);
        a = max(aYolk, aWhite) * (1 - m * m);
    } else if (i.splash > 0.5) {
        float r = length(i.uv * float2(1, 1.6) - float2(0, 0.6));  // 平らな輪を横から見たもの
        a = saturate(1 - abs(r - 0.65) * 4) * 0.45 + saturate(1 - r * 3) * 0.2;
    } else {
        float heavy = camRight.w, big = 1 - smoothstep(0.03, 0.12, heavy);
        a = snow ? saturate((1 - length(i.uv)) * 2.5) * 0.9 : (1 - abs(i.uv.x)) * (0.55 - 0.25 * i.uv.y * i.uv.y) * lerp(0.32 + 0.4 * heavy, 0.6, big);
    }
    return float4(ToScreen(precipLight.rgb * (snow ? 1.0 : 0.85)), saturate(a) * i.fade);
}
)";

const char kWetSurfaceHlsl[] = R"(
float WetHash(float2 c) { return frac(sin(dot(c, float2(127.1, 311.7))) * 43758.5453); }
// 平面上のなめらかなノイズ (0 .. 1)。
float WetNoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3 - 2 * f);
    float a = WetHash(i), b = WetHash(i + float2(1, 0)), c = WetHash(i + float2(0, 1)), d = WetHash(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// 場所ごとの濡れと積雪の地図 (Weather.h の WetMap。surfaceMap: 幅 3 x n、高さ n、1 テクセルに向き 0 .. 3)。
// `place`: xy 最初のマスの角 (ワールドの x z)、z: 1 マス (m)、w: 一辺のマス数 (0: 地図なし)。(x, z) での
// `block` (0: 日なたの濡れ、1: 日陰の濡れ、2: 積雪 m) の値: マスの真ん中の間を線形に、地図の外は一番
// 近いマス。だから気候の境目で、濡れも積雪もなめらかに薄れる。
float4 WetPlace(float4 place, float2 xz, int block) {
    float n = place.w;
    float2 g = clamp((xz - place.xy) / place.z - 0.5, 0, n - 1);
    float2 b = min(floor(g), n - 2);
    float2 f = g - b;
    int2 i = (int2)b + int2(block * (int)n, 0);
    float4 a = lerp(surfaceMap.Load(int3(i, 0)), surfaceMap.Load(int3(i + int2(1, 0), 0)), f.x);
    float4 c = lerp(surfaceMap.Load(int3(i + int2(0, 1), 0)), surfaceMap.Load(int3(i + int2(1, 1), 0)), f.x);
    return lerp(a, c, f.y);
}
// 向き `k` (0 .. 3) の値。
float WetPick(float4 v, int k) { return dot(v, float4(k == 0, k == 1, k == 2, k == 3)); }

// 雨粒が落ちた所の小さな濡れた点 (数センチ、不均一)。`density` が言う数だけ (0: なし .. 1: 全部
// 濡れ)。細かいノイズのうち、しきい値より下の部分。遠く (点が 1、2 ピクセルより小さい) では
// 点を平均に溶かし、ちらつかない。`s`: 面上の場所 (メートル)、`footprint`: 1 ピクセルで
// どこまで進むか (fwidth。全ピクセルが走る所で取る)。
float WetSpecks(float2 s, float density, float footprint) {
    float n = 0.55 * WetNoise(s * 22) + 0.3 * WetNoise(s * 55 + 7.1) + 0.15 * WetNoise(s * 120 + 3.3);
    float threshold = lerp(0.18, 0.86, density);
    float specks = 1 - smoothstep(threshold - 0.04, threshold + 0.04, n);
    specks = lerp(specks, density, saturate(footprint * 22 * 1.2 - 0.2));
    return max(specks, smoothstep(0.85, 1.0, density));  // (完全に濡れた: 乾いた隙間は残らない)
}

// 雨が、`k` (0 .. 1) の向きで、ある点にどれだけ届いたか: 面から少し離れた場所は、その向きでは
// 何の向こうにもない - まわり 75 cm の渦巻き上の 32 か所で読む (真ん中を重く)。だから屋根の縁に
// 向かって、点は 1 m ほどかけて薄くなる。本物の地面のように。それぞれの場所は「届いたか
// 届かないか」を読むので、渦巻きを場所ごとに少しずつ回して動かす (ゆっくり、点の大きさの
// ゆらぎで): 各読み取りの段が別の場所に落ちて重なる - 点が止まる所に帯も線も出ない
// (2026-11-20: 5 x 5 の格子だと 5 本の直線の帯ができ、点がそれぞれの帯に沿って切れた)。
float WetOpen(int k, float3 p, float2 s) {
    const float2 wobble = float2(WetNoise(s * 7 + 11.3), WetNoise(s * 7 + 27.9)) * 2 - 1;
    const float turn = WetNoise(s * 0.8 + 5.1) * 6.2831853;
    const float4 c = mul(mapViewProj[k], float4(p + float3(wobble.x, 0, wobble.y) * 0.18, 1));
    const float4 ax = mul(mapViewProj[k], float4(1, 0, 0, 0)), az = mul(mapViewProj[k], float4(0, 0, 1, 0));
    float open = 0, total = 0;
    [unroll] for (int i = 0; i < 32; ++i) {
        const float r = 0.75 * sqrt((i + 0.5) / 32.0);
        const float a = i * 2.3999632 + turn;  // (黄金角: 円盤に均等に)
        const float w = 1.05 - r / 0.75;
        const float2 o = float2(cos(a), sin(a)) * r;
        open += saturate((0.06 - MapPastAt(k, c + ax * o.x + az * o.y)) / 0.06) * w;
        total += w;
    }
    return open / total;
}

// しみこみ: 覆われた点に、まわりから濡れがどれだけ届くか - 地面を広がる水
// (WetWay::seep)。点のまわり `reach` の渦巻き上の 16 か所で読む: 十分な数が開いていれば濡れる。
// `reach` の内側に向かって、点は少しずつ減る。`reach` より小さいものの下は、近くが全部開いている。
// 屋根の下では縁だけ (2026-11-20: 球や樽のすぐ下の地面が乾いたままだった)。
float WetSeep(int k, float3 p, float2 s, float reach) {
    const float turn = WetNoise(s * 0.8 + 9.7) * 6.2831853;
    const float4 c = mul(mapViewProj[k], float4(p, 1));
    const float4 ax = mul(mapViewProj[k], float4(1, 0, 0, 0)), az = mul(mapViewProj[k], float4(0, 0, 1, 0));
    float open = 0;
    [unroll] for (int i = 0; i < 16; ++i) {
        const float r = reach * sqrt((i + 0.5) / 16.0);
        const float a = i * 2.3999632 + turn;
        const float2 o = float2(cos(a), sin(a)) * r;
        open += saturate((0.06 - MapPastAt(k, c + ax * o.x + az * o.y)) / 0.06);
    }
    return saturate(open / 16 * 4);
}

// 面がどれだけ濡れて見えるか (0 .. 1): それに届いた雨の降り方のうち一番濡れるもの (届いた
// 場所は、雨が降る間は濡れたまま。その後の風がどうであれ)。日なたか日陰か (日陰は乾くのが
// 遅い)。小さい点が、どんどん増えて全部濡れるまで。屋根の縁に向かって、少しずつ減る (線も波も
// なし: 濡れ具合と同じだけの点)。上を向く所が最大で、下を向く所は 0。場所ごとの地図
// (surfaceData[9]) があるときは、どれだけ濡れたかをその場所の値で (雨の所だけ濡れる)。
float WetAt(float3 wpos, float3 n) {
    // 面の 2 つの軸 (面がだいたい載っている平面): 点はその上に平らに載る。
    float3 a = abs(n);
    float2 s = a.y >= a.x && a.y >= a.z ? wpos.xz : a.x >= a.z ? wpos.zy : wpos.xy;
    float footprint = max(fwidth(s.x), fwidth(s.y));  // (どの return より前: 微分は全ピクセルが要る)
    int count = (int)surfaceData[0].x;
    if (count <= 0 || n.y < -0.3) return 0;
    float lit = shadowInfo.x > 0 ? SunShadow(wpos, n) : 1;
    float3 p = wpos + n * 0.12;
    float4 place = surfaceData[9];
    bool placed = place.w > 1.5;
    float4 sunHere = placed ? WetPlace(place, wpos.xz, 0) : 0, shadeHere = placed ? WetPlace(place, wpos.xz, 1) : 0;
    // どれだけ濡れたか (時間で: `level`) と、そのうちここにどれだけ届くか (`wet`: level に場所の
    // 開き具合を掛けたもの)。点の数は `wet`、点の暗さは `level`。
    float wet = 0, level = 0;
    [loop] for (int k = 0; k < count; ++k) {
        float4 way = surfaceData[1 + k];  // x: 日なたでの濡れ、y: 日陰での濡れ、z: その 向きの地図 (-1: なし)、w: しみこみ (m)
        float l = placed ? lerp(WetPick(shadeHere, k), WetPick(sunHere, k), lit) : lerp(way.y, way.x, lit);
        level = max(level, l);
        if (l <= wet) continue;
        int m = (int)way.z;
        if (m < 0 || mapInfo[m].x < 0.5) {
            wet = l;  // (地図なし: 当たるものがなく、全部開いている)
            continue;
        }
        float open = WetOpen(m, p, s);
        if (way.w > 0.02 && open < 0.999) open = max(open, WetSeep(m, p, s, way.w));
        wet = max(wet, l * open);
    }
    if (wet <= 0) return 0;
    // 乾いているのと濡れているのの間 (縁、雨の降り始め) では、ここは点が多く、あそこは少ない -
    // 1、2 m ほどのまだら - だから縁が一様な帯にならない (乾ききった所や濡れきった所は変わらない)。
    const float patch = WetNoise(s * 0.6) * 2 - 1;
    const float density = saturate(wet + patch * wet * (1 - wet) * 0.9);
    return saturate(level * 1.5) * WetSpecks(s, density, footprint) * lerp(0.45, 1, saturate(n.y));
}


// すべての面の 濡れ (3D 空間の RenderFrame::surface): 面の中の水で暗くなり - 少しだけなめらかに
// なる: 水たまりではなく、濡れた地面。(2026-11-19: なめらかにすると、日陰の濡れた点が空を映して
// 乾いた地面より明るく見え、影の縁が線になった。今は点は必ず暗い。)
// 面の 積雪 (0 .. 1): どれだけ白いか - 上を向き、雪がまっすぐ下に届く所
// (surfaceData[0].z: その 向きの地図。屋根の下: なし)。薄い雪: 白いまだらが育ってつながり、
// 2 cm で全部白くなる (もっと深いと、積もった雪 - SnowLayerVS - が高さでも覆う)。
static const float3 kSnowWhite = float3(0.82, 0.84, 0.88);
// `depth` が積もる (x, z) で、雪がどう立つか (高さ: depth x この値。0: そこに雪なし)。薄いとき:
// ゆっくりしたノイズが高い所で盛り上がる塊 - 深さより高く、雪が集まっている - が育ってつながる。
// 約 3.5 cm からは、ゆるやかなうねりのある一面の雪 (2026-11-20: 薄い雪は、雨のような平らな点だった)。
// うねりは深さの ±20% だが、多くて ±2 cm (深い雪が大きく波打たない)。
float SnowShape(float2 xz, float depth) {
    if (depth <= 0) return 0;
    float n = 0.65 * WetNoise(xz * 2.2 + 1.7) + 0.35 * WetNoise(xz * 5.3 + 4.1);
    float cover = saturate(depth / 0.015);  // (1.5 cm で全部覆う)
    float lump = saturate((n - (1 - cover)) / 0.3);
    lump = lump * lump * (3 - 2 * lump);
    float thin = lump * lerp(2.5, 1, saturate(depth / 0.03));
    float deep = 1 + (0.4 * n - 0.2) * min(1, 0.1 / depth);
    return lerp(thin, deep, saturate((depth - 0.015) / 0.02));
}
// 点は、地図 `m` が見る向き (0 .. 1) に開いているか (地図なし: 開いている)。
float OpenFor(int m, float3 p) {
    return m >= 0 && mapInfo[m].x > 0.5 ? saturate((0.06 - MapPastAt(m, mul(mapViewProj[m], float4(p, 1)))) / 0.06) : 1;
}
// 面の 積雪 (0 .. 1): 上を向く所。各降り方が届く具合は、雨の染みと同じ渦巻きで読む (WetOpen) -
// 屋根の縁に向かって 1 m ほどかけて、雪のまだら (点が集まった塊) が少しずつ減る。全部届く所では
// 雪が立つ所が白い (積もった雪の足元は高さに合わせる - SnowLayerVS と同じ形)。(2026-11-20:
// 前は 1 か所だけ読んでいて、屋根の下に吹き込む所は地図のマスの線が見え、範囲が一様に白かった。)
// 場所ごとの地図があるときは、積もった深さをその場所の値で (雪の所だけ積もる)。
float SnowAt(float3 wpos, float3 n) {
    float3 a = abs(n);
    float2 s = a.y >= a.x && a.y >= a.z ? wpos.xz : a.x >= a.z ? wpos.zy : wpos.xy;
    float footprint = max(fwidth(s.x), fwidth(s.y));  // (どの return より前: 微分は全ピクセルが要る)
    int count = (int)surfaceData[0].x;
    if (n.y < 0.3 || count <= 0) return 0;
    float3 p = wpos + n * 0.12;
    float4 place = surfaceData[9];
    bool placed = place.w > 1.5;
    float4 snowHere = placed ? WetPlace(place, wpos.xz, 2) : 0;
    float depth = 0, most = 0;
    [loop] for (int k = 0; k < count; ++k) {
        float snow = placed ? WetPick(snowHere, k) : surfaceData[5 + k].x;
        if (snow <= 0) continue;
        most += snow;
        int m = (int)surfaceData[1 + k].z;
        depth += snow * (m >= 0 && mapInfo[m].x > 0.5 ? WetOpen(m, p, s) : 1);
    }
    if (depth <= 0) return 0;
    // 届く割合 (縁で 1 から 0 へ) に、雪の薄さ (1.5 cm で全部) を掛けたものが、まだらの数。
    float reach = saturate(depth / most);
    float wet = reach * saturate(most / 0.015);
    const float patch = WetNoise(s * 0.6 + 3.7) * 2 - 1;  // (縁が一様な帯にならない: 1、2 m のまだら)
    float density = saturate(wet + patch * wet * (1 - wet) * 0.9);
    float specks = WetSpecks(s * 0.45, density, footprint * 0.45);  // (雨の点より大きい塊)
    float lying = smoothstep(0.01, 0.03, SnowShape(wpos.xz, depth)) * smoothstep(0.85, 1, reach);
    return max(specks, lying) * smoothstep(0.3, 0.5, n.y);
}

// すべての面の 濡れ と 積雪 (3D 空間の RenderFrame::surface): 面の中の水で暗くなり - 少しだけ
// なめらかになる: 水たまりではなく、濡れた地面。(2026-11-19: なめらかにすると、日陰の濡れた点が
// 空を映して乾いた地面より明るく見え、影の縁が線になった。今は点は必ず暗い。)
// 雪はその上に積もる: 白くて艶がない。
void Surface(inout float3 albedo, inout float rough, float metal, float3 wpos, float3 n) {
    float wet = WetAt(wpos, n);
    float snow = SnowAt(wpos, n);
    wet *= 1 - snow;
    albedo *= 1 - wet * 0.3 * (1 - metal);
    rough = lerp(rough, min(rough, max(rough * 0.85, 0.5)), wet);
    albedo = lerp(albedo, kSnowWhite, snow);
    rough = lerp(rough, 0.75, snow);
}
)";

const char kSnowLayerHlsl[] = R"(
// 高さのある 積雪 (RenderFrame::solids): 目のまわりの格子 1 つを真上から見る
// (SnowLayerOf。2026-11-20: 傾いた向きごとの層は、板のように立ち上がって、ものを突き抜けた)。
// 各角は、その下にあるものの上面に載り (まっすぐ下の地図)、そこに雪が積もった高さまで立ち上がる
// (各降り方の雪を、その降り方が届く所で、SnowShape の塊の中で)。動くものが押した所
// (足跡、SnowTrailPS) では、決してその上にはならない。段では、上側で雪が薄くなって
// なくなる (丸い縁)。急すぎる所と下に何もない所では、角を除く (NaN: その三角形は描かない)。
// 段をまたぐ三角形は捨てる - 雪が立たず、ものの中にもない。傾いた向きの降り方が屋根の下に
// 吹き込む分は、そこでは白い (SnowAt) が高さはない。縁の角は本当の縁まで外へ動かす (EdgeReach)。
// 場所ごとの地図 (layerPlace。surfaceMap を頂点シェーダーにも結ぶ) があるときは、各降り方の雪の
// 深さをその場所の値で。
// モデルと同じ光 (Shade)、霧の中 (Fogged)。RendererShaderText(kWetSurfaceHlsl) の後。
cbuffer SnowLayer : register(b4) {
    float4 layerGrid;     // xy: 格子の角 (ワールドの x z、ちょうどのマス単位)、z: 1 マス (m)、w: 1 辺のマス数
    float4 layerInfo;     // x: まっすぐ下の地図、y: その線が始まる所 (ワールドの y: 目)
    float4 layerSnow[4];  // 各降り方: x: その雪 (m)、y: その 向きの地図 (-1: なし)
    float4 trailInfo;     // xy: 足跡の最初のマス (ワールドの x z)、z: 1 マス (m)、w: 1 辺のマス数 (0: なし)
    float4 trailMore;     // x: 足跡の数のうちどれか (Play ごとに別)
    float4 layerPlace;    // 場所ごとの地図 (WetPlace の place。w が 0: なし)
};
Texture2D<float4> trailMap : register(t9);

// (x, z) で雪が立てる一番高さ: 動くものが押した所では、その高さ。そうでなければ 1e30。
float TrailCeiling(float2 xz) {
    if (trailInfo.w < 1) return 1e30;
    float n = trailInfo.w;
    float2 cell = floor(xz / trailInfo.z);
    int2 t = int2(cell - n * floor(cell / n));  // (マスの地図上の場所: 巻き戻る)
    float4 v = trailMap.Load(int3(t, 0));
    return all(v.yz == cell) && v.w == trailMore.x ? v.x : 1e30;
}

// (x, z) の 1 つの角: その下の上面 (ワールドの y。なし: -1e30)、そこに雪がどれだけ深く
// 積もるか、雪の上面はどこか。
struct SnowCorner {
    float ground;
    float depth;
    float top;
    bool none;
};
SnowCorner CornerAt(float2 xz, float fade) {
    SnowCorner c;
    int m = (int)layerInfo.x;
    float past = MapPastAt(m, mul(mapViewProj[m], float4(xz.x, layerInfo.y, xz.y, 1)));
    c.none = past < -1e29;
    c.ground = layerInfo.y + past;
    c.depth = 0;
    c.top = c.ground;
    if (c.none) return c;
    float3 above = float3(xz.x, c.ground + 0.05, xz.y);
    bool placed = layerPlace.w > 1.5;
    float4 snowHere = placed ? WetPlace(layerPlace, xz, 2) : 0;
    [unroll] for (int j = 0; j < 4; ++j) {
        float snow = placed ? WetPick(snowHere, j) : layerSnow[j].x;
        if (snow > 0) c.depth += snow * OpenFor((int)layerSnow[j].y, above);
    }
    float h = c.depth * SnowShape(xz, c.depth) * fade;
    c.top = max(c.ground, min(c.ground + h, TrailCeiling(xz)));
    return c;
}

// 真下の地図で、(x, z) の下の面が `level` の高さ (段の半分より近く。それより高くてもよい) にあるか
// (0 .. 1): まわりの 4 テクセルをそれぞれ「ある / ない」で読み、場所で混ぜる - 地図のテクセル
// (約 8 cm) より細かく、縁がどこにあるかが分かる。
float OnLevel(float2 xz, float level, float ledge) {
    int m = (int)layerInfo.x;
    float4 p = mul(mapViewProj[m], float4(xz.x, layerInfo.y, xz.y, 1));
    float2 f = (p.xy * float2(0.5, -0.5) + 0.5) * mapSize.x - 0.5;
    float2 b = floor(f), w = f - b;
    float on[4];
    [unroll] for (int i = 0; i < 4; ++i) {
        float2 t = b + float2(i % 2, i / 2);
        bool inside = t.x >= 0 && t.y >= 0 && t.x < mapSize.x && t.y < mapSize.x;
        float d = inside ? directionMaps.Load(int3(t + mapInfo[m].yz, 0)).r : 1;
        on[i] = d < 1 && layerInfo.y + (p.z - d) * mapInfo[m].w > level - ledge * 0.5 ? 1 : 0;
    }
    return lerp(lerp(on[0], on[1], w.x), lerp(on[2], on[3], w.x), w.y);
}
// 段の角から `dir` に沿って、本当の段 (OnLevel が `from` でなくなる所) までの距離 (多くて `most`)。
// 縁の角 (隣が段の下): `level` = その高さ、`from` = true (面がなくなる所まで)。根元の角 (隣が段の
// 上): `level` = その高さ + 段、`from` = false (面が立ち上がる所まで)。8 か所で読み、半分の所を
// 直線で求める。
float EdgeReach(float2 xz, float2 dir, float level, float ledge, float most, bool from) {
    float before = OnLevel(xz, level, ledge), t0 = 0;
    if ((before >= 0.5) != from) return 0;
    [loop] for (int i = 1; i <= 8; ++i) {
        float t = most * i / 8.0;
        float on = OnLevel(xz + dir * t, level, ledge);
        if ((on >= 0.5) != from) return lerp(t0, t, saturate((before - 0.5) / (abs(before - on) > 1e-4 ? before - on : 1e-4)));
        before = on;
        t0 = t;
    }
    return most;
}

// 足場の端 (下の面が 45 度より急に落ちる所・下に何もない所) が、(x, z) から -x / +x / -z / +z の向きに
// どれだけ離れているか (x y z w。見つからなければ `reach`)。雪の格子と同じ間隔で読む。上る段 (壁) は端ではない。
float4 DropDistances(float2 xz, float ground, float reach, float pace) {
    int m = (int)layerInfo.x;
    float4 dist = reach;
    int steps = (int)ceil(reach / pace);
    const float2 dirs[4] = {float2(-1, 0), float2(1, 0), float2(0, -1), float2(0, 1)};
    [unroll] for (int k = 0; k < 4; ++k) {
        [loop] for (int s = 1; s <= steps; ++s) {
            float along = s * pace;
            float2 at = xz + dirs[k] * along;
            float past = MapPastAt(m, mul(mapViewProj[m], float4(at.x, layerInfo.y, at.y, 1)));
            if (past < -1e29 || layerInfo.y + past < ground - (0.06 + along)) {
                dist[k] = min(dist[k], along - 0.5 * pace);
                break;
            }
        }
    }
    return dist;
}
// 端から `dist` の所に、雪がどれだけ残るか (0 .. 1): 端で 0、`reach` で 1 の四分円 (枕のように丸い縁)。
float Pillow(float dist, float reach) {
    float t = saturate(dist / reach);
    return sqrt(saturate(1 - (1 - t) * (1 - t)));
}
// 隣の角から見た端までの距離 (真ん中の角が読んだもの `d` から、`by` だけ動いた所)。見つからなかった
// 向き (`reach`) は、隣でも見つからないとみなす。
float Shifted(float d, float by, float reach) { return d >= reach - 1e-4 ? reach : clamp(d + by, 0, reach); }

struct SnowOut {
    float4 pos : SV_Position;
    float3 nrm : NORMAL;
    float3 wpos : TEXCOORD2;
    float depth : TEXCOORD4;  // そこにどれだけ深く積もるか
    float2 edge : TEXCOORD5;  // x: 根元の角 (1)、y: 縁の角 (1)。両方が混ざる三角形は段をまたぐ: 描かない
};

SnowOut SnowLayerVS(uint id : SV_VertexID) {
    SnowOut o;
    uint side = (uint)layerGrid.w + 1;
    float2 g = float2(id % side, id / side);
    float pace = layerGrid.z;
    float2 xz = layerGrid.xy + g * pace;
    // (格子の端に向かって低く: 終わる所に壁を作らない)
    float fade = saturate((layerGrid.w * 0.5 - max(abs(g.x - layerGrid.w * 0.5), abs(g.y - layerGrid.w * 0.5))) * pace / 4);
    SnowCorner c = CornerAt(xz, fade);
    SnowCorner l = CornerAt(xz - float2(pace, 0), fade), r = CornerAt(xz + float2(pace, 0), fade);
    SnowCorner d = CornerAt(xz - float2(0, pace), fade), u = CornerAt(xz + float2(0, pace), fade);
    // 足場の端に向かって雪は薄くなり、枕のように丸く終わる (2026-11-24): 端までの距離を 4 つの向きで探し、
    // 雪の深さの 3 倍の幅で減らす。急な斜面 (球の肩) も端になるので、丸い物の上の雪は山の形になる。
    if (!c.none && c.depth > 0) {
        float reach = clamp(c.depth * 3, 0.15, 1.5);
        float4 e = DropDistances(xz, c.ground, reach, pace);
        c.top = c.ground + (c.top - c.ground) * Pillow(min(min(e.x, e.y), min(e.z, e.w)), reach);
        l.top = l.ground + (l.top - l.ground) * Pillow(min(min(Shifted(e.x, -pace, reach), Shifted(e.y, pace, reach)), min(e.z, e.w)), reach);
        r.top = r.ground + (r.top - r.ground) * Pillow(min(min(Shifted(e.x, pace, reach), Shifted(e.y, -pace, reach)), min(e.z, e.w)), reach);
        d.top = d.ground + (d.top - d.ground) * Pillow(min(min(e.x, e.y), min(Shifted(e.z, -pace, reach), Shifted(e.w, pace, reach))), reach);
        u.top = u.ground + (u.top - u.ground) * Pillow(min(min(e.x, e.y), min(Shifted(e.z, pace, reach), Shifted(e.w, -pace, reach))), reach);
    }
    float ledge = 0.12 + pace;  // (約 45 度より急な段)
    bool foot = (!l.none && l.ground - c.ground > ledge) || (!r.none && r.ground - c.ground > ledge) ||
                (!d.none && d.ground - c.ground > ledge) || (!u.none && u.ground - c.ground > ledge);
    bool rim = l.none || r.none || d.none || u.none || c.ground - l.ground > ledge || c.ground - r.ground > ledge ||
               c.ground - d.ground > ledge || c.ground - u.ground > ledge;
    // 斜めの隣も見る: 格子の三角形は斜めの辺も持つ。丸い物の縁では、上下左右は物の上なのに斜めだけが
    // 地面という角があり、それを段と見ないと、地面から物の肩へ立ち上がる三角すいの雪ができた (2026-11-24)。
    {
        int m = (int)layerInfo.x;
        [unroll] for (int k = 0; k < 4; ++k) {
            float2 at = xz + float2(k % 2 == 0 ? -pace : pace, k < 2 ? -pace : pace);
            float past = MapPastAt(m, mul(mapViewProj[m], float4(at.x, layerInfo.y, at.y, 1)));
            float g = layerInfo.y + past;
            if (past < -1e29 || c.ground - g > ledge) rim = true;
            else if (g - c.ground > ledge) foot = true;
        }
    }
    // 下の面の傾きと雪の傾き (段の向こうの隣は、これと同じ高さとみなす)。
    float gl = l.none || abs(l.ground - c.ground) > ledge ? c.ground : l.ground, gr = r.none || abs(r.ground - c.ground) > ledge ? c.ground : r.ground;
    float gd = d.none || abs(d.ground - c.ground) > ledge ? c.ground : d.ground, gu = u.none || abs(u.ground - c.ground) > ledge ? c.ground : u.ground;
    float3 groundN = normalize(float3(gl - gr, 2 * pace, gd - gu));
    // 根元の角は描く (2026-11-21: 前は除いていて、物のまわりの地面の雪がマスの階段で途切れた)。
    // 段をまたぐ三角形 (根元の角と縁の角の両方を持つ) は SnowLayerPS が捨てる。
    if (c.none || (foot && rim) || groundN.y < 0.45) {
        float nan = asfloat(0x7fc00000);
        o.pos = float4(nan, nan, nan, nan);
        o.nrm = float3(0, 1, 0);
        o.wpos = float3(0, 0, 0);
        o.depth = 0;
        o.edge = float2(0, 0);
        return o;
    }
    float own = c.top - c.ground;
    float tl = gl + (gl == c.ground && (l.none || abs(l.ground - c.ground) > ledge) ? own : l.top - l.ground);
    float tr = gr + (gr == c.ground && (r.none || abs(r.ground - c.ground) > ledge) ? own : r.top - r.ground);
    float td = gd + (gd == c.ground && (d.none || abs(d.ground - c.ground) > ledge) ? own : d.top - d.ground);
    float tu = gu + (gu == c.ground && (u.none || abs(u.ground - c.ground) > ledge) ? own : u.top - u.ground);
    o.nrm = normalize(float3(tl - tr, 2 * pace, td - tu));
    o.depth = c.depth;
    // 縁の角は、本当の縁まで外へ動かす: 円柱や丸い物の上の雪が、格子のマスの階段 (四角) ではなく
    // 物の形に沿って終わる (2026-11-20)。根元の角は、物の壁の手前 (2 cm) まで内へ動かす: まわりの
    // 地面の雪が物の形に沿って根元まで届く (2026-11-21)。
    float2 at = xz;
    if (rim) {
        float2 outward = float2((l.none || c.ground - l.ground > ledge ? -1 : 0) + (r.none || c.ground - r.ground > ledge ? 1 : 0),
                            (d.none || c.ground - d.ground > ledge ? -1 : 0) + (u.none || c.ground - u.ground > ledge ? 1 : 0));
        if (dot(outward, outward) > 0.5) at += normalize(outward) * EdgeReach(xz, normalize(outward), c.ground, ledge, pace * length(outward), true);
    } else if (foot) {
        float2 inward = float2((!l.none && l.ground - c.ground > ledge ? -1 : 0) + (!r.none && r.ground - c.ground > ledge ? 1 : 0),
                               (!d.none && d.ground - c.ground > ledge ? -1 : 0) + (!u.none && u.ground - c.ground > ledge ? 1 : 0));
        if (dot(inward, inward) > 0.5) {
            float2 into = normalize(inward);
            float t = EdgeReach(xz, into, c.ground + ledge, ledge, pace * length(inward), false);
            // (2026-11-24: 物の下へ雪の面を伸ばして根元を埋めようとしたが、伸ばした所が物の影の中で黒く見え、
            // カメラの角度で物の外へはみ出したので、やめた。根元を埋めるのは別のやり方で)
            at += into * max(t - 0.02, 0);
        }
    }
    o.edge = float2(foot ? 1 : 0, rim ? 1 : 0);
    o.wpos = float3(at.x, rim ? c.ground + 0.002 : max(c.top, c.ground + 0.002), at.y);
    o.pos = mul(viewProj, float4(o.wpos, 1));
    return o;
}

float4 SnowLayerPS(SnowOut i) : SV_Target {
    if (i.edge.x > 0.001 && i.edge.y > 0.001) discard;  // (段をまたぐ三角形: 物の壁に張り付く雪の板になる)
    clip(SnowShape(i.wpos.xz, i.depth) - 0.02);  // (塊が立たない所: なし)
    float3 n = normalize(i.nrm);
    float3 v = normalize(eye.xyz - i.wpos);
    float grain = (WetNoise(i.wpos.xz * 40) - 0.5) * 0.05;  // (少し不均一に: プラスチックのようにしない)
    return float4(ToScreen(Fogged(Shade(kSnowWhite + grain, 0, 0.75, n, v, i.wpos), i.wpos)), 1);
}
)";

const char kSnowTrailHlsl[] = R"(
// 足跡: 目のまわりの各マスで雪が立てる一番高さ (SnowTrailOf)。フレームごとに受け継ぐ
// (テクスチャ 2 枚で、各フレームは片方からもう片方へ描く)。動くものがある所 (その下側、下から
// 見たもの: 動く形だけの 向きの地図) では、雪はそれより高く立てない - 押されて、そのままになる。
// 雪が降る間は、雪が育つのと同じ速さでまた埋まる。地図は巻き戻る: 各マスは、自分がどの
// ワールドのマスかを覚える (四角に新しく入ったマスは、足跡なしで始まる)。
// RendererShaderText(kWetSurfaceHlsl) の後。
cbuffer SnowTrail : register(b4) {
    float4 trailInfo;  // xy: 最初のマス (ワールドの x z)、z: 1 マス (m)、w: 1 辺のマス数
    float4 trailMore;  // x: 数のうちどれか (Play ごとに別)、y: 今どれだけ埋まるか (m)、z: 上向きの線が始まる所 (ワールドの y)、w: 下からの地図
};
Texture2D<float4> trailBefore : register(t9);

float4 SnowTrailVS(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 SnowTrailPS(float4 pos : SV_Position) : SV_Target {
    float n = trailInfo.w;
    float2 t = floor(pos.xy);
    float2 cell = trailInfo.xy + (t - trailInfo.xy - n * floor((t - trailInfo.xy) / n));
    float4 before = trailBefore.Load(int3(t, 0));
    float ceiling = all(before.yz == cell) && before.w == trailMore.x ? before.x : 1e30;
    if (ceiling < 1e29) ceiling += trailMore.y;
    int m = (int)trailMore.w;
    if (m >= 0 && mapInfo[m].x > 0.5) {
        float2 c = (cell + 0.5) * trailInfo.z;
        [unroll] for (int y = -1; y <= 1; ++y) {
            [unroll] for (int x = -1; x <= 1; ++x) {
                float past = MapPastAt(m, mul(mapViewProj[m], float4(c.x + x * trailInfo.z, trailMore.z, c.y + y * trailInfo.z, 1)));
                if (past > -1e29) ceiling = min(ceiling, trailMore.z - past + 0.03 * (abs(x) + abs(y)));  // (柔らかい縁)
            }
        }
    }
    return float4(ceiling, cell, trailMore.x);
}
)";

namespace {

void TrailFirstCell(Vec3 eye, float out[4]) {
    const float half = static_cast<float>(kTrailCells / 2);
    out[0] = (std::isfinite(eye.x) ? std::floor(eye.x / kTrailCell) : 0.f) - half;
    out[1] = (std::isfinite(eye.z) ? std::floor(eye.z / kTrailCell) : 0.f) - half;
    out[2] = kTrailCell;
    out[3] = static_cast<float>(kTrailCells);
}

}  // namespace

SnowLayerConstants SnowLayerOf(Vec3 eye, std::span<const WetWay> ways, std::span<const int> maps, int downMap, uint32_t trailCount, bool trails,
                               const WetMapInfo& place) {
    SnowLayerConstants k{};
    k.place[0] = place.x, k.place[1] = place.z, k.place[2] = place.cell, k.place[3] = place.cells;
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)) eye = {0, 0, 0};
    const float half = kSnowCell * static_cast<float>(kSnowCells) * 0.5f;
    k.grid[0] = std::floor(eye.x / kSnowCell) * kSnowCell - half;  // (ちょうどのマス単位: 目が動いても格子が泳がない)
    k.grid[1] = std::floor(eye.z / kSnowCell) * kSnowCell - half;
    k.grid[2] = kSnowCell;
    k.grid[3] = static_cast<float>(kSnowCells);
    k.info[0] = static_cast<float>(downMap);
    k.info[1] = eye.y;
    for (size_t j = 0; j < 4; ++j) {
        const bool has = j < ways.size() && j < kWetWays;
        k.snow[j][0] = has && std::isfinite(ways[j].snow) ? std::clamp(ways[j].snow, 0.f, kMostSnowDepth) : 0.f;
        k.snow[j][1] = has && j < maps.size() ? static_cast<float>(maps[j]) : -1.f;
    }
    if (trails) TrailFirstCell(eye, k.trail);
    k.trailMore[0] = static_cast<float>(trailCount);
    return k;
}

SnowTrailConstants SnowTrailOf(Vec3 eye, uint32_t count, float fill, int upMap) {
    SnowTrailConstants k{};
    TrailFirstCell(eye, k.trail);
    k.more[0] = static_cast<float>(count);
    k.more[1] = std::isfinite(fill) ? std::max(fill, 0.f) : 0.f;
    k.more[2] = (std::isfinite(eye.y) ? eye.y : 0.f) - 70.f;  // (歩く所より下: 下からの地図は目の 80 m 下から始まる)
    k.more[3] = static_cast<float>(upMap);
    return k;
}

uint32_t PrecipitationCount(const Precipitation& p) {
    const float amount = std::isfinite(p.amount) ? std::clamp(p.amount, 0.f, 1.f) : 0.f;
    const bool rain = p.kind == Precipitation::Kind::Rain;
    const float share = std::pow(amount, rain ? 1.4f : 1.f);
    return static_cast<uint32_t>(share * static_cast<float>(rain ? kMaxRaindrops : kMaxSnowflakes) + 0.5f);
}

FallSpeed FallOf(Precipitation::Kind kind) {
    // (2026-11-19: 前に受けていた風の半分 - 傾きすぎた)
    return kind == Precipitation::Kind::Rain ? FallSpeed{9.f, 3.f, 0.45f} : FallSpeed{0.9f, 0.6f, 0.5f};
}

Vec3 FallDirection(Precipitation::Kind kind, Vec3 wind) {
    const FallSpeed f = FallOf(kind);
    const float x = wind.x * f.carried, y = -(f.least + f.spread * 0.5f), z = wind.z * f.carried;
    const float len = std::sqrt(x * x + y * y + z * z);
    return {x / len, y / len, z / len};
}

std::vector<float> WetSurfaceData(std::span<const WetWay> ways, std::span<const int> maps, const WetMapInfo& place) {
    const size_t count = std::min(ways.size(), kWetWays);
    std::vector<float> data(4 * (2 + 2 * kWetWays), 0.f);
    data[0] = static_cast<float>(count);
    float* at = &data[4 * (1 + 2 * kWetWays)];  // (surfaceData[9]: 場所ごとの地図)
    at[0] = place.x, at[1] = place.z, at[2] = place.cell, at[3] = place.cells;
    for (size_t k = 0; k < count; ++k) {
        float* way = &data[4 * (1 + k)];
        way[0] = std::clamp(ways[k].sun, 0.f, 1.f);
        way[1] = std::clamp(ways[k].shade, 0.f, 1.f);
        way[2] = static_cast<float>(k < maps.size() ? maps[k] : -1);
        way[3] = std::isfinite(ways[k].seep) ? std::clamp(ways[k].seep, 0.f, 4.f) : 0.f;
        data[4 * (1 + kWetWays + k)] = std::isfinite(ways[k].snow) ? std::clamp(ways[k].snow, 0.f, kMostSnowDepth) : 0.f;
    }
    return data;
}

}  // namespace rm::weather
