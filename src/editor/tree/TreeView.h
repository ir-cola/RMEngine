#pragma once
// コアのツリービュー (階層 パネル)。すべてのソフトのすべてのツリーを 1 つのコードで描く
// (憲法 3)。ソフトは tree::Source を実装するだけ。
//
//   クリック / Ctrl+クリック / Shift+クリック     選ぶ (見えている行の範囲)
//   ▸ / ▾, ← / →                         閉じる / 開く (閉じた項目での ← は親へ行く)
//   ↑ / ↓ / Home / End (+Shift)          選択を動かす
//   行をドラッグ                         動かす: 行の上 / 下の端 = その前 / 後、
//                                        真ん中 = その中、最後の行より下 = 最後。
//                                        ツリーの外の参照フィールドへ (Source::DragOut)
//   F2 / ダブルクリック                  その場で名前を変える (Enter / ほかをクリック = 完了、Esc = 取り消し)
//   Ctrl+A                               すべて選ぶ
//   右クリック                           ソフトのメニュー (+ 名前を変更)
//   検索ボックス                         一致する項目とその親を出す
//
// ビューはソフトのデータを変えない。名前の変更と移動はソフトへの頼みで、ソフトがそれを
// Undo の項目にする (憲法 7)。メニューの行が何をするかはソフト次第。

#include "base/Json.h"
#include "editor/lang/Lang.h"
#include "editor/tree/TreeModel.h"
#include "editor/widgets/Widgets.h"
#include "ui/Context.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace rm::editor::tree {

// 右クリックメニューの 1 行。ショートカットを付けてもよい (ツリーがキーを持っている間効く)。
using Action = w::Action;

// 行の絵 (Item::icon) を、ウィジェットが描くとおりに (行と、参照フィールドの一覧)。
w::Icon PictureOf(Icon icon);

// ソフトがツリーに入れるもの。
class Source {
public:
    virtual ~Source() = default;
    // すべての項目を表示順に (Item を見る)。ツリーを描くたびに呼ぶ。
    virtual void Items(std::vector<Item>& out) = 0;
    // 項目がないときに出す。
    virtual std::wstring EmptyText() const { return Tr(L"まだ何もありません。"); }

    // 省略可: 項目の上にある、項目ではない行 (階層 のシーン): いつもあり、スクロールせず、検索にも
    // 掛からない。クリックすると選択を消し、TopClicked を呼ぶ (SelectionChanged のあと)。右クリックは
    // 何もない所のメニュー (Actions に何も無い) を開く。ここにドロップした行は最上位の最後に行く。
    struct TopRow {
        std::wstring name;
        w::Icon icon = w::Icon::Object;
        bool selected = false;  // 選ばれたように描く (それが指すものはインスペクターにある)
    };
    virtual std::optional<TopRow> Top() const { return std::nullopt; }
    virtual void TopClicked() {}

    virtual bool CanRename(ItemId id) const {
        (void)id;
        return false;
    }
    virtual void Rename(ItemId id, const std::string& nameUtf8) {
        (void)id;
        (void)nameUtf8;
    }
    // `items` (選択の Roots。表示順) を `parent` の下の `index` に動かす (Drop を見る)。
    virtual bool CanMove() const { return false; }
    virtual void Move(std::span<const ItemId> items, ItemId parent, size_t index) {
        (void)items;
        (void)parent;
        (void)index;
    }
    // 選択が変わった (選択項目が消えたときも。例: Undo のあと)。
    virtual void SelectionChanged(std::span<const ItemId> ids) { (void)ids; }
    // 右クリックのメニューの行 (何もない所では選択は空) とそのショートカット。
    virtual std::vector<Action> Actions(std::span<const ItemId> selection) {
        (void)selection;
        return {};
    }
    // ツリーから別のパネル (インスペクターの参照フィールド) へドラッグした行: それが何か
    // (w::DragThing)。既定: ID を持つ "item" (EntityRef フィールドが受け取る)。
    virtual w::DragThing DragOut(std::span<const ItemId> items);
    // 行の高さ (DIP)。高い行ほど絵が大きい (アセット パネルのサムネイル)。
    virtual float RowHeight() const { return 24.f; }
    // 行の絵を `r` の中に自分で描いて (サムネイル) true を返す。false: コアが項目の Icon を描く。
    virtual bool DrawPicture(ui::Context& c, const Item& item, const ui::Rect& r) {
        (void)c;
        (void)item;
        (void)r;
        return false;
    }
};

// ツリーごと (ソフトごとに 1 つ): 選択、開いている / 閉じている項目、検索、名前変更、ドラッグ。
struct State {
    Selection sel;
    std::unordered_set<ItemId> collapsed;
    std::string filter;

    // 外から (ソフトから) 設定する: 項目が並んだあとの次の Draw で適用する。
    bool hasPending = false;
    std::vector<ItemId> pending;
    bool pendingRename = false;

    ItemId renaming = 0;
    bool renameStart = false;
    std::string renameText;

    ItemId pressItem = 0;     // 左ボタンで押した行。まだ離していない
    float pressX = 0, pressY = 0;
    bool pressDeferred = false;  // 選択中の行を押した: 離したときにそれだけを選ぶ (ドラッグのあとではなく)
    bool dragging = false;
    std::vector<ItemId> moving;

    bool hasKeys = false;     // 最後のクリックがこのツリーだった
    w::Popup menu;
    std::vector<Action> menuActions;
    ItemId scrollTo = 0;      // 次の Draw でこの行を見える所に保つ
};

// 次の Draw で `ids` を選ぶ (rename 付きなら、最初のものの名前変更を始める) ようツリーに頼む。
void RequestSelect(State& st, std::vector<ItemId> ids, bool rename = false);

// プロジェクトを閉じるときにツリーから残すもの (UserSettings/Layout.json に、レイアウトと一緒に -
// ユーザーの見え方であって、プロジェクトのデータではない): { "collapsed": [ids], "selected": [ids] }。
// 読み込みは選択を頼む (次の Draw で適用。無くなった ID はそこで捨て、無くなった項目の閉じた ID は
// Draw が忘れる)。
json::Value SaveState(const State& st);
void LoadState(State& st, const json::Value& v);

void Draw(ui::Context& c, State& st, Source& src, const ui::Rect& area);

}  // namespace rm::editor::tree
