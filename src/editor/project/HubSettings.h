#pragma once
// Hub state shared by every project: the recent-project list and the last folder
// used for "new project". Stored in %APPDATA%\RM_Engine\hub.json (unknown keys kept).

#include "base/Json.h"

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

    std::filesystem::path LastLocation() const;
    void SetLastLocation(const std::filesystem::path& folder);

    static std::filesystem::path SettingsDir();

private:
    json::Value doc_ = json::Value::MakeObject();
    std::vector<RecentProject> recent_;
};

// Windows paths compare case-insensitively.
bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b);

}  // namespace rm::editor
