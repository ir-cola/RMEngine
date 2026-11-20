#pragma once
// パネルがどこにあるか: ドッキングのツリー (純粋 - ウィンドウも描画もなし。RMTests でテストする)。
//
//   Layout
//     main       エディターのウィンドウの中のツリー (null にはならない。空の葉 1 つのこともある)
//     floating   エディターから出して、それぞれ自分の OS ウィンドウに入れたパネル。ツリーが 1 つずつ
//
//   Node = 分割 (2 つの子が横に並ぶか縦に重なる。比率がある)
//        | 葉  (パネル id のタブの積み重ね。そのうち 1 つを出す)
//
// どのパネル id も、配置全体で多くて 1 回しか現れない。入っていないパネルは閉じている。
// 配置はユーザーから見たエディターの見え方で、プロジェクトのデータではない: Undo はできず、
// プロジェクトを「未保存」にもしない (プロジェクトの隣、UserSettings/ に保存する)。

#include "base/Json.h"
#include "ui/Types.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::dock {

// Horizontal: a | b (横に並ぶ)。 Vertical: a の下に b。
enum class Axis : uint8_t { Horizontal, Vertical };
// ドラッグしたパネルが、ターゲットのノードに対してどこへ行くか。
enum class Zone : uint8_t { Center, Left, Right, Top, Bottom };

struct Node {
    uint32_t id = 0;  // エディターが動いている間、ノードを見分ける (保存しない)

    // 分割 (a と b があるとき)。ratio = a の割合、0..1。
    Axis axis = Axis::Horizontal;
    float ratio = 0.5f;
    std::unique_ptr<Node> a, b;

    // 葉: タブの積み重ね。
    std::vector<std::string> tabs;
    int active = 0;

    bool IsLeaf() const { return !a; }
    bool IsEmptyLeaf() const { return IsLeaf() && tabs.empty(); }
};

struct ScreenRect {  // 仮想画面の物理ピクセルでのウィンドウの矩形
    int x = 0, y = 0, w = 0, h = 0;
};

struct Floating {
    uint32_t id = 0;
    ScreenRect rect;
    std::unique_ptr<Node> root;
};

class Layout {
public:
    Layout();
    Layout(Layout&&) noexcept = default;
    Layout& operator=(Layout&&) noexcept = default;
    Layout Clone() const;

    std::unique_ptr<Node> main;
    std::vector<Floating> floating;

    // --- 作る (既定の配置) ---
    std::unique_ptr<Node> MakeLeaf(std::vector<std::string> tabs, int active = 0);
    std::unique_ptr<Node> MakeSplit(Axis axis, float ratio, std::unique_ptr<Node> a, std::unique_ptr<Node> b);

    // --- 問い合わせ ---
    bool Contains(std::string_view panel) const;
    // そのパネルを持つ葉。メインのツリーにあるときは floatingId = 0。
    const Node* LeafOf(std::string_view panel, uint32_t* floatingId = nullptr) const;
    Node* FindNode(uint32_t id);
    const Node* FindNode(uint32_t id) const;
    // 0 = メインのツリー。そうでなければ、そのノードを持つ浮かせたウィンドウの id。~0u = どこにもない。
    uint32_t SiteOf(uint32_t nodeId) const;
    Floating* FindFloating(uint32_t id);
    std::vector<std::string> Panels() const;  // 開いている全パネル (ツリーの順)

    // --- 編集。どれも、配置が変わったとき true を返す。 ---
    bool Activate(std::string_view panel);
    bool Close(std::string_view panel);
    // 閉じているパネルを、指定した葉のタブとして開く (葉がなければ、メインのツリーの最初の葉)。
    // 開いているパネルは前面に出すだけ。
    bool Open(std::string_view panel, uint32_t leafId);
    // パネルをターゲットのノードの横 / 中へ動かす。Center は葉のターゲットが要る。tabIndex = そのタブの
    // 帯の中での位置 (-1 = 最後)。端はターゲットを分割する (ターゲットが分割なら、グループ全体を包む)。
    bool Dock(std::string_view panel, uint32_t targetId, Zone zone, int tabIndex = -1);
    // 浮かせたウィンドウを丸ごと (そのパネルすべて) 戻す。
    bool DockFloating(uint32_t floatingId, uint32_t targetId, Zone zone);
    // パネルを、rect の位置に新しい浮かせたウィンドウとして出す。その id を返す (0 = 何もしなかった)。
    // すでに浮かせたウィンドウに 1 つだけで入っているパネルは、そのウィンドウを動かすだけ。
    uint32_t Float(std::string_view panel, ScreenRect rect);
    // 浮かせたウィンドウと、その中のパネルをすべて閉じる。
    bool CloseFloating(uint32_t floatingId);

    // 知らないパネルと重複したパネルを消し、比率と開いているタブを直し、空の葉とウィンドウを捨てる。
    // 読み込みの後に走らせる: 保存した配置は、なくなったプラグインのパネルを名指しているかもしれない。
    void Sanitize(const std::vector<std::string>& knownPanels);

    // --- 保存の形式 ---
    json::Value ToJson() const;
    static std::optional<Layout> FromJson(const json::Value& v);

private:
    uint32_t NewId() { return nextId_++; }
    std::unique_ptr<Node>* FindSlot(uint32_t id);
    // パネルをその葉から、刈らずに外す (ターゲットのノードは、挿入までは残さないといけない)。
    std::unique_ptr<Node> Extract(std::string_view panel, uint32_t* fromLeaf, int* fromIndex);
    bool Insert(std::unique_ptr<Node> sub, uint32_t targetId, Zone zone, int tabIndex);
    void Prune();
    void Renumber(Node& n);

    uint32_t nextId_ = 1;
};

// --- 幾何 (純粋) -------------------------------------------------------------------------

struct Placed {
    const Node* node = nullptr;
    ui::Rect rect;
};

// ツリーのすべてのノードの矩形。親が子より先。gap = 分割線の太さ。
void Arrange(const Node& root, const ui::Rect& area, float gap, std::vector<Placed>& out);
// r に置いた分割の 2 つの子の矩形。
void SplitChildren(const Node& split, const ui::Rect& r, float gap, ui::Rect& a, ui::Rect& b);
// r に置いた分割の分割線。
ui::Rect SplitterRect(const Node& split, const ui::Rect& r, float gap);
// 分割線をマウスの下に置く比率。両側が少なくとも minSize を保つようにする。
float RatioAt(const Node& split, const ui::Rect& r, float gap, float mouse, float minSize);
// target に zone でパネルをドロップしたとき、パネルがどこに収まるか (プレビュー用)。
ui::Rect ZonePreview(const ui::Rect& target, Zone zone, bool targetIsLeaf);
// 端へのドロップがノードを分割するとき、新しい部分が受け取る割合。
float EdgeShare(bool targetIsLeaf);

// --- タブの帯: 帯より長くなることがあるタブの 1 行 (純粋) ----------------------
//
//   | pad | tab0 | spacing | tab1 | ... |  < > v | pad |
//         '-------- view (scroll で動く) -------'  ボタンはタブが収まらない間だけ
//
// 位置は行の始まりから測る (scroll = 0 のとき tab0 の左)。
struct TabStripFit {
    float content = 0;      // すべてのタブを、間の spacing も入れて横に並べた長さ
    float view = 0;         // タブを見せる幅
    float scroll = 0;       // 行が左へどれだけ動いたか (0 .. content - view の範囲に保つ)
    bool overflow = false;  // タブが収まらない: ボタンが右に場所を取る
};
// available = 帯の幅からパディングを引いた物。buttons = < > v ボタンに要る場所。
TabStripFit FitTabStrip(std::span<const float> widths, float spacing, float available, float buttons, float scroll);
float TabLeft(std::span<const float> widths, float spacing, size_t index);
float ClampTabScroll(float content, float view, float scroll);
// タブ `index` を丸ごと見せるスクロール。動きは最小限にする (ビューより広いタブは左側を見せる)。
float ScrollToShowTab(std::span<const float> widths, float spacing, float view, float scroll, size_t index);
// < (dir -1) または > (dir +1) を 1 回押す: その側で切れている、または隠れている次のタブを
// 丸ごと見える所へ持ってくる。
float StepTabScroll(std::span<const float> widths, float spacing, float view, float scroll, int dir);
// x (行の始まりからの相対、scroll は足し済み) にドロップしたタブが入る場所。
int TabInsertIndex(std::span<const float> widths, float spacing, float x);

}  // namespace rm::editor::dock
