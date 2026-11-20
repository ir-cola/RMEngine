#pragma once
// ゲーム制作 × 3D - シーンの 空 (スカイボックス): シーンの設定 (game_production::SceneSettingInfo -
// シーン、つまり 階層 の一番上の行を選ぶと インスペクター に出る)。シーンビュー、ゲームビュー、
// 書き出したゲームで、3D 空間のレンダラーが何よりも後ろに描く (RenderFrame::sky, SkyShader.h)。
//
//   なし                  カメラの背景色。これまでどおり
//   地球の空              空気、太陽、月、星、雲 - 時刻 (0 .. 24) で決まる (1 日の長さ を設定すると
//                         ゲームが動く間進む)。どれだけ曇るか、雷、雲を流す風は天気のもの
//                         (プラグイン 天候: SkyWeather - 無ければ、少し雲のある晴れ)
//   宇宙                  星、星雲、天の川、太陽
//   画像 (パノラマ 1 枚)  ぐるりと 1 枚の絵 (正距円筒: 360 x 180 度)
//   画像 (6 枚)           目のまわりの立方体の各面に 1 枚ずつの絵
//   自分のシェーダー      .rmsky ファイル: float3 Sky(SkyInput i) を持つ HLSL (SkyShader.h の NewSkyCode)
//
// 光を空に合わせる (地球の空, 宇宙): シーンの最初の平行光が太陽の (夜は月の) 向き、色、強さを
// とり、環境光は空の色をとる - 空を描くのに使う数 (SkyLook) なので、光と空が食い違うことは
// ない。
// 純粋 - tests/TestSky.cpp でテストする。

#include "base/Math.h"
#include "base/Ref.h"
#include "core/RuntimeRegistry.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/SkyShader.h"

#include <optional>
#include <vector>

namespace rm::game_production {
struct SystemContext;
class Scene;
}

namespace rm::game_x_3d {

RM_ENUM(Label="空の種類")
enum class SkyKind {
    RM_ITEM(Label="なし (カメラの背景の色)") None,
    RM_ITEM(Label="地球の空") Earth,
    RM_ITEM(Label="宇宙") Space,
    RM_ITEM(Label="画像 (パノラマ 1 枚)") Panorama,
    RM_ITEM(Label="画像 (6 枚)") SixImages,
    RM_ITEM(Label="自分のシェーダー") Custom,
};

RM_STRUCT(Label="空")
struct Sky {
    RM_PROPERTY(Edit, Label="種類", Tooltip="シーンの後ろに描く空。なし: カメラの背景の色 / 地球の空: 時刻と天気で変わる空 / 宇宙: 星と星雲 / 画像: パノラマ 1 枚か 6 枚 / 自分のシェーダー: .rmsky のファイル")
    SkyKind kind = SkyKind::None;
    RM_PROPERTY(Edit, Label="明るさ", Tooltip="空の色に掛けます", Min=0, Max=10, Step=0.05, ShowIf="kind=Earth|Space|Panorama|SixImages|Custom") float brightness = 1.f;
    // 地球の空
    RM_PROPERTY(Edit, Label="時刻", Tooltip="0〜24 時。6 時に東 (+X) から昇り、12 時に一番高く、18 時に西へ沈みます", Min=0, Max=24, Step=0.25, ShowIf="kind=Earth") float hour = 12.f;
    RM_PROPERTY(Edit, Label="1 日の長さ (秒)", Tooltip="Play 中、この秒数で 24 時間すすみます (0: 時刻は止まったまま)。書き出したゲームでも同じ", Min=0, Max=1000000, ShowIf="kind=Earth") float dayLength = 0.f;
    RM_PROPERTY(Edit, Label="太陽の高さ (度)", Tooltip="地球の空: 真昼の高さ / 宇宙・自分のシェーダー: 太陽のある高さ (0 で地平線、90 で真上)", Min=-90, Max=90, ShowIf="kind=Earth|Space|Custom") float sunHeight = 60.f;
    RM_PROPERTY(Edit, Label="太陽の向き (度)", Tooltip="真上から見て、太陽の通り道 (地球の空) / 太陽 (宇宙など) を回します。0: 真昼の太陽は後ろ (+Z) の空", Min=-360, Max=360, ShowIf="kind=Earth|Space|Custom") float sunDirection = 0.f;
    RM_PROPERTY(Edit, Label="光を空に合わせる", Tooltip="シーンの 1 つ目の平行光 (無ければ足します) の向き・色・強さと、環境光の色を、空の太陽 (夜は月) に合わせます。光の部品の「強さ」は倍率として残ります", ShowIf="kind=Earth|Space") bool driveLight = true;
    // 宇宙
    RM_PROPERTY(Edit, Label="星の多さ", Min=0, Max=4, Step=0.05, ShowIf="kind=Space") float stars = 1.f;
    RM_PROPERTY(Edit, Label="星雲の濃さ", Min=0, Max=4, Step=0.05, ShowIf="kind=Space") float nebula = 0.6f;
    RM_PROPERTY(Edit, Label="星雲の色 1", Min=0, Max=1, ShowIf="kind=Space") Color nebulaColor1{0.55f, 0.20f, 0.75f, 1};
    RM_PROPERTY(Edit, Label="星雲の色 2", Min=0, Max=1, ShowIf="kind=Space") Color nebulaColor2{0.15f, 0.45f, 0.85f, 1};
    RM_PROPERTY(Edit, Label="天の川の濃さ", Min=0, Max=4, Step=0.05, ShowIf="kind=Space") float milkyWay = 0.8f;
    RM_PROPERTY(Edit, Label="太陽を描く", ShowIf="kind=Space") bool showSun = true;
    // 画像
    RM_PROPERTY(Edit, Label="画像", Tooltip="パノラマ: 横 360 度 x 縦 180 度の 1 枚 (横が縦の 2 倍)。真ん中が前 (-Z) / 自分のシェーダー: sky0 として読めます", AssetKind="image", ShowIf="kind=Panorama|Custom") AssetRef picture;
    RM_PROPERTY(Edit, Label="回す (度)", Tooltip="真上から見て画像を回します", Min=-360, Max=360, ShowIf="kind=Panorama|SixImages") float turn = 0.f;
    RM_PROPERTY(Edit, Label="右 (+X)", AssetKind="image", ShowIf="kind=SixImages") AssetRef right;
    RM_PROPERTY(Edit, Label="左 (-X)", AssetKind="image", ShowIf="kind=SixImages") AssetRef left;
    RM_PROPERTY(Edit, Label="上 (+Y)", Tooltip="下の辺が前 (-Z) の画像とつながる向き", AssetKind="image", ShowIf="kind=SixImages") AssetRef up;
    RM_PROPERTY(Edit, Label="下 (-Y)", Tooltip="上の辺が前 (-Z) の画像とつながる向き", AssetKind="image", ShowIf="kind=SixImages") AssetRef down;
    RM_PROPERTY(Edit, Label="後ろ (+Z)", AssetKind="image", ShowIf="kind=SixImages") AssetRef back;
    RM_PROPERTY(Edit, Label="前 (-Z)", Tooltip="カメラが最初に向いている方 (-Z) に見える画像", AssetKind="image", ShowIf="kind=SixImages") AssetRef front;
    // 自分のシェーダー
    RM_PROPERTY(Edit, Label="シェーダー", Tooltip="空のシェーダーのファイル (.rmsky)。⋮ の「新しい空のシェーダーを作る」で例から作れます", AssetKind=".rmsky", ShowIf="kind=Custom") AssetRef shader;
    RM_PROPERTY(Edit, Label="値", Tooltip="シェーダーで i.value として読めます", ShowIf="kind=Custom") Vec3 value{0, 0, 0};
    RM_PROPERTY(Edit, Label="色", Tooltip="シェーダーで i.color として読めます", Min=0, Max=1, ShowIf="kind=Custom") Color color{1, 1, 1, 1};
};

// 太陽に向かう向き (長さ 1)。地球の空: 時刻で決まる - 6 時は東 (+X) で上がり、12 時が一番高く
// (sunHeight、+Z 側)、18 時は西で下り、0 時は同じだけ下。その道は sunDirection で回す。
// ほかの空: 12 時の 地球の空 の太陽がある所。
Vec3 SunDirection(const Sky& sky);

// 天気が空に与えるもの (プラグイン 天候 が伝える: SkyWeatherInfo)。初期値は、少し雲のある
// 晴れで風なし。
struct SkyWeather {
    float cover = 0.15f, dark = 0.0048f, haze = 0.006f;  // 雲の量、その暗さ、もや (0 .. 1)
    float through = 0.992f;                              // 太陽の光がどれだけ通るか
    float flash = 0;                                      // 雷: a flash now (0 .. 1)
    float driftX = 0, driftZ = 0;  // 風が雲をどれだけ運んだか (ワールドの単位)
    float windX = 0, windZ = 0;    // 今の風 (m/s、ワールド)
};
// RuntimeRegistry の点: 項目は SkyWeatherInfo (static)。伝えてきた最初のものを使う。
inline constexpr char kSkyWeatherPoint[] = "rm_engine.game_x_3d.sky_weather";
struct SkyWeatherInfo {
    // 空が見せる、`seconds` (ゲームの時間) のときのシーンの天気。false: 天気が無い。
    bool (*of)(const game_production::Scene& scene, double seconds, SkyWeather& out) = nullptr;
};
SkyWeather SkyWeatherOf(const RuntimeRegistry* registry, const game_production::Scene& scene, double seconds);

// このフレームの空: 描くコード (組み込みのもの。nullopt: ユーザーの .rmsky か無し)、その数、
// それに従う光。
struct SkyLook {
    std::optional<space3d::BuiltinSky> builtin;
    space3d::SkyConstants constants{};  // (invViewProj はレンダラーの)
    bool drivesLight = false;           // 光を空に合わせる (地球の空, 宇宙)
    Vec3 lightGoes{0, -1, 0};           // 光が向かう向き (太陽から、夜は月から)
    Color lightColor{1, 1, 1, 1};       // リニア
    float lightStrength = 1.f;          // multiplies the light component's own 強さ
    Color ambientSky{0.16f, 0.16f, 0.16f, 1}, ambientGround{0.08f, 0.08f, 0.08f, 1};
    float flash = 0;  // 雷: 今の閃光 (0 .. 1。雲と環境光を照らす)
};
// `seconds`: 星が瞬くもとになる時間 (ゲームの: エディターでは Play まで 0)。`weather`:
// 天気が空に与えるもの (地球の空)。
SkyLook LookOf(const Sky& sky, double seconds, const SkyWeather& weather = {});

// 光を空に合わせる: 最初の平行光が、見た目の向きと色、見た目の強さを掛けた強さをとる (無ければ
// 1 つ足す)。環境光の色は空の色になる (強さはそのまま。雷の閃光で明るくなる)。見た目が光を
// 動かさないときは何もしない。
void FollowSky(const SkyLook& look, std::vector<space3d::Light>& lights, space3d::Ambient& ambient);

// システム 空の時刻: ゲームが動く間、地球の空の 時刻 が進む (1 日の長さ 秒で 1 日)。
void RunSkyClock(game_production::SystemContext& ctx);

}  // namespace rm::game_x_3d
