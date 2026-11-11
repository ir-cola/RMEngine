#pragma once
#include "platform/Win.h"

#include <filesystem>
#include <string>

namespace rm::platform {

// A file that only one process can have open at a time - "this project is open in an editor".
// Taking it creates the file (hidden) and keeps it open with no sharing; letting go deletes it.
// When the process ends without letting go (a crash, Task Manager), Windows closes the handle and
// the file goes too (FILE_FLAG_DELETE_ON_CLOSE), so a lock is never left behind.
class FileLock {
public:
    enum class Result { Taken, HeldByOther, Failed };
    FileLock() = default;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    ~FileLock() { Release(); }
    Result Take(const std::filesystem::path& path, std::string* error);
    void Release();
    bool Held() const { return handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

}  // namespace rm::platform
