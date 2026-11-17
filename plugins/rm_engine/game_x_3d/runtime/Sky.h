#pragma once
// ゲーム制作 × 3D - the scene's 空 (スカイボックス): a scene setting (game_production::SceneSettingInfo -
// the インスペクター shows it when the scene, the 階層's top row, is chosen), drawn behind everything in
// the scene view, the game view and the exported game by 3D 空間's renderer (RenderFrame::sky,
// SkyShader.h).
//
//   なし                  the camera's background colour, as before
//   地球の空              the air, the sun, the moon and the stars, clouds - by the 時刻 (0 .. 24; it goes on
//                         while the game plays when 1 日の長さ is set) and the 天気
//   宇宙                  stars, nebulae, the Milky Way and a sun
//   画像 (パノラマ 1 枚)  one picture around (equirectangular: 360 x 180 degrees)
//   画像 (6 枚)           a picture for each side of a cube around the eye
//   自分のシェーダー      a .rmsky file: HLSL with float3 Sky(SkyInput i) (SkyShader.h's NewSkyCode)
//
// 光を空に合わせる (地球の空, 宇宙): the scene's first directional light takes the sun's (at night the
// moon's) direction, colour and strength, and the ambient light the sky's colours - the numbers the
// sky is drawn with (SkyLook), so the light and the sky never disagree.
// Pure - tested in tests/TestSky.cpp.

#include "base/Math.h"
#include "base/Ref.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/SkyShader.h"

#include <optional>
#include <vector>

namespace rm::game_production {
struct SystemContext;
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

RM_ENUM(Label="天気")
enum class Weather {
    RM_ITEM(Label="晴れ") Clear,
    RM_ITEM(Label="晴れ時々くもり") PartlyCloudy,
    RM_ITEM(Label="くもり") Cloudy,
    RM_ITEM(Label="雨") Rain,
    RM_ITEM(Label="雷雨") Storm,
    RM_ITEM(Label="雪") Snow,
    RM_ITEM(Label="霧") Fog,
};

RM_STRUCT(Label="空")
struct Sky {
    RM_PROPERTY(Edit, Label="種類", Tooltip="シーンの後ろに描く空。なし: カメラの背景の色 / 地球の空: 時刻と天気で変わる空 / 宇宙: 星と星雲 / 画像: パノラマ 1 枚か 6 枚 / 自分のシェーダー: .rmsky のファイル")
    SkyKind kind = SkyKind::None;
    RM_PROPERTY(Edit, Label="明るさ", Tooltip="空の色に掛けます", Min=0, Max=10, Step=0.05, ShowIf="kind=Earth|Space|Panorama|SixImages|Custom") float brightness = 1.f;
    // 地球の空
    RM_PROPERTY(Edit, Label="時刻", Tooltip="0〜24 時。6 時に東 (+X) から昇り、12 時に一番高く、18 時に西へ沈みます", Min=0, Max=24, Step=0.25, ShowIf="kind=Earth") float hour = 12.f;
    RM_PROPERTY(Edit, Label="1 日の長さ (秒)", Tooltip="Play 中、この秒数で 24 時間すすみます (0: 時刻は止まったまま)。書き出したゲームでも同じ", Min=0, Max=1000000, ShowIf="kind=Earth") float dayLength = 0.f;
    RM_PROPERTY(Edit, Label="天気", Tooltip="雲の量と暗さ、もや。雷雨はときどき雲が光ります", ShowIf="kind=Earth") Weather weather = Weather::Clear;
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

// Towards the sun (length 1). 地球の空: by the hour - up in the east (+X) at 6, highest (sunHeight, on
// the +Z side) at 12, down in the west at 18, as low below at 0; the path turned by sunDirection.
// The others: where the 地球の空's sun is at 12.
Vec3 SunDirection(const Sky& sky);

// What the sky is this frame: the code it is drawn with (a built-in one; nullopt: the user's .rmsky
// or none), its numbers, and the light that follows it.
struct SkyLook {
    std::optional<space3d::BuiltinSky> builtin;
    space3d::SkyConstants constants{};  // (invViewProj is the renderer's)
    bool drivesLight = false;           // 光を空に合わせる (地球の空, 宇宙)
    Vec3 lightGoes{0, -1, 0};           // where the light goes (from the sun, or the moon at night)
    Color lightColor{1, 1, 1, 1};       // linear
    float lightStrength = 1.f;          // multiplies the light component's own 強さ
    Color ambientSky{0.16f, 0.16f, 0.16f, 1}, ambientGround{0.08f, 0.08f, 0.08f, 1};
};
// `seconds`: the time the clouds drift and the stars twinkle by (the game's: 0 in the editor until Play).
SkyLook LookOf(const Sky& sky, double seconds);

// 光を空に合わせる: the first directional light takes the look's direction and colour, and its strength
// times the look's (none: one is added); the ambient light's colours become the sky's (its strength
// stays). Nothing when the look does not drive the light.
void FollowSky(const SkyLook& look, std::vector<space3d::Light>& lights, space3d::Ambient& ambient);

// The system 空の時刻: while the game plays, 地球の空's 時刻 goes on (1 日の長さ seconds a day).
void RunSkyClock(game_production::SystemContext& ctx);

}  // namespace rm::game_x_3d
