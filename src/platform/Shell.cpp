#include "platform/Shell.h"

#include "base/Utf.h"

#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace rm::platform {

namespace {

std::optional<std::wstring> ShowOpenDialog(HWND owner, FILEOPENDIALOGOPTIONS extra, std::wstring_view title,
                                           std::wstring_view initialFolder, const COMDLG_FILTERSPEC* filter) {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return std::nullopt;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | extra | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    const std::wstring t(title);
    dlg->SetTitle(t.c_str());
    if (filter) dlg->SetFileTypes(1, filter);
    if (!initialFolder.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(std::wstring(initialFolder).c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder.Get());
        }
    }
    if (FAILED(dlg->Show(owner))) return std::nullopt;  // includes "cancelled"
    ComPtr<IShellItem> result;
    if (FAILED(dlg->GetResult(&result))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return std::nullopt;
    std::wstring s = path;
    CoTaskMemFree(path);
    return s;
}

}  // namespace

std::optional<std::wstring> PickFolder(HWND owner, std::wstring_view title, std::wstring_view initialFolder) {
    return ShowOpenDialog(owner, FOS_PICKFOLDERS, title, initialFolder, nullptr);
}

std::optional<std::wstring> PickOpenFile(HWND owner, std::wstring_view title, std::wstring_view filterName,
                                         std::wstring_view filterSpec) {
    const std::wstring name(filterName), spec(filterSpec);
    const COMDLG_FILTERSPEC filter{name.c_str(), spec.c_str()};
    return ShowOpenDialog(owner, FOS_FILEMUSTEXIST, title, {}, &filter);
}

void ShowInExplorer(const std::filesystem::path& path) {
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
    if (!pidl) return;
    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
}

std::filesystem::path GetKnownFolder(KnownFolder which) {
    const KNOWNFOLDERID* id = &FOLDERID_Documents;
    if (which == KnownFolder::RoamingAppData) id = &FOLDERID_RoamingAppData;
    if (which == KnownFolder::LocalAppData) id = &FOLDERID_LocalAppData;
    PWSTR p = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &p)) && p) out = p;
    CoTaskMemFree(p);
    return out;
}

bool SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        if (void* dst = GlobalLock(mem)) {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

std::wstring GetClipboardText(HWND owner) {
    std::wstring out;
    if (!OpenClipboard(owner)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

std::string ErrorText(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring w = n && buf ? std::wstring(buf, n) : L"エラー " + std::to_wstring(code);
    if (buf) LocalFree(buf);
    while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' ')) w.pop_back();
    return WideToUtf8(w);
}

std::optional<std::string> ReadFileBytes(const std::filesystem::path& path, std::string* error) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        if (error) *error = ErrorText(GetLastError());
        return std::nullopt;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart > (256ll << 20)) {
        CloseHandle(f);
        if (error) *error = "ファイルが大きすぎます";
        return std::nullopt;
    }
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = data.empty() || ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    const DWORD err = GetLastError();
    CloseHandle(f);
    if (!ok || read != data.size()) {
        if (error) *error = ErrorText(err);
        return std::nullopt;
    }
    return data;
}

bool WriteFileAtomic(const std::filesystem::path& path, std::string_view data, std::string* error) {
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        if (error) *error = ErrorText(GetLastError());
        return false;
    }
    DWORD written = 0;
    BOOL ok = data.empty() || WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    ok = ok && written == data.size() && FlushFileBuffers(f);
    DWORD err = GetLastError();
    CloseHandle(f);
    if (ok) {
        ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        err = GetLastError();
    }
    if (!ok) {
        DeleteFileW(tmp.c_str());
        if (error) *error = ErrorText(err);
    }
    return ok != FALSE;
}

double NowSeconds() {
    static const double freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / freq;
}

RECT VisibleWindowRect(HWND hwnd) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(hwnd, &r);
    return r;
}

HWND TopLevelWindowAt(POINT screen, HWND exclude) {
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT)) {
        if (h == exclude || !IsWindowVisible(h) || IsIconic(h)) continue;
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TRANSPARENT) continue;
        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        const RECT r = VisibleWindowRect(h);
        if (PtInRect(&r, screen)) return h;
    }
    return nullptr;
}

}  // namespace rm::platform
