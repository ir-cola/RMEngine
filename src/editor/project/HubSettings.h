#pragma once
// 全プロジェクトで共有する Hub の状態: 最近のプロジェクトの一覧と、非表示にしたプラグインの通知。
// %APPDATA%\RM_Engine\hub.json に保存する (知らないキーは残す - 古い "lastLocation" は使わずに残る)。
// 新しいプロジェクトは、覚えたフォルダーではなく、標準で <engine>/Project に入る (platform::EngineDirectory)。

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
    int64_t lastOpened = 0;      // unix 秒
};

class HubSettings {
public:
    void Load();
    bool Save();

    const std::vector<RecentProject>& Recent() const { return recent_; }
    // 別の RM Engine がその後ファイルにしたこと (開いたプロジェクトが入り、外したものが消える) を
    // 反映した一覧 - 書き込みはしない。Save が最初にするほか、Hub は一覧を出すときにする。
    void SyncRecent();
    void Touch(const std::filesystem::path& file);  // 先頭に移し、lastOpened = 今 にする
    void Remove(const std::filesystem::path& file);
    // プロジェクトが移った (名前が変わった): 一覧の同じ場所、同じ lastOpened。
    void Replace(const std::filesystem::path& from, const std::filesystem::path& to);

    // 前提が無いプラグインについての通知の「変わるまで二度と表示しない」
    // (設計 RME_PLATFORM 1-b-2): そういうプラグインの組が同じ間だけ非表示にする。
    bool LimitedNoticeHidden(const std::vector<PluginId>& limited) const;
    void HideLimitedNotice(const std::vector<PluginId>& limited);
    bool HasHiddenNotice() const;
    void ShowNoticesAgain();  // forgets "この状態が変わるまで表示しない"

    // オプション (hub.json の "options"。新しい版のキーは残す)。
    Options& GetOptions() { return options_; }
    reflect::Unknowns& OptionsUnknowns() { return optionsUnknowns_; }


private:
    json::Value doc_ = json::Value::MakeObject();
    std::vector<RecentProject> recent_;
    // ユーザーが一覧から外したもの (削除、名前の変更の古いパス): Save でファイルから戻さない。
    std::vector<std::filesystem::path> forgotten_;
    // これが最後に読んだか書いたときにファイルにあった一覧と、その後にこれが開いたもの:
    // その間にファイルから消えた行は別の RM Engine が外したので、Save でも外したまま
    // (2026-11-18 画面で: 古い一覧を出したままの Hub が、プロジェクトを開いたときにそれを戻していた)。
    std::vector<std::filesystem::path> onDisk_;
    std::vector<std::filesystem::path> opened_;
    Options options_;
    reflect::Unknowns optionsUnknowns_;
};

// Windows のパスは大文字小文字を区別せずに比べる。
bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b);

}  // namespace rm::editor
