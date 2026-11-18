#pragma once
// How an asset is read ("取り込みの設定"): kept in its .meta under "import", next to the id -
//   { "guid": "...", "import": { "scale": 0.01, "up": "Z" } }
// Written once here as RM_STRUCTs, so saving, loading, the インスペクター and undo come from them (rule 9).
// Keys of a newer version inside "import" (and around it) are written back unchanged (rule 6).
//
// The editor shows them when an asset is selected in the アセット panel and writes the .meta at once.
// Whoever reads the asset (a plugin: the 3D model loader, the textures, the 2D sprites) reads them
// with ReadImport and reads the file again when ChangedAt moves (the file OR its .meta changed).
// The game reads the same .meta files.

#include "base/Json.h"
#include "core/asset/AssetIndex.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"

#include <filesystem>
#include <string_view>
#include <system_error>

namespace rm::asset {

RM_ENUM(Label="上の向き")
enum class UpAxis {
    RM_ITEM(Label="Y が上") Y,
    RM_ITEM(Label="Z が上 (Blender の OBJ・3ds Max など)") Z,
};

RM_ENUM(Label="圧縮")
enum class TextureCompression {
    RM_ITEM(Label="自動") Auto,
    RM_ITEM(Label="なし") None,
    RM_ITEM(Label="BC1 (不透明な色)") BC1,
    RM_ITEM(Label="BC3 (透明のある色)") BC3,
    RM_ITEM(Label="BC4 (1 チャンネル)") BC4,
    RM_ITEM(Label="BC5 (ノーマルマップ)") BC5,
    RM_ITEM(Label="BC7 (きれいな色と透明)") BC7,
};

RM_STRUCT(Label="3D モデルの取り込み")
struct ModelImport {
    RM_PROPERTY(Edit, Label="大きさ", Tooltip="読み込んだ形に掛ける倍率 (センチメートルで作ったモデルなら 0.01)", Min=0.0001, Max=10000) float scale = 1;
    RM_PROPERTY(Edit, Label="上の向き", Tooltip="作ったソフトで上だった軸。Z なら Y が上になるように起こして読みます") UpAxis up = UpAxis::Y;
    RM_PROPERTY(Edit, Label="中の絵の圧縮", Tooltip="モデルの中 (.glb など) の絵を GPU でどの形で持つか。自動: 不透明な色は BC1、透明のある色は BC7、ノーマルマップは BC5") TextureCompression textureCompression = TextureCompression::Auto;
    RM_PROPERTY(Edit, Label="中の絵の大きさの上限 (ピクセル)", Tooltip="モデルの中の絵がこれより大きければ縮めて読みます", Min=16, Max=8192) int textureMaxSize = 4096;
};

RM_ENUM(Label="ストリーミング")
enum class SoundStream {
    RM_ITEM(Label="自動 (10 秒より長ければ)") Auto,
    RM_ITEM(Label="する (少しずつ解いて流す)") Always,
    RM_ITEM(Label="しない (全部解いて持つ)") Never,
};

// A sound (音: WAV / OGG / MP3 ...). Read by the 音 plugin (rm_engine/audio) - the core knows the
// settings, not the sound.
RM_STRUCT(Label="音の取り込み")
struct SoundImport {
    RM_PROPERTY(Edit, Label="音量", Tooltip="この音をいつも何倍の大きさで鳴らすか (音源の部品・スクリプトの音量にさらに掛かります)", Min=0, Max=4, Step=0.01) float volume = 1;
    RM_PROPERTY(Edit, Label="ストリーミング", Tooltip="長い BGM は全部を解かずに少しずつ解いて流します (メモリを使わない)。短い効果音は全部解いて持つ方が速く鳴ります") SoundStream stream = SoundStream::Auto;
    RM_PROPERTY(Edit, Label="ループの始め (秒)", Tooltip="繰り返すとき、終わりからここへ戻ります (イントロの後から繰り返す BGM)", Min=0, Step=0.01) float loopStart = 0;
    RM_PROPERTY(Edit, Label="ループの終わり (秒)", Tooltip="繰り返すとき、ここで始めへ戻ります。0: 音の最後まで", Min=0, Step=0.01) float loopEnd = 0;
};

RM_ENUM(Label="フィルタ")
enum class TextureFilter {
    RM_ITEM(Label="滑らか") Smooth,
    RM_ITEM(Label="くっきり (ドット絵)") Sharp,
};

RM_ENUM(Label="端の扱い")
enum class TextureWrap {
    RM_ITEM(Label="繰り返し") Repeat,
    RM_ITEM(Label="端で止める") Clamp,
    RM_ITEM(Label="鏡のように反転") Mirror,
};

// (Before 2026-11-18 the filter was a bool "sharp": MigrateImport reads it as `filter`.)
RM_STRUCT(Label="画像の取り込み")
struct ImageImport {
    RM_PROPERTY(Edit, Label="フィルタ", Tooltip="滑らか: 拡大・縮小をなめらかに / くっきり: 拡大してもぼかさず、ピクセルをそのまま大きくします (ドット絵。3D のテクスチャと 2D のスプライト)") TextureFilter filter = TextureFilter::Smooth;
    RM_PROPERTY(Edit, Label="端の扱い", Tooltip="3D のテクスチャで、UV が 0〜1 の外に出たとき。繰り返し: 絵を並べる / 端で止める: 端の色を伸ばす (1 枚の看板の縁に反対側の色がにじまない) / 鏡のように反転: 並べるたびに裏返す (つなぎ目が目立たない)") TextureWrap wrap = TextureWrap::Repeat;
    RM_PROPERTY(Edit, Label="ミップマップ", Tooltip="半分ずつ小さくした絵を作り、遠くの物に使います (ちらつかない)。いつも近くで同じ大きさに見える絵 (UI など) は外すと、GPU のメモリが約 3 割減ります") bool mipmaps = true;
    RM_PROPERTY(Edit, Label="透明部分を保つ", Tooltip="小さい絵の透明の割合を元の絵とそろえます。葉・草・金網のように透明で切り抜く絵が、遠くで消えたり細ったりしません", ShowIf="mipmaps") bool keepCoverage = false;
    RM_PROPERTY(Edit, Label="切り抜きの境目", Tooltip="透明部分を保つときの境目のアルファ (マテリアルの「切り抜き」の値と同じにします)", Min=0.01, Max=0.99, Step=0.01, ShowIf="mipmaps&keepCoverage") float coverageCutoff = 0.5f;
    RM_PROPERTY(Edit, Label="ミップマップ・ストリーミング", Tooltip="画面での大きさに要る細かさの段だけを GPU に置きます (遠くの物の絵は小さい段だけ)。近づくと細かい段を足します。GPU のメモリが減ります (読み込んだ絵はメモリに残ります)", ShowIf="mipmaps") bool streaming = false;
    RM_PROPERTY(Edit, Label="大きさの上限 (ピクセル)", Tooltip="これより大きい画像は縮めて読みます (GPU のメモリを減らす)。ファイルはそのまま", Min=16, Max=8192) int maxSize = 4096;
    RM_PROPERTY(Edit, Label="圧縮", Tooltip="3D のテクスチャとして GPU でどの形で持つか。自動: 不透明な色は BC1、透明のある色は BC7 (古い GPU では BC3)、ノーマルマップは BC5、1 チャンネルの数は BC4。なし: 圧縮しない (メモリは 4〜8 倍)") TextureCompression compression = TextureCompression::Auto;
};

// The settings type of a kind of asset (null: it has none).
const reflect::TypeDesc* ImportTypeOf(Kind kind);

// Pure: settings written by an older version, as this one names them (in place). Now: an image's
// "sharp": true/false becomes "filter": "Sharp"/"Smooth" (unless "filter" is there already).
void MigrateImport(json::Value& import);

// Pure: the "import" object of a .meta's text (an empty object when there is none; migrated), and the
// text with another one (every other key kept).
json::Value ImportInMeta(std::string_view metaText);
std::string MetaWithImport(std::string_view metaText, const json::Value& import);

// Files: the import settings of `file` (an asset's full path; read from "<file>.meta").
json::Value ReadImportJson(const std::filesystem::path& file);
// Writes them into its .meta. A .meta that is not there is made with `guid` (the asset's id, when it is
// known); one that is locked or broken is never written over. False: could not.
bool WriteImportJson(const std::filesystem::path& file, const json::Value& import, const std::string& guid = {});
template <class T>
T ReadImport(const std::filesystem::path& file) {
    T t;
    reflect::Load(t, ReadImportJson(file));
    return t;
}
// The later of the file's and its .meta's times: when it moves, read the asset again (a setting
// changed counts as the file changing). `ec` is set when the file itself is not there.
std::filesystem::file_time_type ChangedAt(const std::filesystem::path& file, std::error_code& ec);

}  // namespace rm::asset
