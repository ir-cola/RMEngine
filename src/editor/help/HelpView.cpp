#include "editor/help/HelpView.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Image.h"
#include "platform/Shell.h"

#include <algorithm>
#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

using ui::Rect;

namespace {

constexpr float kTabH = 28.f, kTabRadius = 6.f, kSheetRadius = 6.f;
constexpr float kTreeW = 230.f, kPageMinW = 320.f;  // これより狭いと木を隠す (≡ で出す)
constexpr double kHideDelay = 0.3;                   // 出した木からカーソルが外れて、消えるまで

// フォルダーのページが無いときに出す、子の一覧。
help::Doc ContentsOf(const help::Node& n) {
    help::Doc d;
    d.title = n.title;
    help::Block h;
    h.kind = help::Block::Kind::Heading;
    h.level = 1;
    h.spans.push_back({help::Span::Kind::Text, n.title, {}});
    d.blocks.push_back(std::move(h));
    for (const help::Node& child : n.children) {
        help::Block b;
        b.kind = help::Block::Kind::Bullet;
        b.spans.push_back({help::Span::Kind::Link, child.title, "/" + child.id});
        d.blocks.push_back(std::move(b));
    }
    if (n.children.empty()) {
        help::Block b;
        b.spans.push_back({help::Span::Kind::Text, Tr(L"このページにはまだ説明がありません。"), {}});
        d.blocks.push_back(std::move(b));
    }
    return d;
}

// 木の中で `id` の親たちの ID (上から)。
bool PathTo(const std::vector<help::Node>& nodes, const std::string& id, std::vector<std::string>& path) {
    for (const help::Node& n : nodes) {
        if (n.id == id) return true;
        path.push_back(n.id);
        if (PathTo(n.children, id, path)) return true;
        path.pop_back();
    }
    return false;
}

}  // namespace

// --- 木に出すもの --------------------------------------------------------------------------------

class HelpView::Source final : public tree::Source {
public:
    Source(HelpView& view, int tab) : view_(view), tab_(tab) {}

    void Items(std::vector<tree::Item>& out) override {
        ids_.clear();
        for (const help::Node& n : view_.Roots(tab_)) Add(n, 0, out);
    }
    std::wstring EmptyText() const override { return Tr(L"ヘルプのページがありません。"); }
    void SelectionChanged(std::span<const tree::ItemId> ids) override {
        if (ids.empty()) return;
        if (const auto it = ids_.find(ids.front()); it != ids_.end()) view_.Select(tab_, it->second);
    }

private:
    void Add(const help::Node& n, tree::ItemId parent, std::vector<tree::Item>& out) {
        tree::Item item;
        item.id = help::ItemIdOf(n.id);
        item.parent = parent;
        item.name = WideToUtf8(n.title);
        item.canHaveChildren = !n.children.empty();
        item.icon = n.children.empty() ? tree::Icon::Text : tree::Icon::Folder;
        out.push_back(std::move(item));
        ids_[out.back().id] = n.id;
        for (const help::Node& c : n.children) Add(c, help::ItemIdOf(n.id), out);
    }

    HelpView& view_;
    int tab_;
    std::unordered_map<tree::ItemId, std::string> ids_;
};

HelpView::HelpView() {
    sources_[0] = std::make_unique<Source>(*this, 0);
    sources_[1] = std::make_unique<Source>(*this, 1);
}

HelpView::~HelpView() = default;

void HelpView::SetLibrary(help::Library lib, bool english) {
    lib_ = std::move(lib);
    english_ = english;
    images_.clear();
    // 見ていたページを読み直す (言語が変わった・ファイルが変わった)。無くなっていたら、そのタブは最初から。
    for (int t = 0; t < 2; ++t) {
        if (!page_[t].empty() && !help::FindNode(Roots(t), page_[t])) page_[t].clear();
    }
    docId_.clear();
    doc_.reset();
}

bool HelpView::Open(const std::string& pageId) {
    for (int t = 0; t < 2; ++t) {
        std::vector<std::string> path;
        if (!PathTo(Roots(t), pageId, path)) continue;
        tab_ = t;
        for (const std::string& p : path) trees_[t].collapsed.erase(help::ItemIdOf(p));  // (そこまで開く)
        trees_[t].filter.clear();
        tree::RequestSelect(trees_[t], {help::ItemIdOf(pageId)});
        trees_[t].scrollTo = help::ItemIdOf(pageId);
        page_[t] = pageId;
        narrowOpen_ = false;
        return true;
    }
    return false;
}

void HelpView::Select(int tab, const std::string& pageId) {
    page_[tab] = pageId;
    narrowOpen_ = false;  // (細いとき: ページを選んだら木は引っ込む)
}

void HelpView::LoadDoc(const std::string& pageId) {
    docId_ = pageId;
    docError_.clear();
    doc_.reset();
    if (pageId.empty()) return;
    if (lib_.files.contains(pageId)) {
        doc_ = help::LoadPage(lib_, pageId, english_, &docError_);
        if (!doc_) log::Warn("ヘルプのページを読めませんでした: {} ({})", pageId, docError_);
        return;
    }
    // ページの無いフォルダー・本: 子の一覧。
    if (const help::Node* n = help::FindNode(Roots(tab_), pageId)) doc_ = ContentsOf(*n);
}

w::DocImage HelpView::ImageOf(ui::Context& c, const std::string& path) {
    if (!gfx_ || path.empty() || docId_.empty()) return {};
    if (gfx_->Generation() != imagesMadeIn_) {
        images_.clear();
        imagesMadeIn_ = gfx_->Generation();
    }
    const fs::path file = (help::PageFolder(lib_, docId_, english_) / PathFromUtf8(path)).lexically_normal();
    auto it = images_.find(file);
    if (it == images_.end()) {
        Image img;
        if (auto bytes = platform::ReadFileBytes(file, nullptr)) {
            const auto* data = reinterpret_cast<const uint8_t*>(bytes->data());
            std::string err;
            img.bitmap = platform::LoadBitmapFromMemory(*gfx_, c.P().DC(), {data, bytes->size()}, 1600, &err);
            if (img.bitmap) {
                const auto size = img.bitmap->GetSize();
                img.width = size.width;
                img.height = size.height;
            }
        }
        it = images_.emplace(file, std::move(img)).first;  // (読めなかったものも覚える: 毎フレーム読み直さない)
    }
    return {it->second.bitmap.Get(), it->second.width, it->second.height};
}

// --- 描く ----------------------------------------------------------------------------------------

void HelpView::Draw(ui::Context& c, const Rect& r) {
    auto& p = c.P();
    // 上のフォルダーのタブ 2 つ (デバッグの窓と同じ部品)。選んでいないタブ、シート、選んだタブの順。
    const std::wstring titles[2] = {Tr(L"Hub とエディター本体"), Tr(L"プラグイン")};
    Rect tabs[2];
    float tx = r.x + 4;  // (最初のタブはシートの左の角の上: 選んだときはシートの角を丸めない)
    for (int t = 0; t < 2; ++t) {
        const float tw = p.TextWidth(titles[t], 12.5f) + 32;
        tabs[t] = {tx, r.y + 4, tw, kTabH};
        tx += tw - 2;
    }
    const Rect sheet{r.x + 4, r.y + 4 + kTabH, r.w - 8, r.h - kTabH - 8};
    int chosen = tab_;
    for (int t = 0; t < 2; ++t) {
        if (t != tab_ && w::FolderTab(c, std::format("help.tab{}", t), tabs[t], titles[t], false, kTabRadius)) chosen = t;
    }
    w::DrawFolderSheet(c, sheet, kSheetRadius, tab_ == 0);
    w::FolderTab(c, std::format("help.tab{}", tab_), tabs[tab_], titles[tab_], true, kTabRadius);
    if (chosen != tab_) {
        tab_ = chosen;
        narrowOpen_ = false;
    }

    // 見るページ: 選んだもの、無ければそのタブの最初 (本体: 核の一番上のページ)。
    std::string want = page_[tab_];
    if (want.empty()) {
        if (tab_ == 0 && lib_.corePage) {
            want = *lib_.corePage;
        } else if (!Roots(tab_).empty()) {
            want = Roots(tab_).front().id;
        }
    }
    if (want != docId_) LoadDoc(want);

    const Rect inner = sheet.Inset(6);
    const bool narrow = inner.w < kTreeW + kPageMinW;
    if (!narrow) {
        narrowOpen_ = false;
        const Rect treeR{inner.x, inner.y, kTreeW, inner.h};
        DrawTree(c, treeR);
        p.Line(treeR.Right() + 3, inner.y + 4, treeR.Right() + 3, inner.Bottom() - 4, theme::Border, 1.f);
        DrawPage(c, {treeR.Right() + 8, inner.y, inner.w - kTreeW - 8, inner.h});
        return;
    }

    // 細い: 本文だけ。左上の ≡ で木を出す (カーソルが外れて少しすると消える)。
    const Rect menuButton{inner.x + 2, inner.y + 2, 26, 26};
    const Rect pop{inner.x, menuButton.Bottom() + 4, std::min(280.f, inner.w - 8), inner.h - menuButton.h - 6};
    const ui::Input& in = c.In();
    const bool overPop = narrowOpen_ && (pop.Contains(in.mouseX, in.mouseY) || menuButton.Contains(in.mouseX, in.mouseY));
    // 出している木の下の本文は、押されても何もしない。
    const bool input = c.InputEnabled();
    if (overPop) c.SetInputEnabled(false);
    DrawPage(c, {inner.x, menuButton.Bottom() + 4, inner.w, inner.h - menuButton.h - 6});
    c.SetInputEnabled(input);
    if (w::IconButton(c, "help.menu", menuButton, w::Icon::Menu)) {
        narrowOpen_ = !narrowOpen_;
        leftAt_ = -1;
    }
    ui::TextStyle ts;
    ts.size = 12.5f;
    ts.color = theme::TextDim;
    p.Text({menuButton.Right() + 6, menuButton.y, inner.w - menuButton.w - 10, menuButton.h}, doc_ ? doc_->title : std::wstring(), ts);
    if (!narrowOpen_) return;
    w::DrawSurface(c, pop, w::Surface::Popup);
    DrawTree(c, pop.Inset(4));
    // 消すのは: Esc・ページを選んだ (Select)・カーソルが外れて少し経った。ボタンを押している間
    // (スクロールバーを掴んではみ出した) と、検索の欄に打っている間は消さない。
    const bool holding = in.down[0] || in.down[1] || in.down[2];
    const bool typing = c.Focused() != 0;
    if (c.KeyPressed(ui::Key::Escape)) {
        narrowOpen_ = false;
    } else if (overPop || holding || typing) {
        leftAt_ = -1;
    } else {
        const double now = c.Time();
        if (leftAt_ < 0) leftAt_ = now;
        if (now - leftAt_ >= kHideDelay) {
            narrowOpen_ = false;
        } else {
            c.RequestAnimation();  // (待っている間もフレームを回す)
        }
    }
}

void HelpView::DrawTree(ui::Context& c, const Rect& r) {
    c.PushId(static_cast<uint64_t>(tab_));
    tree::Draw(c, trees_[tab_], *sources_[tab_], r);
    c.PopId();
}

void HelpView::DrawPage(ui::Context& c, const Rect& r) {
    auto& p = c.P();
    if (!doc_) {
        ui::TextStyle ts;
        ts.size = 13;
        ts.color = theme::TextDim;
        ts.wrap = true;
        ts.v = ui::VAlign::Top;
        const std::wstring why = docError_.empty() ? Tr(L"左の一覧からページを選んでください。") : TrF(L"ページを読めませんでした: {}", Utf8ToWide(docError_));
        p.Text(r.Inset(16), why, ts);
        return;
    }
    const std::string scrollId = "help.page." + docId_;  // (ページごとに、上から)
    float& contentH = c.FloatState(c.MakeId(scrollId + ".h"), 400.f);
    const Rect view = r.Inset(0, 2, 0, 2);
    const float off = w::BeginScroll(c, scrollId, view, contentH);
    std::string clicked;
    const float x = view.x + 16, width = view.w - 40;
    const float h = w::DocView(c, scrollId, x, view.y + 12 - off, width, view, *doc_,
                               [&](const std::string& path) { return ImageOf(c, path); }, &clicked);
    contentH = h + 28;
    w::EndScroll(c, scrollId, view, contentH);
    if (clicked.empty()) return;
    // リンク: Web のページはブラウザーで、それ以外はヘルプの中で開く。
    const auto files = lib_.files.find(docId_);
    const std::string book = files != lib_.files.end() ? files->second.book : docId_;
    const std::string folder = files != lib_.files.end() ? files->second.folder : docId_;
    const std::string target = help::ResolveLink(book, folder, clicked);
    if (help::IsWebLink(target)) {
        platform::OpenWebPage(Utf8ToWide(target));
    } else if (target.empty() || !Open(target)) {
        log::Warn("ヘルプのリンク先が見つかりません: {} ({})", clicked, docId_);
    }
}

// --- 見え方を残す --------------------------------------------------------------------------------

json::Value HelpView::SaveState() const {
    json::Value v = json::Value::MakeObject();
    v.Set("tab", tab_);
    json::Value pages = json::Value::MakeArray();
    json::Value trees = json::Value::MakeArray();
    for (int t = 0; t < 2; ++t) {
        pages.Push(page_[t]);
        trees.Push(tree::SaveState(trees_[t]));
    }
    v.Set("pages", std::move(pages));
    v.Set("trees", std::move(trees));
    return v;
}

void HelpView::LoadState(const json::Value& v) {
    if (!v.IsObject()) return;
    tab_ = std::clamp(static_cast<int>(v.Get("tab").AsDouble(0)), 0, 1);
    const json::Value& pages = v.Get("pages");
    const json::Value& trees = v.Get("trees");
    for (int t = 0; t < 2; ++t) {
        if (static_cast<size_t>(t) < pages.Items().size()) page_[t] = pages.Items()[static_cast<size_t>(t)].AsString();
        if (static_cast<size_t>(t) < trees.Items().size()) tree::LoadState(trees_[t], trees.Items()[static_cast<size_t>(t)]);
    }
    docId_.clear();
    doc_.reset();
}

}  // namespace rm::editor
