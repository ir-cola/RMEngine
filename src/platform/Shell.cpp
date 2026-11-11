#include "platform/Shell.h"

#include "base/Utf.h"

#include <bcrypt.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <vector>

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

void OpenFolder(const std::filesystem::path& folder) {
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

namespace {

// Told by the shell before each item is deleted. When it would not go to the Recycle Bin (the flag
// TSF_DELETE_RECYCLE_IF_POSSIBLE is missing: no bin on that drive, or the item is bigger than the
// bin), the deletion is cancelled - with FOF_NOCONFIRMATION the shell would otherwise delete it for
// good without asking.
class RecycleOnly final : public IFileOperationProgressSink {
public:
    bool refused = false;

    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == __uuidof(IFileOperationProgressSink)) {
            *out = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { return --refs_; }  // lives on the caller's stack

    IFACEMETHODIMP PreDeleteItem(DWORD flags, IShellItem*) override {
        if (flags & TSF_DELETE_RECYCLE_IF_POSSIBLE) return S_OK;
        refused = true;
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    IFACEMETHODIMP StartOperations() override { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    IFACEMETHODIMP ResetTimer() override { return S_OK; }
    IFACEMETHODIMP PauseTimer() override { return S_OK; }
    IFACEMETHODIMP ResumeTimer() override { return S_OK; }

private:
    ULONG refs_ = 1;
};

}  // namespace

bool MoveToRecycleBin(std::span<const std::filesystem::path> paths, std::string* error, bool* notRecyclable) {
    if (notRecyclable) *notRecyclable = false;
    if (paths.empty()) return true;
    ComPtr<IFileOperation> op;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
    if (FAILED(hr)) {
        if (error) *error = ErrorText(static_cast<DWORD>(hr));
        return false;
    }
    // FOFX_RECYCLEONDELETE: to the Recycle Bin (Windows 8+). No questions, no window, no error dialog.
    op->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI);
    RecycleOnly sink;
    DWORD cookie = 0;
    const bool advised = SUCCEEDED(op->Advise(&sink, &cookie));
    if (!advised) {
        // Without the sink there is no way to stop a permanent deletion: do not delete.
        if (error) *error = "ごみ箱へ移すかどうかを確かめられないので、消していません";
        return false;
    }
    for (const auto& p : paths) {
        ComPtr<IShellItem> item;
        if (FAILED(SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(&item))) || FAILED(op->DeleteItem(item.Get(), nullptr))) {
            op->Unadvise(cookie);
            if (error) *error = "消す物を指せませんでした: " + WideToUtf8(p.native());
            return false;
        }
    }
    hr = op->PerformOperations();
    BOOL aborted = FALSE;
    op->GetAnyOperationsAborted(&aborted);
    op->Unadvise(cookie);
    if (sink.refused) {
        if (notRecyclable) *notRecyclable = true;
        if (error) *error = "ごみ箱に入れられない場所 (USB メモリ・ネットワーク・ごみ箱より大きい物) なので、消していません";
        return false;
    }
    if (FAILED(hr) || aborted) {
        if (error) *error = FAILED(hr) ? ErrorText(static_cast<DWORD>(hr)) : std::string("途中で止められました");
        return false;
    }
    return true;
}

FolderWatch::~FolderWatch() {
    if (handle_) FindCloseChangeNotification(handle_);
}

bool FolderWatch::Start(const std::filesystem::path& folder) {
    if (handle_) FindCloseChangeNotification(handle_);
    handle_ = FindFirstChangeNotificationW(folder.c_str(), TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
    if (handle_ == INVALID_HANDLE_VALUE) handle_ = nullptr;
    return handle_ != nullptr;
}

bool FolderWatch::Changed() {
    if (!handle_ || WaitForSingleObject(handle_, 0) != WAIT_OBJECT_0) return false;
    // Several changes in a row are one: wait for the next only after this one is seen.
    if (!FindNextChangeNotification(handle_)) {
        FindCloseChangeNotification(handle_);
        handle_ = nullptr;
    }
    return true;
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

std::filesystem::path ExeDirectory() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path EngineDirectory() {
    const std::filesystem::path exe = ExeDirectory();
    // Built from source: the exe is in <source>/build/bin/x64/<config>/. The source folder is the
    // one with RM_Engine.sln (a few levels up at most).
    std::error_code ec;
    std::filesystem::path dir = exe;
    for (int up = 0; up < 6 && !dir.empty(); ++up) {
        if (std::filesystem::exists(dir / L"RM_Engine.sln", ec)) return dir;
        if (dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    return exe;
}

bool SystemPrefersLight() {
    DWORD value = 0, size = sizeof(value);
    const LSTATUS r = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return r == ERROR_SUCCESS && value != 0;
}

std::optional<std::string> Sha256File(const std::filesystem::path& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::nullopt;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::optional<std::string> out;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<unsigned char> buf(1 << 16);
        DWORD got = 0;
        bool ok = true;
        while ((ok = ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) != 0) && got > 0) {
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buf.data(), got, 0))) {
                ok = false;
                break;
            }
        }
        unsigned char digest[32];
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            static const char* kHex = "0123456789abcdef";
            std::string hex;
            for (const unsigned char b : digest) {
                hex += kHex[b >> 4];
                hex += kHex[b & 15];
            }
            out = std::move(hex);
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return out;
}

}  // namespace rm::platform