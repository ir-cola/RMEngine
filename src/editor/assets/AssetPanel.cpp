#include "editor/assets/AssetPanel.h"

#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"

namespace rm::editor {

using ui::Rect;

namespace {

ui::TextStyle Style(float size, ui::Color color) {
    ui::TextStyle s;
    s.size = size;
    s.color = color;
    return s;
}

}  // namespace

w::Icon IconOf(asset::Kind kind) {
    switch (kind) {
        case asset::Kind::Folder: return w::Icon::Folder;
        case asset::Kind::Image: return w::Icon::Image;
        case asset::Kind::Model: return w::Icon::Cube;
        case asset::Kind::Audio: return w::Icon::Audio;
        case asset::Kind::Font: return w::Icon::Font;
        case asset::Kind::Text: return w::Icon::Text;
        case asset::Kind::Other: break;
    }
    return w::Icon::File;
}

tree::Icon TreeIconOf(asset::Kind kind) {
    switch (kind) {
        case asset::Kind::Folder: return tree::Icon::Folder;
        case asset::Kind::Image: return tree::Icon::Image;
        case asset::Kind::Model: return tree::Icon::Model;
        case asset::Kind::Audio: return tree::Icon::Audio;
        case asset::Kind::Font: return tree::Icon::Font;
        case asset::Kind::Text: return tree::Icon::Text;
        case asset::Kind::Other: break;
    }
    return tree::Icon::File;
}

std::wstring KindLabel(asset::Kind kind) {
    switch (kind) {
        case asset::Kind::Folder: return Tr(L"フォルダ");
        case asset::Kind::Image: return Tr(L"画像");
        case asset::Kind::Model: return Tr(L"3D モデル");
        case asset::Kind::Audio: return Tr(L"音");
        case asset::Kind::Font: return Tr(L"フォント");
        case asset::Kind::Text: return Tr(L"文書");
        case asset::Kind::Other: break;
    }
    return Tr(L"ファイル");
}

namespace {

inspector::RefChoice ChoiceOf(const AssetLibrary& lib, const asset::Entry& e) {
    const asset::Kind kind = asset::KindOf(e.path, e.folder);
    inspector::RefChoice r;
    r.value = e.guid;
    r.name = lib.NameOf(e);
    r.detail = lib.FolderOf(e);
    r.icon = IconOf(kind);
    r.kind = std::string(asset::KindKey(kind));
    return r;
}

}  // namespace

std::optional<inspector::RefChoice> AssetChoice(const AssetLibrary& lib, std::string_view guid) {
    const asset::Entry* e = lib.Index().ByGuid(guid);
    if (!e) return std::nullopt;
    return ChoiceOf(lib, *e);
}

std::vector<inspector::RefChoice> AssetChoices(const AssetLibrary& lib) {
    std::vector<inspector::RefChoice> out;
    const auto entries = lib.Index().Entries();
    for (const size_t i : lib.Order()) out.push_back(ChoiceOf(lib, entries[i]));
    return out;
}

// ---------------------------------------------------------------------------

AssetPanel::AssetPanel(AssetLibrary& library, std::function<void(std::wstring)> showError)
    : lib_(library), showError_(std::move(showError)) {}

void AssetPanel::Report(const std::wstring& problem) {
    if (!problem.empty() && showError_) showError_(problem);
}

std::vector<std::string> AssetPanel::GuidsOf(std::span<const tree::ItemId> ids) const {
    std::vector<std::string> out;
    for (const tree::ItemId id : ids) {
        if (const auto it = guids_.find(id); it != guids_.end()) out.push_back(it->second);
    }
    return out;
}

void AssetPanel::Items(std::vector<tree::Item>& out) {
    guids_.clear();
    const auto entries = lib_.Index().Entries();
    for (const size_t i : lib_.Order()) {
        const asset::Entry& e = entries[i];
        tree::Item it;
        it.id = asset::NumberOf(e.guid);
        const std::string_view parent = asset::ParentOf(e.path);
        if (const asset::Entry* p = parent.empty() ? nullptr : lib_.Index().ByPath(parent)) it.parent = asset::NumberOf(p->guid);
        it.name = std::string(asset::FileNameOf(e.path));
        it.canHaveChildren = e.folder;
        it.icon = TreeIconOf(asset::KindOf(e.path, e.folder));
        guids_[it.id] = e.guid;
        out.push_back(std::move(it));
    }
}

std::wstring AssetPanel::EmptyText() const {
    return Tr(L"Assets フォルダは空です。\nエクスプローラーでファイルを入れると、ここに並びます (右クリック →「エクスプローラーで開く」)。");
}

void AssetPanel::Rename(tree::ItemId id, const std::string& nameUtf8) {
    if (const auto it = guids_.find(id); it != guids_.end()) Report(lib_.Rename(it->second, nameUtf8));
}

void AssetPanel::Move(std::span<const tree::ItemId> items, tree::ItemId parent, size_t index) {
    (void)index;  // a folder lists its contents by name: only where they go matters
    std::string folder;
    if (parent != 0) {
        const auto it = guids_.find(parent);
        if (it == guids_.end()) return;
        folder = it->second;
    }
    Report(lib_.Move(GuidsOf(items), folder));
}

std::vector<tree::Action> AssetPanel::Actions(std::span<const tree::ItemId> selection) {
    std::vector<tree::Action> list;
    const std::vector<std::string> guids = GuidsOf(selection);
    auto separator = [&] {
        tree::Action s;
        s.separator = true;
        list.push_back(std::move(s));
    };

    tree::Action folder;
    folder.label = Tr(L"新しいフォルダ");
    folder.key = ui::Key::N;
    folder.ctrl = folder.shift = true;
    const std::string where = guids.size() == 1 ? guids[0] : std::string();
    folder.run = [this, where] {
        std::string made;
        Report(lib_.NewFolder(where, &made));
        if (!made.empty()) tree::RequestSelect(state_, {asset::NumberOf(made)}, true);  // and type its name
    };
    list.push_back(std::move(folder));
    separator();

    if (guids.empty()) {
        tree::Action open;
        open.label = Tr(L"エクスプローラーで開く");
        open.run = [this] { platform::OpenFolder(lib_.Root()); };
        list.push_back(std::move(open));
        return list;
    }
    tree::Action show;
    show.label = Tr(L"エクスプローラーで表示");
    show.enabled = guids.size() == 1;
    show.run = [this, guid = guids[0]] {
        if (const asset::Entry* e = lib_.Index().ByGuid(guid)) platform::ShowInExplorer(lib_.FullPath(*e));
    };
    list.push_back(std::move(show));
    separator();
    tree::Action del;
    del.label = Tr(L"削除 (ごみ箱へ)");
    del.key = ui::Key::Delete;
    del.run = [this, guids] { Report(lib_.Delete(guids)); };
    list.push_back(std::move(del));
    return list;
}

w::DragThing AssetPanel::DragOut(std::span<const tree::ItemId> items) {
    w::DragThing t;
    t.kind = "asset";
    t.values = GuidsOf(items);
    return t;
}

void AssetPanel::Draw(ui::Context& c, const Rect& r) {
    if (lib_.Poll()) c.RequestRedraw();  // changed on the disk (Explorer ...)
    Rect area = r;
    const Rect info = ui::TakeBottom(area, 24);
    tree::Draw(c, state_, *this, area);

    // Under the tree: what the selected one is and where it is.
    std::wstring text;
    const std::vector<std::string> sel = GuidsOf(state_.sel.ids);
    if (sel.size() == 1) {
        if (const asset::Entry* e = lib_.Index().ByGuid(sel[0])) {
            text = KindLabel(asset::KindOf(e->path, e->folder)) + L"  ·  Assets/" + Utf8ToWide(e->path);
        }
    } else if (sel.size() > 1) {
        text = TrF(L"{} 個選んでいます", sel.size());
    } else {
        text = TrF(L"{} 個のファイルとフォルダ", lib_.Index().Entries().size());
    }
    w::Separator(c, info.x + 8, info.y, info.w - 16);
    c.P().Text(info.Inset(12, 0, 12, 0), text, Style(11, theme::TextFaint));
}

}  // namespace rm::editor
