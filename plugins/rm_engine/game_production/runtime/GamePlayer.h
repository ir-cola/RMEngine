#pragma once
// The exported game as game production makes it (game/Player.h: the frame knows no software). Offered
// from the runtime part (runtime/Register.cpp) at rm::game::kGamePoint.
//
// What it reads from the .rmpak (written by the export: editor/GameExport.cpp):
//   game.json   { "kind": "rm_engine.game", "format": 1, "title": "My Game",
//                 "settings": { … GameSettings … },
//                 "scenes": ["Assets/Scenes/Main.rmscene", …], "open": 0,
//                 "assets": { "<asset id>": "Assets/…" } }          every asset the game uses, by id
//   the scene files and the assets, at the paths given (all of the game's own folder of assets)
//
// A frame: the scripts (Scripts.dll next to the exe: their input and Tick), then the systems with their
// fixed steps (SystemRunner), then the drawing by the drawer of the game's space (GameDraw.h).

#include "base/Json.h"

namespace rm::game_production {

inline constexpr char kGameDataPath[] = "game.json";
inline constexpr char kGameDataKind[] = "rm_engine.game";
inline constexpr int64_t kGameDataFormat = 1;

}  // namespace rm::game_production
