#pragma once
#include "platform/Win.h"

#include <filesystem>
#include <string>

namespace rm::platform {

// 同時に 1 つのプロセスしか開けないファイル - 「このプロジェクトはエディターで開いている」。
// 取ると、ファイルを (隠しで) 作り、共有なしで開いたままにする。手放すと消える。
// 手放さずにプロセスが終わったとき (クラッシュ、タスクマネージャー) は、Windows がハンドルを閉じて
// ファイルも消える (FILE_FLAG_DELETE_ON_CLOSE) ので、ロックが残ることは決してない。
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
