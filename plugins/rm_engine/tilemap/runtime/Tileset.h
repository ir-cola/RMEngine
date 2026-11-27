#pragma once
// タイルマップ - タイルセット (.rmtileset。アセット): 画像そのものではなく、画像を指して「どう切るか」を
// 書いたファイル。中身はこの RM_STRUCT 1 つをそのまま JSON にしたもの (憲法 9)。知らないキー (後の段階の
// タイルごとの情報・オートタイルの組 …) は捨てずに書き戻す (憲法 6)。設計: docs/TILEMAP.md 3-1。
//
//   絵の元 (TileSource)   番号 (1〜255。並べ替えても消しても変わらない)・画像・タイルの幅 / 高さ・余白・間
//   タイル (TileInfo)      既定と違うタイルだけ (まばら): 名前・当たりの形・タグ・独自データの値 (段階 2)
//   独自データの欄         名前・種類 (真偽 / 整数 / 数 / 文字)・既定値。値はタイルの `data` に名前で持つ
//
// タイルの見分けは「絵の元の番号・列・行」(TileCell.h)。画像を右や下に広げても番号がずれない。
// 1 マス = 1 単位 (= 物理の 1 m): タイルはマスいっぱいに描く (グリッドのマスの大きさで決まる)。
// 純粋 (I/O も描画もしない)。tests/TestTilemap.cpp でテストする。

#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/space2d/runtime/Atlas.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::tilemap {

inline constexpr char kTilesetExtension[] = ".rmtileset";
inline constexpr char kTilesetKind[] = "rm_engine.tileset";
inline constexpr int kMaxSourceId = 255;

RM_STRUCT(Label="絵の元")
struct TileSource {
    RM_PROPERTY() int id = 1;  // マスの値の中の番号 (1〜255)。並べ替えても変わらない (画面では変えない)
    RM_PROPERTY(Edit, Label="名前", Tooltip="パレットの「絵の元」の一覧に出る名前") std::string name;
    RM_PROPERTY(Edit, Label="画像", Tooltip="タイルを並べた画像 (.png など)。アセットのパネルからドラッグするか、クリックして選びます", AssetKind="image") AssetRef image;
    RM_PROPERTY(Edit, Label="タイルの幅 (px)", Tooltip="1 つのタイルの横のピクセル数", Min=1, Max=4096) int tileWidth = 16;
    RM_PROPERTY(Edit, Label="タイルの高さ (px)", Tooltip="1 つのタイルの縦のピクセル数", Min=1, Max=4096) int tileHeight = 16;
    RM_PROPERTY(Edit, Label="余白 (px)", Tooltip="画像の縁の、タイルの無い幅", Min=0, Max=4096) int margin = 0;
    RM_PROPERTY(Edit, Label="間 (px)", Tooltip="タイルとタイルの間の、タイルの無い幅", Min=0, Max=4096) int spacing = 0;
    RM_PROPERTY(Edit, Label="くっきり (ドット絵)", Tooltip="拡大してもぼかさず、ピクセルをそのまま大きくします") bool crisp = true;
};

// タイルの当たりの形 (マスの中。設計図 3-1-1)。斜めは直角の角がどこにあるか。
RM_ENUM(Label="当たりの形")
enum class TileShape {
    RM_ITEM(Label="なし") None,
    RM_ITEM(Label="全部") Full,
    RM_ITEM(Label="上半分") Top,
    RM_ITEM(Label="下半分") Bottom,
    RM_ITEM(Label="左半分") Left,
    RM_ITEM(Label="右半分") Right,
    RM_ITEM(Label="斜め (左下)") SlopeBottomLeft,
    RM_ITEM(Label="斜め (右下)") SlopeBottomRight,
    RM_ITEM(Label="斜め (左上)") SlopeTopLeft,
    RM_ITEM(Label="斜め (右上)") SlopeTopRight,
    RM_ITEM(Label="自分で描く") Custom,
};

RM_ENUM(Label="独自データの種類")
enum class TileFieldKind {
    RM_ITEM(Label="真偽") Bool,
    RM_ITEM(Label="整数") Int,
    RM_ITEM(Label="数 (小数)") Number,
    RM_ITEM(Label="文字") Text,
};

RM_STRUCT(Label="独自データの欄")
struct TileField {
    RM_PROPERTY(Edit, Label="名前", Tooltip="スクリプトが引く名前 (かたさ・移動コスト など)") std::string name;
    RM_PROPERTY(Edit, Label="種類") TileFieldKind kind = TileFieldKind::Int;
    RM_PROPERTY(Edit, Label="既定値", Tooltip="値を決めていないタイルの値 (真偽は true / false)") std::string defaultValue;
};

RM_STRUCT()
struct TileDataValue {
    RM_PROPERTY() std::string name;
    RM_PROPERTY() std::string value;  // 文字のまま (欄の種類で読む。欄を消しても残る)
};

RM_STRUCT(Label="タイル")
struct TileInfo {
    RM_PROPERTY() int source = 0;  // どのタイルか: 絵の元の番号・列・行
    RM_PROPERTY() int column = 0;
    RM_PROPERTY() int row = 0;
    RM_PROPERTY(Edit, Label="名前", Tooltip="スクリプトが引く名前 (空でよい)") std::string name;
    RM_PROPERTY(Edit, Label="当たり", Tooltip="物理で当たる形。タブ「タイル」の「タイルセット」で、絵の上に塗っても決められます") TileShape shape = TileShape::None;
    RM_PROPERTY() std::vector<Vec2> points;  // 自分で描く形の点 (マスの中の 0〜1。Y が上)。凸の多角形 1 つ
    RM_PROPERTY(Edit, Label="タグ", Tooltip="スクリプトやナビゲーションが見る印 (壊せる・通れない など)") std::vector<std::string> tags;
    RM_PROPERTY() std::vector<TileDataValue> data;  // 独自データの値 (欄の名前で)
};

RM_STRUCT(Label="タイルセット")
struct Tileset {
    RM_PROPERTY() std::string kind = kTilesetKind;
    RM_PROPERTY() int format = 1;
    RM_PROPERTY(Edit, Label="絵の元", Tooltip="タイルを切り出す画像。1 つのタイルセットに何枚でも (255 枚まで)") std::vector<TileSource> sources;
    RM_PROPERTY(Edit, Label="独自データの欄", Tooltip="タイルごとに持たせる値の名前と種類") std::vector<TileField> fields;
    RM_PROPERTY() std::vector<TileInfo> tiles;  // 既定と違うタイルだけ (タブ「タイル」で決める)
};

// ファイルの中身 -> タイルセット。番号の重なりや範囲の外は直す (NormalizeIds)。nullopt: *error に理由。
std::optional<Tileset> ReadTileset(std::string_view bytes, std::string* error, reflect::Unknowns* unknowns = nullptr);
std::string WriteTileset(const Tileset& t, const reflect::Unknowns* unknowns = nullptr);

// 絵の元の番号を 1〜255 の重ならない数にする (後から足した方を、使っていない一番小さい番号に)。変えたら true。
bool NormalizeIds(Tileset& t);
// 足す絵の元の番号 (使っていない一番小さいもの。もう無い: 0)。
int NewSourceId(const Tileset& t);
const TileSource* FindSource(const Tileset& t, int id);
// 絵の元の切り方 (space2d の格子の切り方)。
space2d::GridCut CutOf(const TileSource& s);

// --- タイルごとの情報 (まばら) ----------------------------------------------------------------
// 絵の元 `source` の 列 `column`・行 `row` のタイルの情報 (無い: null = 既定)。
const TileInfo* FindTile(const Tileset& t, int source, int column, int row);
// 同じ。無ければ既定の情報 (そのタイルの番号つき)。
TileInfo TileOrDefault(const Tileset& t, int source, int column, int row);
// 既定 (名前・当たり・点・タグ・値が無い) か。
bool IsDefaultTile(const TileInfo& info);
// `info` を入れる (同じタイルのものは置き換え。既定なら消す: まばらのまま)。並びは絵の元・行・列の順。
void SetTile(Tileset& t, TileInfo info);
// 独自データの値 (無い: 欄の既定値。欄も無い: 空)。
std::string TileData(const Tileset& t, const TileInfo& info, std::string_view field);
// 値を入れる (欄の既定値と同じなら消す)。
void SetTileData(const Tileset& t, TileInfo& info, std::string_view field, std::string value);
const TileField* FindField(const Tileset& t, std::string_view name);

}  // namespace rm::tilemap
