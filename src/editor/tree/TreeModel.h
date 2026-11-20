#pragma once
// コアのツリー (階層) の純粋な部分: どの行が見えるか、選択、ドロップの行き先。
// 描画も I/O もしない - tests/TestTree.cpp でテストする。
//
// 設計 RME_PLATFORM 4-c: コアはツリーを出し (開く / 閉じる、選ぶ、ドラッグで並べ替え、
// 名前の変更、検索、右クリックメニューの枠)、中に何があるかは知らない。ソフトが項目を並べ
// (ゲーム: エンティティとその親、3D モデル: メッシュとボーン、イラスト: レイヤー)、
// 移動やメニューの行が何を意味するかを決める。

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rm::editor::tree {

using ItemId = uint64_t;  // ソフトが決める。0 = なし / 最上位

// 行の名前の前の小さな絵 (コアが描く。どれにするかはソフトが選ぶ)。
enum class Icon : uint8_t {
    None,    // 絵なし (名前は絵があるはずの所から始まる)
    Object,  // 自分の形を持たないもの (丸): 場所のないエンティティ
    Cube,    // 3D 空間にあるもの
    Camera,  // 見るもの (ゲームのカメラ)
    Light,   // ほかのものを照らすもの
    Square,  // 2D の平面にあるもの
    Effect,  // a sparkle (パーティクル)
    Scene,   // レイヤー (ゲームのシーン: シーン の一覧)
    // files (the アセット panel)
    Folder,
    File,
    Image,
    Model,
    Audio,
    Font,
    Text,
};
// 名前で、このヘッダーが見えないプラグイン向け (部品の ComponentInfo::icon): "object",
// "cube", "camera", "light", "square", "effect", "image", "audio"。ほかは None。
Icon IconNamed(std::string_view name);

struct Item {
    ItemId id = 0;
    ItemId parent = 0;     // 0 = 最上位
    std::string name;      // UTF-8
    bool canHaveChildren = true;
    bool dimmed = false;   // 薄く描く (例: 無効なエンティティ)
    Icon icon = Icon::None;
    std::wstring badge;    // 行の右にある一言。アクセント色で (シーン の一覧の 最初)。空: なし
};
// ソフトは項目を表示順に並べる: 親は子より前、兄弟は順番どおり。

struct Row {
    size_t item = 0;  // 項目の一覧への添字
    int depth = 0;
    bool hasChildren = false;
    bool expanded = false;  // 子を表示している
};

// 描く行。`collapsed`: 子を隠す項目。空でない `filter` は、名前にそれを含む項目 (ASCII の文字は
// 大文字小文字を区別しない) とその祖先を、すべて開いて表示する。
std::vector<Row> VisibleRows(std::span<const Item> items, const std::unordered_set<ItemId>& collapsed, std::string_view filter);
bool NameMatches(std::string_view name, std::string_view filter);

// 一覧の中の項目の添字。無ければ npos。一覧を端から探す: 何度も探すなら ItemIndex を使う。
size_t Find(std::span<const Item> items, ItemId id);

// id -> 一覧の中の位置。(フレームごとに) 1 度作るので、ID での検索は手間がかからない。ここにある、
// 項目ごと・選択したものごとに探す関数はすべてこれ (または set) を使う: 何千もの項目がすべて選ばれて
// いても軽いまま (items x selected に比例して増える作業はない)。
class ItemIndex {
public:
    explicit ItemIndex(std::span<const Item> items);
    size_t Find(ItemId id) const;  // 無ければ npos
    bool Has(ItemId id) const { return map_.contains(id); }

private:
    std::unordered_map<ItemId, size_t> map_;
};
// `id` が `ancestor` 自身か、その下のどこかにあれば true。
bool IsUnder(std::span<const Item> items, ItemId id, ItemId ancestor);

struct Selection {
    std::vector<ItemId> ids;  // 表示順
    ItemId anchor = 0;        // Shift の範囲の始まり
    ItemId focus = 0;         // キーボードのカーソル / 最後にクリックしたもの

    bool Contains(ItemId id) const;  // ids を端から探す: フレームの全行には Set() を使う
    std::unordered_set<ItemId> Set() const { return {ids.begin(), ids.end()}; }
    bool operator==(const Selection&) const = default;
};

// 行のクリック: ふつう = それだけ、Ctrl = 切り替え、Shift = 見えている行のうちアンカーからの範囲、
// Ctrl+Shift = その範囲を足す。
void Click(Selection& sel, std::span<const Item> items, std::span<const Row> rows, ItemId id, bool ctrl, bool shift);
void SelectOnly(Selection& sel, ItemId id);
void SelectAll(Selection& sel, std::span<const Item> items, std::span<const Row> rows);
// 無くなった ID を忘れ、残りを表示順にする。ID が変わったら true を返す。
bool Prune(Selection& sel, std::span<const Item> items);

// まとめて動かす (またはコピー / 削除する) 選択項目: 祖先も選ばれている項目は、その祖先と
// 一緒に行く。表示順。
std::vector<ItemId> Roots(std::span<const Item> items, std::span<const ItemId> ids);

// ドラッグした項目の行き先: `parent` の下の、動かす項目を「数えない」子の中での `index` の位置
// (なのでソフトは先にそれらを取り除いてから、そこに差し込む)。
struct Drop {
    ItemId parent = 0;
    size_t index = 0;
    enum class Mark { Before, Inside, After, End } mark = Mark::End;  // ビューが描くもの
    ItemId target = 0;                                                // 印が付いている行 (End では使わない)
    bool operator==(const Drop&) const = default;
};

// rowIndex: マウスの下に見えている行、`fraction`: 0 (その上端) .. 1 (その下端)。
// 上 4 分の 1 = その前、下 4 分の 1 = その後 (開いているときは最初の子)、真ん中 =
// その中 (最後に)。ドロップすると項目が自分の中に入ってしまうときは nullopt。
std::optional<Drop> DropOnRow(std::span<const Item> items, std::span<const Row> rows, size_t rowIndex, float fraction,
                              std::span<const ItemId> moving);
// 最後の行より下: 最上位の最後。
Drop DropAtEnd(std::span<const Item> items, std::span<const ItemId> moving);

}  // namespace rm::editor::tree
