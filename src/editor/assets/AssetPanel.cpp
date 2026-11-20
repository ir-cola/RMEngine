#include "editor/assets/AssetPanel.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
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

float AssetPanel::RowHeight() const {
    return thumbnails_ && thumbnailSize_ >= kThumbnailsFrom ? std::max(24.f, thumbnailSize_ + 6.f) : 24.f;
}

bool AssetPanel::DrawPicture(ui::Context& c, const tree::Item& item, const Rect& r) {
    if (!thumbnails_ || thumbnailSize_ < kThumbnailsFrom) return false;
    const auto it = guids_.find(item.id);
    if (it == guids_.end()) return false;
    ID2D1Bitmap* bitmap = AssetThumbnail(c, lib_, *thumbnails_, it->second, r.w);
    if (!bitmap) return false;
    w::DrawThumbnail(c, r, bitmap, item.dimmed);
    return true;
}

void AssetPanel::SetPreviews(const PluginRegistry* registry, std::function<void()> redraw) {
    registry_ = registry;
    described_ = redraw ? std::make_unique<UiWake>(std::move(redraw)) : nullptr;
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
    if (!thumbnails.Has(kind)) return nullptr;
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
    (void)index;  // フォルダーは中身を名前で並べる: どこに入るかだけが大事
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
        if (!made.empty()) tree::RequestSelect(state_, {asset::NumberOf(made)}, true);  // 名前を打つ
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
    if (registry_ && guids.size() == 1) {
        // 音: ここで聞く (音プラグインのプレビュー)。
        if (const asset::Entry* e = lib_.Index().ByGuid(guids[0])) {
            for (tree::Action& a : PreviewActions(lib_, PreviewFor(*registry_, asset::KindOf(e->path, e->folder)), guids[0])) list.push_back(std::move(a));
        }
    }
    // アセットの詳細と取り込みの設定をインスペクターに出す (選んだときの出し方と同じ - その後
    // インスペクターが別の物を出していても)。
    tree::Action details;
    details.label = Tr(L"詳しく見る (インスペクター)");
    details.enabled = guids.size() == 1 && static_cast<bool>(inspect_);
    details.run = [this] { InspectSelected(); };
    list.push_back(std::move(details));
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

void AssetPanel::SetInspector(std::function<void(Inspectable)> inspect, std::function<void(std::unique_ptr<ICommand>)> push) {
    inspect_ = std::move(inspect);
    push_ = std::move(push);
}

void AssetPanel::SelectionChanged(std::span<const tree::ItemId> ids) {
    if (ids.size() == 1) pendingInspect_ = true;  // ボタンを離したときに出す (ドラッグの後ではない)
}

void AssetPanel::InspectSelected() {
    const std::vector<std::string> sel = GuidsOf(state_.sel.ids);
    if (sel.size() != 1 || !inspect_) return;
    const asset::Entry* e = lib_.Index().ByGuid(sel[0]);
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
    std::function<void()> known = described_ ? described_->Signal() : std::function<void()>();
    target.blocks = [lib, view, guid, type, push, preview, known, facts] {
        std::vector<InspectBlock> blocks;
        const asset::Entry* now = lib->Index().ByGuid(guid);
        if (!now) return blocks;  // 削除された: 何も出さない
        InspectBlock info;
        info.note = KindLabel(asset::KindOf(now->path, now->folder)) + L"  ·  Assets/" + Utf8ToWide(now->path) + L"\nID " + Utf8ToWide(guid);
        if (!facts.empty()) info.note += L"\n" + facts;
        if (preview && preview->describe) {
            // (音: 長さと形式 - 「鳴らしてみる」はブロックの ⋮ メニューにある)
            if (const std::wstring line = preview->describe(lib->FullPath(*now), known); !line.empty()) info.note += L"\n" + line;
            info.title = KindLabel(asset::KindOf(now->path, now->folder));
            info.menu = PreviewActions(*lib, preview, guid);
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

void AssetPanel::Draw(ui::Context& c, const Rect& r) {
    if (lib_.Poll()) c.RequestRedraw();  // ディスクで変わった (エクスプローラーなど)
    Rect area = r;
    const Rect info = ui::TakeBottom(area, 24);
    tree::Draw(c, state_, *this, area);
    // アセットを 1 つクリックするとインスペクターに出す - ボタンを離してから、ドラッグの後ではない
    // (ドラッグは、インスペクターが今出している物の欄へ持って行くところかもしれない)。
    if (state_.dragging) pendingInspect_ = false;
    if (pendingInspect_ && !c.In().down[0]) {
        pendingInspect_ = false;
        InspectSelected();
    }

    // ツリーの下: 選んだ物が何で、どこにあるか。
    std::wstring text;
    const std::vector<std::string> sel = GuidsOf(state_.sel.ids);
    if (sel.size() == 1) {
        if (const asset::Entry* e = lib_.Index().ByGuid(sel[0])) {
            const asset::Kind kind = asset::KindOf(e->path, e->folder);
            text = KindLabel(kind) + L"  ·  Assets/" + Utf8ToWide(e->path);
            const AssetPreview* preview = registry_ ? PreviewFor(*registry_, kind) : nullptr;
            if (preview && preview->describe) {
                const std::function<void()> known = described_ ? described_->Signal() : std::function<void()>();
                if (const std::wstring line = preview->describe(lib_.FullPath(*e), known); !line.empty()) text += L"  ·  " + line;
            }
        }
    } else if (sel.size() > 1) {
        text = TrF(L"{} 個選んでいます", sel.size());
    } else {
        text = TrF(L"{} 個のファイルとフォルダ", lib_.Index().Entries().size());
    }
    // フォルダーを調べている (Worker で): 終わるまでは、最後の調べの結果を出す。
    if (lib_.Looking()) text += L"  ·  " + Tr(L"調べています…");
    w::Separator(c, info.x + 8, info.y, info.w - 16);
    float textRight = 12;
    if (thumbnails_ && info.w > 260) {
        // サムネイルの大きさ (左の端: なし - 種類の絵を、小さな行で)。
        constexpr float kSliderW = 96;
        const Rect slider{info.Right() - 12 - kSliderW, info.y + 2, kSliderW, info.h - 4};
        if (w::Slider(c, "asset_thumbnail_size", slider, thumbnailSize_, kMinThumbnail, kMaxThumbnail)) c.RequestRedraw();
        if (c.IsHovered(slider)) w::Tooltip(c, Tr(L"サムネイルの大きさ (左の端: サムネイルなし)"));
        textRight += kSliderW + 12;
    }
    c.P().Text(info.Inset(12, 0, textRight, 0), text, Style(11, theme::TextFaint));
}

}  // namespace rm::editor
