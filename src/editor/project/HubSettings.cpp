#include "editor/project/HubSettings.h"

#include "base/Log.h"
#include "base/Utf.h"
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

fs::path HubSettings::SettingsDir() { return platform::GetKnownFolder(platform::KnownFolder::RoamingAppData) / L"RM_Engine"; }

void HubSettings::Load() {
    recent_.clear();
    doc_ = json::Value::MakeObject();
    const fs::path file = SettingsDir() / L"hub.json";
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
    for (const auto& item : doc_.Get("recent").Items()) {
        const std::string& path = item.Get("path").AsString();
        if (path.empty()) continue;
        recent_.push_back({PathFromUtf8(path), item.Get("lastOpened").AsInt(0)});
    }
}

bool HubSettings::Save() {
    json::Value list = json::Value::MakeArray();
    for (const auto& r : recent_) {
        json::Value item = json::Value::MakeObject();
        item.Set("path", PathToUtf8(r.file));
        item.Set("lastOpened", r.lastOpened);
        list.Push(std::move(item));
    }
    doc_.Set("version", 1);
    doc_.Set("recent", std::move(list));

    const fs::path dir = SettingsDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string err;
    if (!platform::WriteFileAtomic(dir / L"hub.json", json::Write(doc_), &err)) {
        log::Warn("Hub の設定を保存できませんでした: {}", err);
        return false;
    }
    return true;
}

void HubSettings::Touch(const fs::path& file) {
    Remove(file);
    recent_.insert(recent_.begin(), {file, NowUnix()});
    if (recent_.size() > kMaxRecent) recent_.resize(kMaxRecent);
}

void HubSettings::Remove(const fs::path& file) {
    std::erase_if(recent_, [&](const RecentProject& r) { return SamePath(r.file, file); });
}

fs::path HubSettings::LastLocation() const { return PathFromUtf8(doc_.Get("lastLocation").AsString()); }

void HubSettings::SetLastLocation(const fs::path& folder) { doc_.Set("lastLocation", PathToUtf8(folder)); }

}  // namespace rm::editor
