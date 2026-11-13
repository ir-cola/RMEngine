#pragma once
// The exported game's frame (design NewEngineBluePrint 11-b / 11-b-2): <ゲーム>.exe is this, the
// libraries of the core and of the plugins it uses, and a small generated main (editor/export:
// GameMainSource) that lists those plugins. Knows no software and no plugin: the plugins register into
// a RuntimeRegistry (their runtime parts, in the listed order), and the one that makes the game -
// ゲーム制作 - offers it at kGamePoint. What is drawn and how the game moves is that plugin's.
//
//   <exe folder>/
//     <ゲーム>.exe        the game (no editor in it; no .pdb)
//     Game.rmpak          its assets (core/pak: packed and scrambled; the same build number as the exe)
//     Scripts.dll         the game's C++ scripts, when it has some
//     vcruntime140.dll …  the C++ runtime (Visual Studio's redistributable files: a PC without it plays)
//
// Every path the game reads is of its own folder (never one of the PC it was made on).
// `<ゲーム>.exe --smoke` draws a few frames and closes with exit code 0 (the export's check); with
// any problem it closes with 1 and writes why into game.log next to the exe.

#include "base/Math.h"
#include "core/RuntimeRegistry.h"
#include "platform/Gfx.h"

#include <d2d1_1.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// The generated main's (the same declarations are written into it - editor/export/ExportPlan.cpp).
extern "C" {
struct RmLinkedPlugin {
    const char* author;
    const char* name;
    void (*registerRuntime)(rm::RuntimeRegistry&);  // null: the plugin registers nothing
};
struct RmGameImage {
    const char* title;          // UTF-8: the window's title until the game says another
    unsigned long long build;   // the export's number (the .rmpak must have the same)
    const RmLinkedPlugin* plugins;
    unsigned long long count;
};
const RmGameImage* rm_game_image();
}

namespace rm::game {

inline constexpr wchar_t kPakName[] = L"Game.rmpak";
inline constexpr wchar_t kScriptsDll[] = L"Scripts.dll";
inline constexpr wchar_t kLogName[] = L"game.log";

// What the frame gives the game.
class Host {
public:
    virtual ~Host() = default;
    // A file of the .rmpak ("game.json", "Assets/…"); nullopt and *error (Japanese) when it is not there.
    virtual std::optional<std::string> Read(std::string_view path, std::string* error) const = 0;
    virtual const RuntimeRegistry& Registry() const = 0;
    virtual platform::Gfx& Graphics() = 0;
    virtual std::filesystem::path Folder() const = 0;  // the exe's folder (Scripts.dll)
    virtual void Log(int level, std::string_view textUtf8) = 0;  // 0 info / 1 warning / 2 error
    virtual void SetTitle(std::string_view titleUtf8) = 0;
    // The mouse on the window's client area (DIPs, as Draw's size); nullopt when it is outside it or the
    // game is not in front.
    virtual std::optional<Vec2> Pointer() const { return std::nullopt; }
    // How often the left button went down / up on the window since the last call (its messages - a click
    // shorter than a frame has both), and back to 0.
    struct PointerPresses {
        int down = 0, up = 0;
    };
    virtual PointerPresses TakePointerPresses() { return {}; }
};

// One game (made by the plugin that offers it).
class Game {
public:
    virtual ~Game() = default;
    // Reads its data (before the window shows). False and *error (Japanese) stops the game.
    virtual bool Load(std::string* error) = 0;
    // How the game wants its window (the game settings' 画面).
    struct Display {
        int width = 1280, height = 720;  // DIPs of the client area, in a window
        // Fullscreen is shown as a borderless window over the whole monitor (★ not the exclusive mode).
        enum class Mode { Windowed, Fullscreen, Borderless } mode = Mode::Windowed;
        bool vsync = true;   // in step with the screen's refresh
        int targetFps = 60;  // without vsync: at most this many frames a second
    };
    virtual Display GetDisplay() const = 0;
    // One frame: the input read, the systems (and their fixed steps), the scripts.
    virtual void Update(double dt) = 0;
    // Draws into the window (the whole of it).
    virtual void Draw(ID2D1DeviceContext* dc, float widthDip, float heightDip) = 0;
    // The game ends (the window closes).
    virtual void Stop() {}
};

// RuntimeRegistry point: items are GameInfo (static). The first offered makes the game.
inline constexpr char kGamePoint[] = "rm.game.maker";
struct GameInfo {
    std::unique_ptr<Game> (*create)(Host& host);
};

// The whole game: the frame, the registry, the .rmpak and the loop. The exit code.
int Run(const RmGameImage& image, bool smoke);

}  // namespace rm::game
