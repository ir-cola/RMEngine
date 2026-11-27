#pragma once
// タイルマップ - タイルセット (.rmtileset。アセット): 画像そのものではなく、画像を指して「どう切るか」を
// 書いたファイル。中身はこの RM_STRUCT 1 つをそのまま JSON にしたもの (憲法 9)。知らないキー (後の段階の
// タイルごとの情報・オートタイルの組 …) は捨てずに書き戻す (憲法 6)。設計: docs/TILEMAP.md 3-1。
//
//   絵の元 (TileSource)   番号 (1〜255。並べ替えても消しても変わらない)・画像・タイルの幅 / 高さ・余白・間
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

RM_STRUCT(Label="タイルセット")
struct Tileset {
    RM_PROPERTY() std::string kind = kTilesetKind;
    RM_PROPERTY() int format = 1;
    RM_PROPERTY(Edit, Label="絵の元", Tooltip="タイルを切り出す画像。1 つのタイルセットに何枚でも (255 枚まで)") std::vector<TileSource> sources;
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

}  // namespace rm::tilemap
