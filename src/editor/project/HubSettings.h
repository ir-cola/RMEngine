#pragma once
// Hub state shared by every project: the recent-project list and the hidden plugin notice.
// Stored in %APPDATA%\RM_Engine\hub.json (unknown keys kept - an old "lastLocation" stays, unused).
// New projects go in <engine>/Project by default (platform::EngineDirectory), not a remembered folder.

#include "base/Json.h"
#include "core/PluginId.h"
#include "core/reflect/Data.h"
#include "editor/project/Options.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rm::editor {

struct RecentProject {
    std::filesystem::path file;  // .rmpf
    int64_t lastOpened = 0;      // unix seconds
};

class HubSettings {
public:
    void Load();
    bool Save();

    const std::vector<RecentProject>& Recent() const { return recent_; }
    void Touch(const std::filesystem::path& file);  // move to the top, set lastOpened = now
    void Remove(const std::filesystem::path& file);
    // The project moved (renamed): same place in the list, same lastOpened.
    void Replace(const std::filesystem::path& from, const std::filesystem::path& to);

    // "Do not show again until this changes" of the notice about plugins that lack a requirement
    // (design RME_PLATFORM 1-b-2): hidden only while the set of such plugins stays the same.
    bool LimitedNoticeHidden(const std::vector<PluginId>& limited) const;
    void HideLimitedNotice(const std::vector<PluginId>& limited);
    bool HasHiddenNotice() const;
    void ShowNoticesAgain();  // forgets "この状態が変わるまで表示しない"

    // The options (hub.json "options"; keys of a newer version are kept).
    Options& GetOptions() { return options_; }
    reflect::Unknowns& OptionsUnknowns() { return optionsUnknowns_; }


private:
    json::Value doc_ = json::Value::MakeObject();
    std::vector<RecentProject> recent_;
    Options options_;
    reflect::Unknowns optionsUnknowns_;
};

// Windows paths compare case-insensitively.
bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b);

}  // namespace rm::editor
