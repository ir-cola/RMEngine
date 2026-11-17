#pragma once
// ゲーム制作 × 3D - the scene's 空 made ready for 3D 空間's renderer (RenderFrame::sky): one painter
// for every picture that shows the sky (the scene view, the game view and the camera preview in the
// editor; the exported game), so the sky looks the same in all of them. Where the pictures and the
// user's .rmsky come from differs (the editor's files / the game's .rmpak): the caller says (Sources).
//
//   SkyPainter painter;                                   // one per picture's owner
//   const SkyLook look = LookOf(*sky, seconds);
//   if (const space3d::SkyDraw* d = painter.Prepare(renderer, *sky, look, sources)) frame.sky = d;
//   FollowSky(look, lights, ambient);                    // 光を空に合わせる

#include "base/Ref.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

namespace rm::game_x_3d {

class SkyPainter {
public:
    struct Sources {
        // A picture on the GPU (a colour picture: read as linear light); null while it is not ready
        // or cannot be read (the caller tells why).
        std::function<space3d::TextureRef(const AssetRef&)> picture;
        // A .rmsky file's text; nullopt when it is not there.
        std::function<std::optional<std::string>(const AssetRef&)> code;
        // Its name for the messages (the file's name).
        std::function<std::string(const AssetRef&)> name;
        // How often the .rmsky is read again (seconds; its edits show without reopening). < 0: once.
        double readEvery = 0.25;
        double now = 0;  // seconds, any start (for readEvery)
    };

    // What to draw behind everything; null: the clear colour (なし, or the sky cannot be drawn: its
    // shader does not compile - told once on the console - or its .rmsky is missing).
    const space3d::SkyDraw* Prepare(space3d::Renderer& renderer, const Sky& sky, const SkyLook& look, const Sources& sources);

private:
    std::string Code(const Sky& sky, const SkyLook& look, const Sources& sources, std::string& fileName);

    space3d::SkyDraw draw_;
    std::string compiled_;  // the HLSL draw_.program was made from
    bool failed_ = false;   // compiled_ did not compile: not tried again until the code changes
    // The user's .rmsky as read last, and when.
    std::string codeGuid_;
    std::optional<std::string> code_;
    double codeReadAt_ = -1e9;
    std::unordered_set<std::string> told_;  // messages given once
};

}  // namespace rm::game_x_3d
