// The exported game's entry: the generated main (editor/export) gives what it was exported with
// (rm_game_image); everything else is game/Player.cpp.

#include "game/Player.h"
#include "platform/Win.h"

#include <cwchar>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool smoke = cmdLine && std::wcsstr(cmdLine, L"--smoke") != nullptr;
    const int code = rm::game::Run(*rm_game_image(), smoke);
    if (SUCCEEDED(hr)) CoUninitialize();
    return code;
}
