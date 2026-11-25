#pragma once
// インスペクター: リフレクションしたオブジェクトのフィールドを、その TypeDesc から描く。型ごとの
// インスペクターのコードはない - プラグインは自分の構造体を書くだけ (設計 RME_PLATFORM 4-c:
// 「核は見せ、決めるのはソフト」)。
//
// ブロックの一覧を見せる (選んだものが持つ「値のかたまり」 - ゲームのエンティティなら、
// エンティティ自身と各部品)。核はブロックの意味を知らない: ブロックにはタイトル、省略できる
// メニュー (⋮: たとえば 外す)、フィールドがある。ブロックの下に「追加」ボタンを置け、
// そのメニューはソフトが埋める。「部品」という言葉はゲームのもので、核のものではない。
//
// オブジェクトには絶対に書き込まない。変更は Edit (ブロックの番号つき) として返り、呼んだ側が
// それをソフトの履歴の reflect::SetFieldCommand にする - だから編集はすべて Undo でき、
// プロジェクトは未保存になる (憲法 7)。テキストと数の欄は、キーを 1 つ打つたびに Edit を送る
// (打つと値が変わる)。欄にフォーカスがある間は全部同じ `merge` キーなので、履歴には打った
// 全体で 1 つの項目が残る。Esc は欄に入る前の値を送る (その項目は消える)。欄をクリックすると
// 全部を選び、ドラッグすると覆った分を選ぶ。数は reflect::NumberFromInput を通る: ファイルを
// 読み込むときと同じ Min / Max / 丸め (設計 R1 T3) - まだ数でない文字 ("-"、"1e") は待つ。
//
// 数のラベル (またはベクトルの欄の前の X / Y / Z、配列の要素の番号) を左右にドラッグすると、
// 終わりのないスライダーのように変わる (Shift: 速く、Alt: ゆっくり。ドラッグごとに Undo は 1 つ)。
//
//   bool           チェックボックス      int / float     数の欄
//   std::string    文字の欄              Vec2/3, Color   成分ごとに数の欄 1 つ (+ 色見本)
//   enum           ドロップダウンリスト (このバージョンが知らない名前は "名前 (不明)" と出る)
//   struct         そのフィールド (インデントする)
//   AssetRef       ファイルの名前 (+ 絵)。アセットパネルからドラッグすると入る (AssetKind= の
//   EntityRef      種類だけ)。クリックすると選ぶリストが開く (検索欄つき)、× で消す。
//                  エンティティ: 同じように 階層 の項目。もう無い ID は
//                  "(見つかりません)" と出て、残る。何があるかは Refs (エディターのもの) から来る。
//   (文字で、Block::choices が選択肢を返すもの: ドロップダウンリスト。一覧に無い値は "値 (見つかりません)"
//                  と出て、残る。選択肢はソフトが今のデータから作る - アニメーターのクリップの名前)
//   std::vector    "N 個" と ＋ (最後のコピーを足す)、そして各要素: ドラッグして別の場所へ
//                  動かすつまみ、番号、値 (上のどれか)、消す ×。変更は毎回配列全体を
//                  いっぺんに設定する (Undo は 1 つの項目)。

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "editor/widgets/Widgets.h"
#include "ui/Context.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rm::editor::inspector {

struct Edit {
    size_t block = 0;
    reflect::FieldPath path;
    json::Value before, after;
    std::string label;  // Undo 履歴用、たとえば 「目標 FPS」を変更
    uint64_t merge = 0;  // != 0: 1 つの欄に打っている - 同じキーの編集は Undo の 1 つの項目
    int component = -1;  // Vec2 / Vec3 / Color のフィールド: 打った数 1 つ (-1: 値全体)
};

// 参照フィールドが見せるものと出すもの (インスペクターはアセットも 階層 も知らない)。
struct RefChoice {
    std::string value;    // 保存する形: アセットの ID / 項目の ID (10 進)
    std::wstring name;
    std::wstring detail;  // 場所 (そのフォルダ / その親)。薄く表示
    w::Icon icon = w::Icon::File;
    std::string kind;     // アセットの種類 ("image"、"model" ... core/asset KindKey)
    std::string file;     // Assets/ にあるアセットのパス (拡張子の AssetKind、".rmprefab" がこれを見る)
};
struct Refs {
    std::function<std::optional<RefChoice>(std::string_view guid)> asset;  // nullopt: プロジェクトにない
    std::function<std::vector<RefChoice>()> assets;                         // 全ファイルとフォルダ
    std::function<std::optional<RefChoice>(uint64_t id)> item;             // 階層 の項目
    std::function<std::vector<RefChoice>()> items;
    // アセットの小さな絵 (アセットパネルのサムネイル)、`size` DIP。null: (まだ) なし。
    std::function<ID2D1Bitmap*(ui::Context& c, const std::string& guid, float size)> thumbnail;
};

// 文字のフィールドの選択肢 1 つ (Block::choices): 保存する値と、見せる名前。
struct Choice {
    std::string value;
    std::wstring label;
};

// 値のかたまり 1 つ。
struct Block {
    const reflect::TypeDesc* type = nullptr;  // null: `note` だけ出す (たとえばプラグインが無い部品)
    void* object = nullptr;
    const reflect::Unknowns* unknowns = nullptr;
    std::wstring title;           // 見出し (クリックするとブロックをたたむ)。空 = 見出しなし
    std::wstring note;            // 見出しの下の 1 行
    std::vector<w::Action> menu;  // 見出しの ⋮ メニュー。空 = ボタンなし
    // ブロックの見せ方 (たとえば ローカル / ワールド): フィールドの上のスイッチ。空 = なし。
    // 選ぶと setMode を呼ぶ - 呼んだ側がその見せ方のブロックを渡す。
    std::vector<std::wstring> modes;
    int mode = 0;
    std::function<void(int)> setMode;
    // `object` といっしょに編集する、同じ型の別のオブジェクト (複数を選んだとき): 全部で
    // 同じではない値は "—" と出す。Edit は `object` のもの。呼んだ側がこれらにも
    // 適用する (ベクトルなら、その `component` だけ)。
    std::vector<const void*> others;
    // 表示だけで編集しない (Play 中のゲームは動いているコピーを見せる): フィールドは入力を受けない。
    bool readOnly = false;
    // フィールドの行を右クリック (表示だけのブロックでも): そのフィールド用のこのメニュー
    // (たとえば Play の この値を残す)。null / 空: メニューなし。
    std::function<std::vector<w::Action>(const reflect::FieldPath& path, std::string_view label)> fieldMenu;
    // この型のこれらのフィールドだけ (名前で。大きな型をタブで見せるウィンドウ用)。空: 全部。
    std::vector<std::string> fields;
    // いっぺんに設定するできあいの値 (天候の 快晴 / 雨 / 嵐 ...): フィールドの上に 1 行 - 名前と
    // これらのドロップダウン。選んだ行が動く (呼んだ側が履歴に積む)。空: 行なし。
    std::wstring presetLabel;
    std::vector<w::Action> presets;
    // 文字のフィールド (配列の要素でないもの) の選択肢: `path` のフィールドに値を返すと、文字の欄の
    // 代わりにそれらのドロップダウンを出す。nullopt / null: ふつうの文字の欄。描くたびに尋ねる。
    std::function<std::optional<std::vector<Choice>>(const reflect::FieldPath& path)> choices;
};

// インスペクターごと: 打っている最中の文字、開いているドロップダウン / メニュー、たたんだブロック。
struct State {
    std::unordered_map<ui::Id, std::string> text;
    ui::Id editing = 0;
    std::string original;  // その欄に入ったときの、編集中の欄の値 (Esc)
    uint64_t session = 0;  // その入力のマージキー
    w::Popup popup;  // enum フィールドのドロップダウンリスト、または参照フィールドのリスト
    ui::Id popupId = 0;  // ... このフィールド (か配列の要素) の
    std::string pickSearch;  // 参照リストの検索欄に打ったもの
    // ラベルをドラッグして数を変える: どれか、どの値から、どれだけ、その Undo キー。
    ui::Id scrub = 0;
    double scrubStart = 0, scrubMoved = 0, scrubSpeed = 0;
    uint64_t scrubSession = 0;
    // つまみで配列の要素をドラッグする: どの配列の、どの要素か。
    ui::Id arrayDrag = 0;
    size_t arrayDragFrom = 0;
    std::optional<Edit> pending;  // ドロップダウンで選んだもの (フレームの終わりに描く)
    w::Popup menu;                // ブロックの ⋮ メニュー
    std::vector<w::Action> menuActions;
    // 追加ボタンのパネル (w::ActionPicker): × / 検索 / フォルダに入った選択肢。開いたフォルダは
    // 次に開くときまで残る (ほかのものを表示したときも: EditorScreen の Inspect)。
    w::Popup addPopup;
    std::vector<w::Action> addActions;
    w::PickerState addPicker;
    std::unordered_set<std::wstring> folded;  // ブロックのタイトル
    float contentHeight = 0;
};

// ブロックを `area` に描く (必要ならスクロールする)。`addLabel` が空でなければ、その下に
// `addMenu` が返すものを並べるパネルを開くボタンを置く (フォルダに入れて: w::Action::folder)。
// 終わった変更があれば返す。
// `refs`: 参照フィールドが見せられるもの (null: "(なし)" / "(見つかりません)" だけ出す)。
std::optional<Edit> Draw(ui::Context& c, State& st, const ui::Rect& area, std::span<const Block> blocks,
                         std::wstring_view addLabel = {}, const std::function<std::vector<w::Action>()>& addMenu = {},
                         const Refs* refs = nullptr);

}  // namespace rm::editor::inspector
