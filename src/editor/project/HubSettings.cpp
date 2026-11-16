#include "editor/project/HubSettings.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/app/Paths.h"
#include "editor/plugin/PluginManifest.h"
#include "platform/Shell.h"

#include <algorithm>
#include <chrono>
#include <cwctype>

namespace fs = std::filesystem;

namespace rm::editor {

namespace {

constexpr size_t kMaxRecent = 50;

std::wstring Normalized(const fs::path& p) {
    std::wstring s = p.lexically_normal().native();
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

int64_t NowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

bool SamePath(const fs::path& a, const fs::path& b) { return Normalized(a) == Normalized(b); }

void HubSettings::Load() {
    recent_.clear();
    doc_ = json::Value::MakeObject();
    options_ = {};
    optionsUnknowns_ = {};
    const fs::path file = paths::HubSettingsFile();
    std::error_code ec;
    if (!fs::exists(file, ec)) return;
    std::string err;
    auto bytes = platform::ReadFileBytes(file, &err);
    if (!bytes) {
        log::Warn("Hub の設定を読めませんでした: {}", err);
        return;
    }
    json::ParseError pe;
    auto doc = json::Parse(*bytes, &pe);
    if (!doc || !doc->IsObject()) {
        log::Warn("Hub の設定が壊れているので無視します ({} 行 {} 文字目: {})", pe.line, pe.column, pe.message);
        return;
    }
    doc_ = std::move(*doc);
    if (doc_.Get("options").IsObject()) {
        reflect::Warnings warnings;
        reflect::Load(options_, doc_.Get("options"), &optionsUnknowns_, &warnings);
        for (const auto& w : warnings) log::Warn("オプション: {}", w);
    }
    for (const auto& item : doc_.Get("recent").Items()) {
        const std::string& path = item.Get("path").AsString();
        if (path.empty()) continue;
        recent_.push_back({PathFromUtf8(path), item.Get("lastOpened").AsInt(0)});
    }
}

bool HubSettings::Save() {
    // Another RM Engine open at the same time may have opened projects since this one read the file:
    // those stay in the list (newest first), unless this one took them out.
    {
        std::string err;
        const auto bytes = platform::ReadFileBytes(paths::HubSettingsFile(), &err);
        const auto doc = bytes ? json::Parse(*bytes) : std::nullopt;
        if (doc && doc->IsObject()) {
            for (const auto& item : doc->Get("recent").Items()) {
                const std::string& path = item.Get("path").AsString();
                if (path.empty()) continue;
                const fs::path file = PathFromUtf8(path);
                auto same = [&](const fs::path& f) { return SamePath(f, file); };
                const auto mine = std::find_if(recent_.begin(), recent_.end(), [&](const RecentProject& r) { return same(r.file); });
                if (mine != recent_.end()) {
                    mine->lastOpened = std::max(mine->lastOpened, item.Get("lastOpened").AsInt(0));
                } else if (std::none_of(forgotten_.begin(), forgotten_.end(), same)) {
                    recent_.push_back({file, item.Get("lastOpened").AsInt(0)});
                }
            }
            std::stable_sort(recent_.begin(), recent_.end(), [](const RecentProject& a, const RecentProject& b) { return a.lastOpened > b.lastOpened; });
            if (recent_.size() > kMaxRecent) recent_.resize(kMaxRecent);
        }
    }
    json::Value list = json::Value::MakeArray();
    for (const auto& r : recent_) {
        json::Value item = json::Value::MakeObject();
        item.Set("path", PathToUtf8(r.file));
        item.Set("lastOpened", r.lastOpened);
        list.Push(std::move(item));
    }
    doc_.Set("version", 1);
    doc_.Set("recent", std::move(list));
    doc_.Set("options", reflect::Save(options_, &optionsUnknowns_));

    const fs::path file = paths::HubSettingsFile();
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::string err;
    if (!platform::WriteFileAtomic(file, json::Write(doc_), &err)) {
        log::Warn("Hub の設定を保存できませんでした: {}", err);
        return false;
    }
    return true;
}

void HubSettings::Touch(const fs::path& file) {
    std::erase_if(recent_, [&](const RecentProject& r) { return SamePath(r.file, file); });
    std::erase_if(forgotten_, [&](const fs::path& f) { return SamePath(f, file); });
    recent_.insert(recent_.begin(), {file, NowUnix()});
    if (recent_.size() > kMaxRecent) recent_.resize(kMaxRecent);
}

void HubSettings::Remove(const fs::path& file) {
    std::erase_if(recent_, [&](const RecentProject& r) { return SamePath(r.file, file); });
    forgotten_.push_back(file);
}

void HubSettings::Replace(const fs::path& from, const fs::path& to) {
    // An old row for the new path (a project that used to be there) would be a duplicate.
    if (!SamePath(from, to)) std::erase_if(recent_, [&](const RecentProject& r) { return SamePath(r.file, to); });
    for (auto& r : recent_) {
        if (SamePath(r.file, from)) r.file = to;
    }
    if (!SamePath(from, to)) forgotten_.push_back(from);
}

bool HubSettings::LimitedNoticeHidden(const std::vector<PluginId>& limited) const {
    auto hidden = ParsePluginIdList(doc_.Get("hiddenPluginNotice"), nullptr);
    if (!hidden || hidden->size() != limited.size()) return false;
    for (const PluginId& id : limited) {
        if (std::find(hidden->begin(), hidden->end(), id) == hidden->end()) return false;
    }
    return true;
}

bool HubSettings::HasHiddenNotice() const { return doc_.Get("hiddenPluginNotice").Size() > 0; }

void HubSettings::ShowNoticesAgain() { doc_.Erase("hiddenPluginNotice"); }

void HubSettings::HideLimitedNotice(const std::vector<PluginId>& limited) {
    json::Value list = json::Value::MakeArray();
    for (const PluginId& id : limited) {
        json::Value item = json::Value::MakeObject();
        item.Set("author", id.author);
        item.Set("name", id.name);
        list.Push(std::move(item));
    }
    doc_.Set("hiddenPluginNotice", std::move(list));
}

}  // namespace rm::editor
