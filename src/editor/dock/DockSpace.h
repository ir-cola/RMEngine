#pragma once
// ウィンドウにドッキングの木を描き、ユーザーが並べ替えられるようにする (エディター専用)。
//
//   タブ列         クリック = 表示、ドラッグ = 移動、中クリック / x = 閉じる、右クリック = メニュー。
//                  収まらないタブ: ホイールか < > で行を動かし、v で全タブを一覧する。表示中の
//                  タブは必ず見える所に動かす。行の端の近くへタブをドラッグしても動く。
//   分割線         ドラッグ = 大きさを変える (比率を保つので、パネルはウィンドウに合わせて伸縮する)
//   ドロップガイド ドラッグ中、カーソルの下のパネル (とウィンドウの端) にガイドが出る。
//                  ガイドの上で離すとそこにドックする。タブ列の上ならタブになる。
//                  それ以外ならパネルは自分のウィンドウに出ていく。
//
// 1 つの DockSpace がエディターの全ウィンドウを受け持つ: エディターウィンドウ (site 0) と、
// 各浮動ウィンドウ (site = その浮動 ID)。あるウィンドウで始めたドラッグは別のウィンドウで
// 終わることがあるので、ドロップ先は画面上の位置とその下にあるウィンドウから探す。
// レイアウトはフレームの終わりにだけ編集する (ノードを描いている最中には絶対にしない)。

#include "editor/dock/DockLayout.h"
#include "editor/widgets/Widgets.h"
#include "platform/Win.h"
#include "ui/Context.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::editor::dock {

struct PanelType {
    std::string id;       // "core.console" - 保存されるレイアウトのキーでもある
    std::wstring title;   // タブの表示名
    std::function<void(ui::Context&, const ui::Rect&)> draw;
};

class DockSpace {
public:
    // 木の構造が変わった後 (パネルの移動・開く・閉じる、ウィンドウの追加や削除) に呼ぶ。
    // 持ち主はレイアウトに合わせて浮動ウィンドウを作ったり壊したりする。
    std::function<void()> onStructureChanged;
    // ドックを表示しているウィンドウを全部再描画する (ドラッグ中は別のウィンドウにガイドが出る)。
    std::function<void()> onRedrawAll;

    void SetPanelTypes(std::vector<PanelType> types) { types_ = std::move(types); }
    const std::vector<PanelType>& PanelTypes() const { return types_; }
    const PanelType* FindPanel(std::string_view id) const;
    std::vector<std::string> PanelIds() const;

    Layout& GetLayout() { return layout_; }
    const Layout& GetLayout() const { return layout_; }
    void SetLayout(Layout layout);

    // --- ウィンドウごと ---
    void Draw(ui::Context& c, const ui::Rect& area, uint32_t site, HWND hwnd, float scale);
    // ドロップガイド、ドラッグ中のタブ、タブのメニュー。最後に、入力を有効にして描く。
    void DrawOverlay(ui::Context& c, uint32_t site);
    void ForgetSite(uint32_t site);

    // --- タイトルバーで動かす浮動ウィンドウ (WM_MOVING / WM_EXITSIZEMOVE) ---
    void WindowMoving(uint32_t floatingId, HWND hwnd);
    void WindowMoveEnded(uint32_t floatingId);

    // --- コマンド (メニュー) ---
    void TogglePanel(std::string_view id);
    void FloatPanel(std::string_view id);  // 今ある場所の隣の新しいウィンドウへ
    void CloseFloating(uint32_t floatingId);  // 浮動ウィンドウの X: 中のパネルを閉じる
    bool IsDragging() const { return drag_.kind == DragKind::Tab || drag_.kind == DragKind::Window; }
    // 浮動ウィンドウのタイトル: 中のパネルの名前。
    std::wstring FloatingTitle(uint32_t floatingId) const;

private:
    enum class DragKind { None, Pending, Tab, Window };

    struct LeafGeo {
        uint32_t id = 0;
        ui::Rect rect;   // 葉全体
        ui::Rect strip;  // タブ列
        ui::Rect view;   // タブを表示する部分 (< > v のボタンを除く)
        std::vector<float> widths;  // 各タブの幅
        bool overflow = false;
    };
    // 葉のタブの行 (葉 ID ごと。保存しない)。
    struct StripState {
        float scroll = 0;
        std::string shown;  // 前のフレームで手前にあったタブ: 新しくなったら見える所に動かす
        size_t count = 0;
        // 各タブの今見えている左端 (行の始まりから、パネル ID ごと)。目標の場所へ滑らかに動く
        // (並べ替えのドラッグ中によける・離したあと収まる・閉じたタブの後ろが詰まる)。
        std::unordered_map<std::string, float> slide;
        double time = 0;  // 前に描いた時刻 (秒)
    };
    struct Site {
        HWND hwnd = nullptr;
        float scale = 1.f;
        ui::Rect area;
        uint32_t rootId = 0;
        bool rootIsLeaf = true;
        std::vector<LeafGeo> leaves;
    };
    struct Guide {
        ui::Rect rect;
        uint32_t node = 0;
        Zone zone = Zone::Center;
        bool leaf = true;
        ui::Rect target;  // 対象ノードの矩形 (プレビュー用)
    };
    struct Target {
        bool valid = false;
        uint32_t site = 0;
        uint32_t node = 0;
        Zone zone = Zone::Center;
        int tabIndex = -1;
        ui::Rect preview;  // 対象ウィンドウでの DIP
        // 自分の葉のタブの行の中: 並べ替え。moveTo = ドラッグ中のタブを抜いた並びで入る番号 (TabMoveIndex)。
        bool reorder = false;
        int moveTo = -1;
    };
    struct Drag {
        DragKind kind = DragKind::None;
        std::string panel;      // タブ
        uint32_t floating = 0;  // ウィンドウ
        uint32_t fromSite = 0;
        float startX = 0, startY = 0;
        float grabX = 0;  // タブのどこをつかんだか (タブの左端からの DIP)
        ui::Rect fromLeaf;
        // ホバー (画面上のカーソル位置から更新する)
        uint32_t hoverSite = ~0u;
        float hoverX = 0, hoverY = 0;  // ホバー中のウィンドウでの DIP
        uint32_t hoverLeaf = 0;        // カーソルの下の葉 (そのガイドを出す)
        Target target;
        // 収まらないタブの行を動かす。タブを行の端の近くに持っている間
        POINT lastCursor{};
        double lastScrollTime = 0;
    };
    struct TabMenu {
        w::Popup popup;
        std::string panel;
    };
    struct TabList {  // v メニュー: 1 つの葉の全タブ
        w::Popup popup;
        uint32_t leaf = 0;
    };

    const Node* RootOf(uint32_t site) const;
    void DrawLeaf(ui::Context& c, const Node& leaf, const ui::Rect& r, uint32_t site, LeafGeo& geo);
    void DrawTabStrip(ui::Context& c, const Node& leaf, const ui::Rect& strip, uint32_t site, LeafGeo& geo);
    void DrawTabList(ui::Context& c, uint32_t site);
    void DrawSplitter(ui::Context& c, const Node& split, const ui::Rect& r);
    std::vector<Guide> GuidesAt(const Site& site, float x, float y) const;
    void UpdateHover(HWND exclude);
    void ScrollWhileDragging(const LeafGeo& leaf, float x, POINT cursor);
    bool MenuOpen(uint32_t site) const;
    void FinishTabDrag();
    void CancelDrag();
    void Post(std::function<void()> fn) { post_.push_back(std::move(fn)); }
    void Changed();
    ScreenRect FloatRectNear(const Site& site, const ui::Rect& leafRect, POINT at) const;
    uint32_t LargestMainLeaf() const;

    std::vector<PanelType> types_;
    Layout layout_;
    std::unordered_map<uint32_t, Site> sites_;
    std::unordered_map<uint32_t, TabMenu> menus_;
    std::unordered_map<uint32_t, TabList> tabLists_;
    std::unordered_map<uint32_t, StripState> strips_;
    Drag drag_;
    std::vector<std::function<void()>> post_;
};

}  // namespace rm::editor::dock
