// Tests for the project lock (platform/FileLock): only one holder at a time, gone when let go.
// Uses a real file in the temp folder (two FileLocks in one process act like two RM Engines).

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
        CHECK(!fs::exists(file, ec));  // let go: the file is gone
        CHECK(second.Take(file, &err) == FileLock::Result::Taken);
    }  // destroyed while held: let go too
    CHECK(!fs::exists(file, ec));
    FileLock nowhere;
    CHECK(nowhere.Take(dir / L"no_such_folder" / L"x.lock", nullptr) == FileLock::Result::Failed);
    fs::remove_all(dir, ec);
}
