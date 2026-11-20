// プロジェクトのロック (platform/FileLock) のテスト: 同時に持てるのは 1 つだけ、手放すと消える。
// 一時フォルダの本物のファイルを使う (1 つのプロセスの中の 2 つの FileLock は、2 つの RM Engine のように振る舞う)。

#include "Test.h"

#include "platform/FileLock.h"

#include <filesystem>

using namespace rm;
using platform::FileLock;
namespace fs = std::filesystem;

RM_TEST(FileLock_OneHolderAtATime) {
    const fs::path dir = fs::temp_directory_path() / L"rm_lock_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / L"Game.rmpf.lock";
    {
        FileLock first, second;
        std::string err;
        CHECK(first.Take(file, &err) == FileLock::Result::Taken);
        CHECK(first.Held() && fs::exists(file, ec));
        CHECK(second.Take(file, &err) == FileLock::Result::HeldByOther);  // "別の RM Engine で開いています"
        CHECK(!second.Held() && !err.empty());
        first.Release();
        CHECK(!first.Held());
        CHECK(!fs::exists(file, ec));  // 手放した: ファイルは消えた
        CHECK(second.Take(file, &err) == FileLock::Result::Taken);
    }  // 持ったまま破棄された: これも手放す
    CHECK(!fs::exists(file, ec));
    FileLock nowhere;
    CHECK(nowhere.Take(dir / L"no_such_folder" / L"x.lock", nullptr) == FileLock::Result::Failed);
    fs::remove_all(dir, ec);
}
