#pragma once
// Windows shell services: file dialogs, Explorer, known folders, clipboard, files, time.
// File dialogs require COM in STA mode (MTA makes Show() wait forever - previous engine 4-2).

#include "platform/FileLock.h"
#include "platform/Win.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rm::platform {

// The dialog opens in `initialFolder` - or, when it is not there (yet), in the nearest folder above it
// that is (never in the one Windows remembers from before, unless none of them is there).
std::optional<std::wstring> PickFolder(HWND owner, std::wstring_view title, std::wstring_view initialFolder);
// `path` when it is a folder that is there, else the nearest folder above it that is; empty: none (a
// drive that is not there, an empty path).
std::filesystem::path ExistingFolderAtOrAbove(const std::filesystem::path& path);
// filterSpec example: L"*.rmpf"
std::optional<std::wstring> PickOpenFile(HWND owner, std::wstring_view title, std::wstring_view filterName,
                                         std::wstring_view filterSpec);

// Opens Explorer with the file (or folder) selected.
void ShowInExplorer(const std::filesystem::path& path);
// Opens a folder in Explorer (its contents, nothing selected).
void OpenFolder(const std::filesystem::path& folder);
// Moves files / folders to the Recycle Bin (no question asked, no progress window). NEVER deletes
// for good: where there is no Recycle Bin (a USB stick, a network folder) or it is too small for the
// item, the shell would delete it permanently - that item is refused and stays (*notRecyclable =
// true). False when any of them was not moved; *error (UTF-8) says why.
bool MoveToRecycleBin(std::span<const std::filesystem::path> paths, std::string* error, bool* notRecyclable = nullptr);

// Tells when something under a folder was added, removed or renamed (Windows' change notification) -
// and, with `contents`, when a file in it was written. Asked without waiting: Changed() is true once
// after each change (the .meta files the editor writes count too - looking again then finds nothing new).
// `onSignal`, when given, is called once when a change comes and nobody has asked yet - on a thread of
// the system's pool, so it must only wake the UI thread (editor::PostToUi), which then asks Changed().
// Without it, a change made while the editor is idle would wait for the next mouse move.
class FolderWatch {
public:
    FolderWatch() = default;
    FolderWatch(const FolderWatch&) = delete;
    FolderWatch& operator=(const FolderWatch&) = delete;
    ~FolderWatch();  // (waits for an onSignal still running)
    // false: the folder cannot be watched (Changed() stays false)
    bool Start(const std::filesystem::path& folder, bool contents = false, std::function<void()> onSignal = {});
    bool Changed();
    // The watch no longer sees the folder at its path: it stopped, or the folder was deleted / moved to
    // the Recycle Bin / made again (the watch follows the old one). The owner starts it again. Opens the
    // folder to ask - once a second at most, not every frame.
    bool Lost() const;

private:
    static void CALLBACK Signaled(void* self, BOOLEAN timedOut);
    void Arm();
    void Close();

    HANDLE handle_ = nullptr;
    HANDLE wait_ = nullptr;
    std::function<void()> onSignal_;
    std::filesystem::path folder_;
    std::optional<std::pair<DWORD, uint64_t>> identity_;  // the folder watched (see Lost)
};

enum class KnownFolder { Documents, RoamingAppData, LocalAppData };
std::filesystem::path GetKnownFolder(KnownFolder which);

bool SetClipboardText(HWND owner, const std::wstring& text);
std::wstring GetClipboardText(HWND owner);

// Text of a Win32 error code (UTF-8, no trailing newline).
std::string ErrorText(DWORD code);

// The largest file ReadFileBytes reads - and WriteFileAtomic writes: one limit for both, so nothing
// can be saved that cannot be opened again (a .rmpf over it used to save and then never open).
inline constexpr uint64_t kMaxFileBytes = 256ull << 20;  // 256 MB

// Reads a whole file (at most kMaxFileBytes). On failure returns nullopt and sets *error (UTF-8).
std::optional<std::string> ReadFileBytes(const std::filesystem::path& path, std::string* error);
// Writes to "<path>.tmp" first, then replaces the file in one step, so a crash
// in the middle never leaves a half-written project file. More than kMaxFileBytes is refused
// (nothing is written; the file stays as it was).
bool WriteFileAtomic(const std::filesystem::path& path, std::string_view data, std::string* error);

// The top-level window under a screen point (physical pixels), skipping `exclude`, hidden,
// minimized, click-through and cloaked windows. Used while dragging panels between windows.
HWND TopLevelWindowAt(POINT screen, HWND exclude = nullptr);
// The visible window rectangle (without the invisible resize borders of Windows 10/11).
RECT VisibleWindowRect(HWND hwnd);

// The folder of the running exe (the engine's own files - Plugins/ ... - are next to it).
std::filesystem::path ExeDirectory();
// The engine's folder: the source folder (the one with RM_Engine.sln) when the exe was built from
// it, otherwise the exe's folder. New projects go in its Project/ by default.
std::filesystem::path EngineDirectory();

// Windows' "app mode" is light (Settings > Personalization > Colors).
bool SystemPrefersLight();

// SHA-256 of a file as lower-case hex; nullopt when it cannot be read (missing ...).
std::optional<std::string> Sha256File(const std::filesystem::path& path);

// Seconds from an arbitrary start point (QueryPerformanceCounter).
double NowSeconds();

}  // namespace rm::platform
