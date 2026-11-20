#include "platform/FileLock.h"

#include "base/Utf.h"

namespace rm::platform {

namespace {

std::string Message(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring w = text ? text : L"";
    if (text) LocalFree(text);
    while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ')) w.pop_back();
    return WideToUtf8(w) + " (" + std::to_string(code) + ")";
}

}  // namespace

FileLock::Result FileLock::Take(const std::filesystem::path& path, std::string* error) {
    Release();
    // 共有なし: 同じファイルへの 2 回目の CreateFile (別の RM Engine) は共有違反で失敗する。
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle_ != INVALID_HANDLE_VALUE) return Result::Taken;
    const DWORD code = GetLastError();
    if (error) *error = Message(code);
    if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) return Result::HeldByOther;
    return Result::Failed;
}

void FileLock::Release() {
    if (handle_ == INVALID_HANDLE_VALUE) return;
    CloseHandle(handle_);  // ファイルを消す (FILE_FLAG_DELETE_ON_CLOSE)
    handle_ = INVALID_HANDLE_VALUE;
}

}  // namespace rm::platform
