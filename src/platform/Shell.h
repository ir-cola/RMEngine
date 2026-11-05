#pragma once
// Windows shell services: file dialogs, Explorer, known folders, clipboard, files, time.
// File dialogs require COM in STA mode (MTA makes Show() wait forever - previous engine 4-2).

#include "platform/Win.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rm::platform {

std::optional<std::wstring> PickFolder(HWND owner, std::wstring_view title, std::wstring_view initialFolder);
// filterSpec example: L"*.rmpf"
std::optional<std::wstring> PickOpenFile(HWND owner, std::wstring_view title, std::wstring_view filterName,
                                         std::wstring_view filterSpec);

// Opens Explorer with the file (or folder) selected.
void ShowInExplorer(const std::filesystem::path& path);

enum class KnownFolder { Documents, RoamingAppData, LocalAppData };
std::filesystem::path GetKnownFolder(KnownFolder which);

bool SetClipboardText(HWND owner, const std::wstring& text);
std::wstring GetClipboardText(HWND owner);

// Text of a Win32 error code (UTF-8, no trailing newline).
std::string ErrorText(DWORD code);

// Reads a whole file. On failure returns nullopt and sets *error (UTF-8).
std::optional<std::string> ReadFileBytes(const std::filesystem::path& path, std::string* error);
// Writes to "<path>.tmp" first, then replaces the file in one step, so a crash
// in the middle never leaves a half-written project file.
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

// Seconds from an arbitrary start point (QueryPerformanceCounter).
double NowSeconds();

}  // namespace rm::platform
