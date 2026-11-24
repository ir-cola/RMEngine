#include "editor/assets/AssetPanel.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
#include "editor/assets/AssetFileType.h"
#include "editor/assets/AssetGrid.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "platform/Image.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <cmath>

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
    r.file = e.path;
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

AssetPanel::AssetPanel(AssetLibrary& library, Thumbnails* thumbnails, float& thumbnailSize, std::function<void(std::wstring)> showError)
    : lib_(library), thumbnails_(thumbnails), thumbnailSize_(thumbnailSize), showError_(std::move(showError)) {}

void AssetPanel::SetPreviews(const PluginRegistry* registry, std::function<void()> redraw) {
    registry_ = registry;
    described_ = redraw ? std::make_unique<UiWake>(std::move(redraw)) : nullptr;
}

const AssetFileType* FileTypeFor(const PluginRegistry& registry, std::string_view fileName) {
    const std::string ext = asset::Extension(asset::FileNameOf(fileName));
    if (ext.empty()) return nullptr;
    for (const auto& t : registry.Contributions<AssetFileType>(kAssetFileTypePoint)) {
        if (t.item->extension && t.item->type && ext == t.item->extension) return t.item;
    }
    return nullptr;
}

const AssetPreview* PreviewFor(const PluginRegistry& registry, asset::Kind kind) {
    for (const auto& p : registry.Contributions<AssetPreview>(kAssetPreviewPoint)) {
        if (p.item->kind == kind) return p.item;
    }
    return nullptr;
}

namespace {

// 「鳴らしてみる」 / 「止める」。プレビューがある種類のアセット用 (なければ何もない)。
std::vector<w::Action> PreviewActions(const AssetLibrary& lib, const AssetPreview* preview, const std::string& guid) {
    std::vector<w::Action> list;
    const asset::Entry* e = lib.Index().ByGuid(guid);
    if (!preview || !e || !preview->play) return list;
    const AssetLibrary* l = &lib;
    w::Action play;
    play.label = Tr(L"鳴らしてみる");
    play.run = [l, preview, guid] {
        if (const asset::Entry* now = l->Index().ByGuid(guid)) preview->play(l->FullPath(*now), l->Import(guid));
    };
    list.push_back(std::move(play));
    w::Action stop;
    stop.label = Tr(L"止める");
    stop.enabled = preview->playing && preview->playing();
    stop.run = [preview] {
        if (preview->stop) preview->stop();
    };
    list.push_back(std::move(stop));
    return list;
}

}  // namespace

ID2D1Bitmap* AssetThumbnail(ui::Context& c, const AssetLibrary& lib, Thumbnails& thumbnails, std::string_view guid, float size) {
    const asset::Entry* e = lib.Index().ByGuid(guid);
    if (!e) return nullptr;
    const asset::Kind kind = asset::KindOf(e->path, e->folder);
    if (!thumbnails.Has(kind, e->path)) return nullptr;
    // (マテリアルのように別のアセットを名指すファイルのために: 今の id -> ファイルの表。変わったときだけ作る)
    thumbnails.KeepAssets(lib.Version(), [&lib] {
        std::unordered_map<std::string, std::filesystem::path> all;
        for (const asset::Entry& a : lib.Index().Entries()) {
            if (!a.folder) all.emplace(a.guid, lib.FullPath(a));
        }
        return all;
    });
    // ウィンドウのピクセルで (高 DPI の画面でもくっきり)。
    float dpiX = 96, dpiY = 96;
    if (ID2D1DeviceContext* dc = c.P().DC()) dc->GetDpi(&dpiX, &dpiY);
    const uint32_t pixels = static_cast<uint32_t>(std::ceil(size * dpiX / 96.f));
    return thumbnails.Get(c, e->guid, kind, lib.FullPath(*e), pixels);
}

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

// --- 左の木 (フォルダーだけ) -------------------------------------------------------------------

void AssetPanel::Items(std::vector<tree::Item>& out) {
    guids_.clear();
    tree::Item root;
    root.id = kRootItem;
    root.name = "Assets";
    root.canHaveChildren = true;
    root.icon = tree::Icon::Folder;
    out.push_back(std::move(root));
    const auto entries = lib_.Index().Entries();
    for (const size_t i : lib_.Order()) {
        const asset::Entry& e = entries[i];
        if (!e.folder) continue;
        tree::Item it;
        it.id = asset::NumberOf(e.guid);
        const std::string_view parent = asset::ParentOf(e.path);
        const asset::Entry* p = parent.empty() ? nullptr : lib_.Index().ByPath(parent);
        it.parent = p ? asset::NumberOf(p->guid) : kRootItem;
        it.name = std::string(asset::FileNameOf(e.path));
        it.canHaveChildren = true;
        it.icon = tree::Icon::Folder;
        guids_[it.id] = e.guid;
        out.push_back(std::move(it));
    }
}

std::wstring AssetPanel::EmptyText() const { return Tr(L"Assets フォルダは空です。"); }

void AssetPanel::Rename(tree::ItemId id, const std::string& nameUtf8) {
    if (const auto it = guids_.find(id); it != guids_.end()) Report(lib_.Rename(it->second, nameUtf8));
}

void AssetPanel::Move(std::span<const tree::ItemId> items, tree::ItemId parent, size_t index) {
    (void)index;  // フォルダーは中身を名前で並べる: どこに入るかだけが大事
    std::string folder;
    if (parent != 0 && parent != kRootItem) {
        const auto it = guids_.find(parent);
        if (it == guids_.end()) return;
        folder = it->second;
    }
    MoveInto(GuidsOf(items), folder);
}

std::vector<tree::Action> AssetPanel::Actions(std::span<const tree::ItemId> selection) {
    const std::vector<std::string> guids = GuidsOf(selection);
    const std::string where = guids.size() == 1 ? guids[0] : std::string();
    return ActionsFor(guids, where, [this](const std::string& made) { tree::RequestSelect(state_, {asset::NumberOf(made)}, true); });
}

w::DragThing AssetPanel::DragOut(std::span<const tree::ItemId> items) {
    w::DragThing t;
    t.kind = "asset";
    t.values = GuidsOf(items);
    return t;
}

void AssetPanel::SelectionChanged(std::span<const tree::ItemId> ids) {
    // 木でフォルダーを選ぶ = そのフォルダーを開く (右に中身)。
    if (ids.size() != 1) return;
    std::string guid;
    if (ids[0] != kRootItem) {
        const auto it = guids_.find(ids[0]);
        if (it == guids_.end()) return;
        guid = it->second;
    }
    if (guid != folder_) {
        folder_ = guid;
        grid_.sel.clear();
        grid_.anchor.clear();
        grid_.search.clear();
    }
}

bool AssetPanel::CanDrop(const w::DragThing& thing, tree::ItemId onto) const {
    if (thing.kind != "asset") return false;
    std::string folder;
    if (onto != kRootItem) {
        const auto it = guids_.find(onto);
        if (it == guids_.end()) return false;
        folder = it->second;
    }
    return CanMoveInto(thing.values, folder);
}

void AssetPanel::Drop(const w::DragThing& thing, tree::ItemId onto) {
    std::string folder;
    if (onto != kRootItem) {
        const auto it = guids_.find(onto);
        if (it == guids_.end()) return;
        folder = it->second;
    }
    MoveInto(thing.values, folder);
}

// --- フォルダーと移動 --------------------------------------------------------------------------

std::string AssetPanel::FolderPath() const {
    if (folder_.empty()) return {};
    const asset::Entry* e = lib_.Index().ByGuid(folder_);
    return e && e->folder ? e->path : std::string();
}

void AssetPanel::OpenFolder(const std::string& guid) {
    folder_ = guid;
    grid_.sel.clear();
    grid_.anchor.clear();
    grid_.search.clear();
    grid_.renaming.clear();
    tree::RequestSelect(state_, {guid.empty() ? kRootItem : asset::NumberOf(guid)});
}

bool AssetPanel::CanMoveInto(const std::vector<std::string>& guids, const std::string& folderGuid) const {
    if (guids.empty()) return false;
    std::string target;
    if (!folderGuid.empty()) {
        const asset::Entry* f = lib_.Index().ByGuid(folderGuid);
        if (!f || !f->folder) return false;
        target = f->path;
    }
    bool changes = false;
    for (const std::string& g : guids) {
        const asset::Entry* e = lib_.Index().ByGuid(g);
        if (!e || g == folderGuid) return false;
        // フォルダーを自分の中へは入れられない。
        if (e->folder && (target == e->path || target.starts_with(e->path + "/"))) return false;
        if (asset::ParentOf(e->path) != target) changes = true;
    }
    return changes;
}

void AssetPanel::MoveInto(const std::vector<std::string>& guids, const std::string& folderGuid) {
    if (!CanMoveInto(guids, folderGuid)) return;
    Report(lib_.Move(guids, folderGuid));
}

// --- メニュー ----------------------------------------------------------------------------------

std::vector<w::Action> AssetPanel::ActionsFor(const std::vector<std::string>& guids, const std::string& where,
                                              std::function<void(const std::string&)> made) {
    std::vector<w::Action> list;
    auto separator = [&] {
        w::Action s;
        s.separator = true;
        list.push_back(std::move(s));
    };

    w::Action folder;
    folder.label = Tr(L"新しいフォルダ");
    folder.key = ui::Key::N;
    folder.ctrl = folder.shift = true;
    folder.run = [this, where, made] {
        std::string m;
        Report(lib_.NewFolder(where, &m));
        if (!m.empty() && made) made(m);  // 選んで、名前を打つ
    };
    list.push_back(std::move(folder));
    // 作成 ▸ プラグインが作る種類のファイル (マテリアルなど: AssetFileType.h)。`where` に作り、名前を打たせる。
    if (registry_) {
        for (const auto& t : registry_->Contributions<AssetFileType>(kAssetFileTypePoint)) {
            const AssetFileType* ft = t.item;
            if (!ft->extension || !ft->type || !ft->label) continue;
            w::Action make;
            make.label = Tr(ft->label);
            make.folder = Tr(L"作成");
            make.run = [this, ft, where, made] {
                std::string m;
                const reflect::TypeDesc& type = ft->type();
                const reflect::Object fresh(type);  // (既定値の新しいもの)
                const std::string bytes = json::Write(reflect::Save(type, fresh.Get()));
                Report(lib_.NewFile(where, ft->folder ? ft->folder : "", WideToUtf8(Tr(ft->newName ? ft->newName : ft->label)), ft->extension, bytes,
                                    &m));
                if (!m.empty() && made) made(m);  // 選んで、名前を打つ
            };
            list.push_back(std::move(make));
        }
    }
    separator();

    if (guids.empty()) {
        w::Action open;
        open.label = Tr(L"エクスプローラーで開く");
        open.run = [this, where] {
            const asset::Entry* e = where.empty() ? nullptr : lib_.Index().ByGuid(where);
            platform::OpenFolder(e && e->folder ? lib_.FullPath(*e) : lib_.Root());
        };
        list.push_back(std::move(open));
        return list;
    }
    if (registry_ && guids.size() == 1) {
        // 音: ここで聞く (音プラグインのプレビュー)。
        if (const asset::Entry* e = lib_.Index().ByGuid(guids[0])) {
            for (w::Action& a : PreviewActions(lib_, PreviewFor(*registry_, asset::KindOf(e->path, e->folder)), guids[0])) list.push_back(std::move(a));
        }
    }
    // アセットの詳細と取り込みの設定をインスペクターに出す (選んだときの出し方と同じ - その後
    // インスペクターが別の物を出していても)。
    w::Action details;
    details.label = Tr(L"詳しく見る (インスペクター)");
    details.enabled = guids.size() == 1 && static_cast<bool>(inspect_);
    details.run = [this, guid = guids[0]] { Inspect(guid); };
    list.push_back(std::move(details));
    w::Action show;
    show.label = Tr(L"エクスプローラーで表示");
    show.enabled = guids.size() == 1;
    show.run = [this, guid = guids[0]] {
        if (const asset::Entry* e = lib_.Index().ByGuid(guid)) platform::ShowInExplorer(lib_.FullPath(*e));
    };
    list.push_back(std::move(show));
    separator();
    w::Action del;
    del.label = Tr(L"削除 (ごみ箱へ)");
    del.key = ui::Key::Delete;
    del.run = [this, guids] { Report(lib_.Delete(guids)); };
    list.push_back(std::move(del));
    return list;
}

// アセットの取り込みの設定の 1 回の変更: その .meta に書く (Undo すると戻る)。
class ImportCommand final : public ICommand {
public:
    ImportCommand(AssetLibrary& lib, std::weak_ptr<AssetPanel::ImportView> view, std::string guid, const reflect::TypeDesc& type,
                  reflect::FieldPath path, json::Value before, json::Value after, std::string label, uint64_t merge)
        : lib_(lib), view_(std::move(view)), guid_(std::move(guid)), type_(type), path_(std::move(path)), before_(std::move(before)),
          after_(std::move(after)), label_(std::move(label)), merge_(merge) {}

    void Do() override { Apply(after_); }
    void Undo() override { Apply(before_); }
    std::string Label() const override { return label_; }
    bool IsNoOp() const override { return before_ == after_; }
    bool MergeWith(const ICommand& next) override {
        const auto* n = dynamic_cast<const ImportCommand*>(&next);
        if (!n || merge_ == 0 || n->merge_ != merge_ || n->guid_ != guid_ || n->path_ != path_) return false;
        after_ = n->after_;
        return true;
    }

private:
    void Apply(const json::Value& v) {
        if (!lib_.Index().ByGuid(guid_)) return;  // (削除された)
        // 今の設定に、この欄だけを変えた物 (ほかの設定と知らないキーはそのまま)。編集が終わったときに
        // .meta に書く (AssetLibrary::FlushImports)。ドラッグの 1 歩ごとには書かない。
        reflect::Object o(type_);
        reflect::Unknowns u;
        reflect::Load(type_, o.Get(), lib_.Import(guid_), &u);
        const reflect::PathTarget pt = reflect::Resolve(type_, o.Get(), &u, path_);
        if (!pt.field) return;
        reflect::LoadField(*pt.field, pt.owner, v, pt.unknowns, nullptr);
        const json::Value out = reflect::Save(type_, o.Get(), &u);
        lib_.QueueImport(guid_, out);
        if (auto view = view_.lock(); view && view->guid == guid_) {
            view->object = reflect::Object(type_);
            view->unknowns = {};
            reflect::Load(type_, view->object.Get(), out, &view->unknowns);
        }
    }

    AssetLibrary& lib_;
    std::weak_ptr<AssetPanel::ImportView> view_;
    std::string guid_;
    const reflect::TypeDesc& type_;
    reflect::FieldPath path_;
    json::Value before_, after_;
    std::string label_;
    uint64_t merge_ = 0;
};

// プラグインが作る種類のファイル (AssetFileType) の中身の 1 回の編集: 前と後の中身 (1 つの欄に打つ間は 1 件)。
// エディターの持つ中身を替える (シーンビューなどがすぐ読む)。ファイルには編集が終わったときに書く。
class ContentCommand final : public ICommand {
public:
    ContentCommand(AssetLibrary& lib, std::string guid, std::string before, std::string after, std::string label, uint64_t merge)
        : lib_(lib), guid_(std::move(guid)), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)), merge_(merge) {}

    void Do() override { Apply(after_); }
    void Undo() override { Apply(before_); }
    std::string Label() const override { return label_; }
    bool IsNoOp() const override { return before_ == after_; }
    bool CanMergeWith(const ICommand& next) const override {
        const auto* n = dynamic_cast<const ContentCommand*>(&next);
        return n && merge_ != 0 && n->merge_ == merge_ && n->guid_ == guid_;
    }
    bool MergeWith(const ICommand& next) override {
        if (!CanMergeWith(next)) return false;
        after_ = static_cast<const ContentCommand&>(next).after_;
        return true;
    }
    size_t Bytes() const override { return before_.size() + after_.size(); }

private:
    void Apply(const std::string& bytes) {
        if (!lib_.Index().ByGuid(guid_)) return;  // (削除された)
        lib_.QueueContent(guid_, bytes);
    }

    AssetLibrary& lib_;
    std::string guid_;
    std::string before_, after_;
    std::string label_;
    uint64_t merge_ = 0;
};

namespace {

// 中身 `bytes` を `type` の物 `o` (と、その知らないキー) に読む。JSON でない・`kind` が違う: false。
bool ReadContent(const AssetFileType& ft, const reflect::TypeDesc& type, std::string_view bytes, reflect::Object& o, reflect::Unknowns& u) {
    const auto v = json::Parse(bytes);
    if (!v || !v->IsObject()) return false;
    if (ft.kind && v->Get("kind").AsString() != ft.kind) return false;
    o = reflect::Object(type);
    u = {};
    reflect::Load(type, o.Get(), *v, &u);
    return true;
}

}  // namespace

void AssetPanel::SetInspector(std::function<void(Inspectable)> inspect, std::function<void(std::unique_ptr<ICommand>)> push) {
    inspect_ = std::move(inspect);
    push_ = std::move(push);
}

void AssetPanel::Inspect(const std::string& which) {
    if (!inspect_) return;
    const asset::Entry* e = lib_.Index().ByGuid(which);
    if (!e) return;
    const std::string guid = e->guid;
    const asset::Kind kind = asset::KindOf(e->path, e->folder);
    const reflect::TypeDesc* type = asset::ImportTypeOf(kind);
    // .meta が今持っている設定。
    view_->guid = guid;
    view_->unknowns = {};
    if (type) {
        view_->object = reflect::Object(*type);
        reflect::Load(*type, view_->object.Get(), lib_.Import(guid), &view_->unknowns);
    } else {
        view_->object = {};
    }
    // ファイルが何か: ディスク上のサイズ、変更した日時、画像ならピクセルのサイズ。
    std::wstring facts;
    if (!e->folder) {
        const std::filesystem::path full = lib_.FullPath(*e);
        std::error_code ec;
        const uintmax_t bytes = std::filesystem::file_size(full, ec);
        const auto changed = std::filesystem::last_write_time(full, ec);
        if (!ec) {
            const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(changed);
            const std::time_t t = std::chrono::system_clock::to_time_t(sys);
            std::tm local{};
            localtime_s(&local, &t);
            const double kb = static_cast<double>(bytes) / 1024.0;
            const std::wstring size = kb < 1024.0 ? std::format(L"{:.1f} KB", kb) : std::format(L"{:.1f} MB", kb / 1024.0);
            facts = TrF(L"ファイル {}  ·  変更 {}", size,
                        std::format(L"{}/{:02}/{:02} {:02}:{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min));
        }
        if (kind == asset::Kind::Image) {
            if (const auto px = platform::ImageFileSize(platform::ThreadImagingFactory(), full)) {
                facts += L"\n" + TrF(L"{} x {} ピクセル", px->first, px->second);
            }
        }
    }
    Inspectable target;
    target.title = lib_.NameOf(*e);
    AssetLibrary* lib = &lib_;
    std::shared_ptr<ImportView> view = view_;
    auto push = push_;
    const AssetPreview* preview = registry_ ? PreviewFor(*registry_, kind) : nullptr;
    const AssetFileType* fileType = registry_ && !e->folder ? FileTypeFor(*registry_, e->path) : nullptr;
    std::function<void()> known = described_ ? described_->Signal() : std::function<void()>();
    // プラグインが作る種類のファイル: その中身 (エディターの持つもの) を、欄で編集する。
    struct ContentView {
        std::string bytes;  // `object` を読んだ元
        reflect::Object object;
        reflect::Unknowns unknowns;
        bool ok = false;
        double readAt = -1e9;
    };
    auto content = std::make_shared<ContentView>();
    target.blocks = [lib, view, guid, type, push, preview, known, facts, fileType, content] {
        std::vector<InspectBlock> blocks;
        const asset::Entry* now = lib->Index().ByGuid(guid);
        if (!now) return blocks;  // 削除された: 何も出さない
        InspectBlock info;
        const std::wstring kindName = fileType ? Tr(fileType->label) : KindLabel(asset::KindOf(now->path, now->folder));
        info.note = kindName + L"  ·  Assets/" + Utf8ToWide(now->path) + L"\nID " + Utf8ToWide(guid);
        if (!facts.empty()) info.note += L"\n" + facts;
        if (preview && preview->describe) {
            // (音: 長さと形式 - 「鳴らしてみる」はブロックの ⋮ メニューにある)
            if (const std::wstring line = preview->describe(lib->FullPath(*now), known); !line.empty()) info.note += L"\n" + line;
            info.title = KindLabel(asset::KindOf(now->path, now->folder));
            info.menu = PreviewActions(*lib, preview, guid);
        }
        if (fileType) {
            const reflect::TypeDesc& ctype = fileType->type();
            // 今の中身 (書いていない編集があればそれ。ファイルは 0.5 秒に 1 回まで読み直す)。
            const double t = platform::NowSeconds();
            if (lib->HasQueuedContent(guid) || t - content->readAt > 0.5) {
                content->readAt = t;
                if (const auto bytes = lib->Content(guid); bytes && (*bytes != content->bytes || content->object.Empty())) {
                    content->bytes = *bytes;
                    content->ok = ReadContent(*fileType, ctype, *bytes, content->object, content->unknowns);
                }
            }
            if (!content->ok) {
                info.note += L"\n" + TrF(L"このファイルは{}として読めません (JSON が壊れているか、別の種類のファイルです)", Tr(fileType->label));
                blocks.push_back(std::move(info));
                return blocks;
            }
            blocks.push_back(std::move(info));
            InspectBlock edit;
            edit.type = &ctype;
            edit.title = Tr(fileType->label);
            edit.note = fileType->note ? Tr(fileType->note) : Tr(L"変えるとすぐに使っている所 (シーンビューなど) に出て、編集が終わるとファイルに書きます。Ctrl+Z で戻せます");
            edit.resolve = [content]() -> reflect::EditTarget {
                if (!content->ok || content->object.Empty()) return {};
                return {content->object.Get(), &content->unknowns};
            };
            edit.onEdit = [lib, guid, fileType, push](InspectBlock::Change ch) {
                // 今の中身に、この欄だけを変えたもの (ほかの欄と知らないキーはそのまま)。
                const auto bytes = lib->Content(guid);
                if (!bytes || !push) return;
                const reflect::TypeDesc& ct = fileType->type();
                reflect::Object o;
                reflect::Unknowns u;
                if (!ReadContent(*fileType, ct, *bytes, o, u)) return;
                const reflect::PathTarget pt = reflect::Resolve(ct, o.Get(), &u, ch.path);
                if (!pt.field) return;
                reflect::LoadField(*pt.field, pt.owner, ch.after, pt.unknowns, nullptr);
                std::string after = json::Write(reflect::Save(ct, o.Get(), &u));
                if (after == *bytes) return;
                push(std::make_unique<ContentCommand>(*lib, guid, *bytes, std::move(after), std::move(ch.label), ch.merge));
            };
            blocks.push_back(std::move(edit));
            return blocks;
        }
        blocks.push_back(std::move(info));
        if (!type) return blocks;
        InspectBlock settings;
        settings.type = type;
        settings.title = Utf8ToWide(TrU8(type->label));
        settings.note = Tr(L"変えると .meta にすぐ書き、使っている所 (シーンビューなど) が読み直します。Ctrl+Z で戻せます");
        settings.resolve = [view, guid]() -> reflect::EditTarget {
            if (view->guid != guid || view->object.Empty()) return {};
            return {view->object.Get(), &view->unknowns};
        };
        std::weak_ptr<ImportView> weak = view;
        settings.onEdit = [lib, weak, guid, type, push](InspectBlock::Change ch) {
            if (push) push(std::make_unique<ImportCommand>(*lib, weak, guid, *type, std::move(ch.path), std::move(ch.before), std::move(ch.after),
                                                           std::move(ch.label), ch.merge));
        };
        blocks.push_back(std::move(settings));
        return blocks;
    };
    inspect_(std::move(target));
}

// --- 描く -------------------------------------------------------------------------------------

namespace {

constexpr float kTopH = 32;    // パンくずリストと検索欄の帯
constexpr float kInfoH = 24;   // 下の帯
constexpr float kSplitW = 5;   // 木と並びの間の区切り (つかめる幅)

}  // namespace

void AssetPanel::DrawTop(ui::Context& c, const Rect& r) {
    auto& p = c.P();
    // 右: 検索欄 (Assets 全体から探す。Esc で消す)。
    const float searchW = std::clamp(r.w * 0.35f, 120.f, 240.f);
    const Rect search{r.Right() - 8 - searchW, r.y + 4, searchW, r.h - 8};
    const ui::Id searchId = c.MakeId("asset.search");
    if (c.Focused() == searchId && c.KeyPressed(ui::Key::Escape)) {
        grid_.search.clear();
        c.ClearFocus();
    }
    if (w::TextField(c, "asset.search", search, grid_.search, Tr(L"検索"))) {
        grid_.sel.clear();
        grid_.anchor.clear();
    }

    // 左: パンくずリスト。それぞれ押すとそのフォルダーへ。ドラッグしたものを落とすとそこへ移す。
    const Rect crumbs{r.x + 6, r.y + 4, search.x - 8 - r.x - 6, r.h - 8};
    std::vector<std::pair<std::string, std::wstring>> steps;  // (フォルダーの id、名前)
    steps.emplace_back(std::string(), L"Assets");
    for (const std::string& path : AssetFolderSteps(FolderPath())) {
        if (const asset::Entry* e = lib_.Index().ByPath(path)) steps.emplace_back(e->guid, Utf8ToWide(asset::FileNameOf(path)));
    }
    c.PushClip(crumbs);
    float x = crumbs.x;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) {
            w::DrawIcon(c, {x, crumbs.y, 14, crumbs.h}, w::Icon::ChevronRight, theme::TextFaint);
            x += 16;
        }
        const float bw = p.TextWidth(steps[i].second, theme::FontSize) + 14;
        const Rect b{x, crumbs.y, bw, crumbs.h};
        c.PushId(static_cast<uint64_t>(i));
        if (w::Button(c, "crumb", b, steps[i].second, w::ButtonKind::Ghost)) OpenFolder(steps[i].first);
        c.PopId();
        if (w::DraggedThing()) {
            const std::string folder = steps[i].first;
            if (const w::DragThing* t = w::DropTarget(c, b, [&](const w::DragThing& d) { return d.kind == "asset" && CanMoveInto(d.values, folder); })) {
                MoveInto(t->values, folder);
            }
        }
        x += bw;
    }
    c.PopClip();
}

void AssetPanel::DrawGrid(ui::Context& c, const Rect& view) {
    auto& p = c.P();
    const ui::Input& in = c.In();
    const auto entries = lib_.Index().Entries();
    const std::string path = FolderPath();
    const bool searching = !grid_.search.empty();
    const std::vector<size_t> list = searching ? AssetsMatching(entries, lib_.Order(), grid_.search) : AssetsInFolder(entries, lib_.Order(), path);
    const size_t back = !searching && !path.empty() ? 1 : 0;  // 先頭の「戻る」
    const size_t count = list.size() + back;
    std::erase_if(grid_.sel, [&](const std::string& g) { return !lib_.Index().ByGuid(g); });
    std::string parentGuid;  // 「戻る」の行き先
    if (back) {
        const std::string_view up = asset::ParentOf(path);
        if (const asset::Entry* e = up.empty() ? nullptr : lib_.Index().ByPath(up)) parentGuid = e->guid;
    }
    auto isSelected = [&](const std::string& g) { return std::find(grid_.sel.begin(), grid_.sel.end(), g) != grid_.sel.end(); };
    // その場で名前を変え始める (メニューはあとのフレームで実行するので、パネルの物だけを使う)。
    auto startRename = [this](const std::string& guid) {
        const asset::Entry* e = lib_.Index().ByGuid(guid);
        grid_.search.clear();
        grid_.sel = {guid};
        grid_.anchor = guid;
        grid_.renaming = guid;
        grid_.renameText = e ? std::string(asset::FileNameOf(e->path)) : TrU8("新しいフォルダ");
        grid_.renameStart = true;
        grid_.keys = true;
    };
    auto menuFor = [&] {
        // (新しいフォルダは今のフォルダーの中に出来る: 選んで名前を打たせる)
        std::vector<w::Action> actions = ActionsFor(grid_.sel, folder_, startRename);
        if (grid_.sel.size() == 1) {
            w::Action rename;
            rename.label = Tr(L"名前を変更");
            rename.key = ui::Key::F2;
            rename.run = [startRename, guid = grid_.sel[0]] { startRename(guid); };
            actions.insert(actions.end() - 2, std::move(rename));  // (区切りと削除の前)
        }
        return actions;
    };

    const bool inputWas = c.InputEnabled();
    if (grid_.menu.open) c.SetInputEnabled(false);
    if (c.MousePressed() || c.MousePressed(ui::Mouse::Right)) grid_.keys = c.IsHovered(view);

    // --- キーボード (最後のクリックが並びで、どのテキストフィールドもキーを持っていないとき) -------
    if (grid_.keys && c.Focused() == 0 && grid_.renaming.empty() && !grid_.dragging && c.InputEnabled() && !in.keys.empty()) {
        if (c.KeyPressed(ui::Key::A, true)) {
            grid_.sel.clear();
            for (const size_t i : list) grid_.sel.push_back(entries[i].guid);
        }
        const asset::Entry* one = grid_.sel.size() == 1 ? lib_.Index().ByGuid(grid_.sel[0]) : nullptr;
        if (c.KeyPressed(ui::Key::Enter) && one && one->folder) {
            OpenFolder(one->guid);
        } else if (c.KeyPressed(ui::Key::Backspace) && back) {
            OpenFolder(parentGuid);
        } else {
            // メニューのショートカット (F2、Delete、Ctrl+Shift+N)。
            for (const w::Action& a : menuFor()) {
                if (a.key != ui::Key::None && a.enabled && a.run && c.KeyPressed(a.key, a.ctrl, a.shift)) {
                    a.run();
                    break;
                }
            }
        }
        c.RequestRedraw();
    }

    // --- タイル ---------------------------------------------------------------------------------
    const float icon = std::clamp(thumbnailSize_, kMinThumbnail, kMaxThumbnail);
    const AssetGridMetrics m = MeasureAssetGrid(view.w - 12, icon);  // (右にスクロールバー)
    const float content = AssetGridHeight(m, count);
    const float off = w::BeginScroll(c, "asset.grid", view, content);
    const float x0 = view.x, y0 = view.y - off;
    if (count == 0) {
        ui::TextStyle hint = Style(12, theme::TextFaint);
        hint.wrap = true;
        hint.v = ui::VAlign::Top;
        const std::wstring text = searching ? Tr(L"見つかりません。")
                                  : entries.empty()
                                      ? Tr(L"Assets フォルダは空です。\nエクスプローラーでファイルを入れると、ここに並びます (右クリック →「エクスプローラーで開く」)。")
                                      : Tr(L"このフォルダは空です。");
        p.Text({view.x + 12, view.y + 8, view.w - 24, 80}, text, hint);
    }
    ui::TextStyle nameStyle = Style(12, theme::Text);
    nameStyle.h = ui::HAlign::Center;
    nameStyle.v = ui::VAlign::Top;
    nameStyle.wrap = true;
    bool tilePressed = false, tileRightClicked = false, renameDrawn = false;
    for (size_t k = 0; k < count; ++k) {
        const Rect tile = AssetTileRect(m, x0, y0, k);
        if (tile.Bottom() < view.y || tile.y > view.Bottom()) continue;
        const Rect pic{tile.x + (tile.w - m.icon) * 0.5f, tile.y + 6, m.icon, m.icon};
        const Rect name{tile.x + 2, pic.Bottom() + 4, tile.w - 4, kAssetNameLine * 2 + 2};
        if (k < back) {
            // 「戻る」: 押すと親のフォルダーへ。ドラッグしたものを落とすと親へ移す。
            c.PushId("back");
            const auto b = c.ButtonBehavior(c.MakeId("tile"), tile);
            c.PopId();
            if (b.hovered && c.MousePressed()) tilePressed = true;
            w::DrawTile(c, tile, false, b.hovered && !grid_.dragging);
            w::DrawIcon(c, pic, w::Icon::Back, theme::TextDim, m.icon * 0.7f);
            p.Text(name, Tr(L"戻る"), nameStyle);
            if (b.clicked && !grid_.dragging) OpenFolder(parentGuid);
            if (w::DraggedThing()) {
                if (const w::DragThing* t = w::DropTarget(c, tile, [&](const w::DragThing& d) { return d.kind == "asset" && CanMoveInto(d.values, parentGuid); })) {
                    MoveInto(t->values, parentGuid);
                }
            }
            continue;
        }
        const asset::Entry& e = entries[list[k - back]];
        const std::string guid = e.guid;
        const asset::Kind kind = asset::KindOf(e.path, e.folder);
        const bool selected = isSelected(guid);
        const bool hovered = c.IsHovered(tile);
        c.PushId(guid);
        if (hovered && c.MousePressed() && grid_.renaming != guid) {
            tilePressed = true;
            grid_.press = guid;
            grid_.pressX = in.mouseX;
            grid_.pressY = in.mouseY;
            grid_.pressDeferred = false;
            if (in.doubleClicked && !in.ctrl && !in.shift) {
                // ダブルクリック: フォルダーは中へ。ファイルはインスペクターに。
                grid_.press.clear();
                if (e.folder) {
                    OpenFolder(guid);
                } else {
                    grid_.inspect = guid;
                }
            } else if (in.ctrl) {
                if (selected) {
                    std::erase(grid_.sel, guid);
                } else {
                    grid_.sel.push_back(guid);
                }
                grid_.anchor = guid;
            } else if (in.shift && !grid_.anchor.empty()) {
                // 起点からここまで (並びの順で)。
                size_t a = count, b = count;
                for (size_t j = 0; j < list.size(); ++j) {
                    if (entries[list[j]].guid == grid_.anchor) a = j;
                    if (entries[list[j]].guid == guid) b = j;
                }
                grid_.sel.clear();
                if (a < count && b < count) {
                    for (size_t j = std::min(a, b); j <= std::max(a, b); ++j) grid_.sel.push_back(entries[list[j]].guid);
                } else {
                    grid_.sel = {guid};
                }
            } else if (selected) {
                grid_.pressDeferred = grid_.sel.size() > 1;  // ほかのタイルはドラッグのために残す
                if (!grid_.pressDeferred) grid_.inspect = guid;
            } else {
                grid_.sel = {guid};
                grid_.anchor = guid;
                grid_.inspect = guid;
            }
            c.RequestRedraw();
        }
        if (hovered && c.MousePressed(ui::Mouse::Right)) {
            tileRightClicked = true;
            if (!selected) {
                grid_.sel = {guid};
                grid_.anchor = guid;
            }
            grid_.menuActions = menuFor();
            if (!grid_.menuActions.empty()) grid_.menu.OpenAt(in.mouseX, in.mouseY);
        }

        w::DrawTile(c, tile, selected, hovered && !grid_.dragging, grid_.keys);
        // 絵: フォルダー / サムネイル (画像・3D モデル) / 種類の絵。
        ID2D1Bitmap* bitmap = !e.folder && thumbnails_ && icon >= kThumbnailsFrom ? AssetThumbnail(c, lib_, *thumbnails_, guid, m.icon) : nullptr;
        if (e.folder) {
            w::DrawIcon(c, pic, w::Icon::Folder, theme::Folder, m.icon);
        } else if (bitmap) {
            w::DrawThumbnail(c, pic, bitmap);
        } else {
            w::DrawIcon(c, pic.Inset(m.icon * 0.22f), IconOf(kind), theme::TextDim);
        }
        // フォルダーへ落とす (並びのタイルでも、左の木からでも)。
        if (e.folder && w::DraggedThing()) {
            if (const w::DragThing* t = w::DropTarget(c, tile, [&](const w::DragThing& d) { return d.kind == "asset" && CanMoveInto(d.values, guid); })) {
                MoveInto(t->values, guid);
            }
        }
        // 名前 (2 行まで)、またはその場で名前を変える欄。
        if (grid_.renaming == guid) {
            renameDrawn = true;
            const ui::Id fid = c.MakeId("rename");
            if (grid_.renameStart) {
                c.FocusText(fid, Utf8ToWide(grid_.renameText), true);
                grid_.renameStart = false;
            }
            const bool focusedBefore = c.Focused() == fid;
            const bool enter = focusedBefore && c.KeyPressed(ui::Key::Enter);
            const bool escape = focusedBefore && c.KeyPressed(ui::Key::Escape);
            w::TextField(c, "rename", {tile.x - 6, name.y - 2, tile.w + 12, 22}, grid_.renameText);
            if (escape) {
                c.ClearFocus();
                grid_.renaming.clear();
            } else if (enter || c.Focused() != fid) {
                if (enter) c.ClearFocus();
                grid_.renaming.clear();
                if (!grid_.renameText.empty() && grid_.renameText != asset::FileNameOf(e.path)) Report(lib_.Rename(guid, grid_.renameText));
            }
        } else {
            c.PushClip(name);
            p.Text(name, Utf8ToWide(asset::FileNameOf(e.path)), nameStyle);
            c.PopClip();
            if (searching && hovered && !grid_.dragging) w::Tooltip(c, L"Assets/" + Utf8ToWide(e.path));
        }
        c.PopId();
    }
    // スクロールで見えなくなったタイルの名前変更は、入力した内容で終わらせる。
    if (!grid_.renaming.empty() && !renameDrawn && !grid_.renameStart) {
        const asset::Entry* e = lib_.Index().ByGuid(grid_.renaming);
        if (e && !grid_.renameText.empty() && grid_.renameText != asset::FileNameOf(e->path)) Report(lib_.Rename(grid_.renaming, grid_.renameText));
        grid_.renaming.clear();
    }

    // --- 何もない所のクリック ------------------------------------------------------------------
    const Rect emptyArea{view.x, view.y, view.w - 12, view.h};
    if (!tilePressed && c.IsHovered(emptyArea) && c.MousePressed() && !in.ctrl && !in.shift) {
        grid_.sel.clear();
        grid_.anchor.clear();
    }
    if (!tileRightClicked && c.IsHovered(emptyArea) && c.MousePressed(ui::Mouse::Right)) {
        grid_.sel.clear();
        grid_.anchor.clear();
        grid_.menuActions = menuFor();
        if (!grid_.menuActions.empty()) grid_.menu.OpenAt(in.mouseX, in.mouseY);
    }

    // --- ドラッグ (フォルダーへ移す / インスペクターの参照欄へ) -------------------------------------
    if (!grid_.press.empty()) {
        if (!grid_.dragging && in.down[0] && std::hypot(in.mouseX - grid_.pressX, in.mouseY - grid_.pressY) >= 5.f) {
            if (!isSelected(grid_.press)) grid_.sel = {grid_.press};
            grid_.dragging = true;
            grid_.pressDeferred = false;
            grid_.inspect.clear();
        }
        if (grid_.dragging && c.KeyPressed(ui::Key::Escape)) {
            grid_.dragging = false;
            grid_.press.clear();
            w::EndDragThing(c);
            c.RequestRedraw();
        }
    }
    if (grid_.dragging) w::SetDragThing({"asset", grid_.sel});
    w::EndScroll(c, "asset.grid", view, content);
    if (!grid_.press.empty() && !in.down[0]) {
        // 離した。
        if (!grid_.dragging && grid_.pressDeferred) {
            grid_.sel = {grid_.press};
            grid_.anchor = grid_.press;
            grid_.inspect = grid_.press;
        }
        if (grid_.dragging) w::EndDragThing(c);  // (これのあとに描くパネルも、離したのを見る)
        grid_.press.clear();
        grid_.pressDeferred = false;
        grid_.dragging = false;
        c.RequestRedraw();
    }
    // 1 つクリックしたものをインスペクターに出す - ボタンを離してから、ドラッグの後ではない
    // (ドラッグは、インスペクターが今出している物の欄へ持って行くところかもしれない)。
    if (!grid_.inspect.empty() && !in.down[0] && !grid_.dragging) {
        Inspect(grid_.inspect);
        grid_.inspect.clear();
    }
    if (grid_.dragging) {
        const asset::Entry* one = grid_.sel.size() == 1 ? lib_.Index().ByGuid(grid_.sel[0]) : nullptr;
        w::DragTag(c, one ? Utf8ToWide(asset::FileNameOf(one->path)) : TrF(L"{} 個", grid_.sel.size()), false);
    }

    c.SetInputEnabled(inputWas);
    w::ActionMenu(c, grid_.menu, grid_.menuActions);
}

void AssetPanel::Draw(ui::Context& c, const Rect& r) {
    if (lib_.Poll()) c.RequestRedraw();  // ディスクで変わった (エクスプローラーなど)
    if (!folder_.empty() && FolderPath().empty()) OpenFolder({});  // 今のフォルダーが消えた: 一番上へ
    if (state_.sel.ids.empty() && !state_.hasPending) tree::RequestSelect(state_, {folder_.empty() ? kRootItem : asset::NumberOf(folder_)});
    Rect area = r;
    const Rect info = ui::TakeBottom(area, kInfoH);
    const Rect top = ui::TakeTop(area, kTopH);
    DrawTop(c, top);
    w::Separator(c, top.x, top.Bottom() - 1, top.w);

    // 左: フォルダーの木 (狭いときは出さない)。区切りをドラッグして幅を変える。
    if (area.w >= 320) {
        treeW_ = std::clamp(treeW_, 120.f, std::max(120.f, area.w - 200.f));
        const Rect left = ui::TakeLeft(area, treeW_);
        const Rect split = ui::TakeLeft(area, kSplitW);
        const auto b = c.ButtonBehavior(c.MakeId("asset.split"), split);
        if (b.hovered || b.held) c.SetCursor(ui::Cursor::SizeWE);
        if (b.held && c.In().down[0]) {
            const float wanted = std::clamp(c.In().mouseX - left.x, 120.f, std::max(120.f, r.w - 200.f));
            if (wanted != treeW_) {
                treeW_ = wanted;
                c.RequestRedraw();
            }
        }
        tree::Draw(c, state_, *this, left);
        w::VSeparator(c, split.x + 2, split.y, split.h);
    }
    DrawGrid(c, area.Inset(2, 0, 0, 0));

    // 下: 選んだ物が何で、どこにあるか。
    std::wstring text;
    if (grid_.sel.size() == 1) {
        if (const asset::Entry* e = lib_.Index().ByGuid(grid_.sel[0])) {
            const asset::Kind kind = asset::KindOf(e->path, e->folder);
            const AssetFileType* fileType = registry_ && !e->folder ? FileTypeFor(*registry_, e->path) : nullptr;
            text = (fileType ? Tr(fileType->label) : KindLabel(kind)) + L"  ·  Assets/" + Utf8ToWide(e->path);
            const AssetPreview* preview = registry_ ? PreviewFor(*registry_, kind) : nullptr;
            if (preview && preview->describe) {
                const std::function<void()> known = described_ ? described_->Signal() : std::function<void()>();
                if (const std::wstring line = preview->describe(lib_.FullPath(*e), known); !line.empty()) text += L"  ·  " + line;
            }
        }
    } else if (grid_.sel.size() > 1) {
        text = TrF(L"{} 個選んでいます", grid_.sel.size());
    } else if (!grid_.search.empty()) {
        text = TrF(L"{} 個見つかりました", AssetsMatching(lib_.Index().Entries(), lib_.Order(), grid_.search).size());
    } else {
        text = TrF(L"{} 個のファイルとフォルダ", lib_.Index().Entries().size());
    }
    // フォルダーを調べている (Worker で): 終わるまでは、最後の調べの結果を出す。
    if (lib_.Looking()) text += L"  ·  " + Tr(L"調べています…");
    w::Separator(c, info.x + 8, info.y, info.w - 16);
    float textRight = 12;
    if (thumbnails_ && info.w > 260) {
        // 絵の大きさ (左の端: サムネイルなし - 種類の絵)。
        constexpr float kSliderW = 96;
        const Rect slider{info.Right() - 12 - kSliderW, info.y + 2, kSliderW, info.h - 4};
        if (w::Slider(c, "asset_thumbnail_size", slider, thumbnailSize_, kMinThumbnail, kMaxThumbnail)) c.RequestRedraw();
        if (c.IsHovered(slider)) w::Tooltip(c, Tr(L"絵の大きさ (左の端: サムネイルなし)"));
        textRight += kSliderW + 12;
    }
    c.P().Text(info.Inset(12, 0, textRight, 0), text, Style(11, theme::TextFaint));
}

}  // namespace rm::editor
