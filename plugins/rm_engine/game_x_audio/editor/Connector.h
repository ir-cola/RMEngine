#pragma once
// The editor part of ゲーム制作 × 音:
//   the 階層's picture of an entity with a 音源 ("audio")
//   baking for the exported game (game_production's ExportBaker): a sound goes in as its file with its
//   import settings in front (runtime/SoundPack.h: Baked/<id>.rmsound) - never decoded or made again.
//   A sound only Media Foundation reads (MP3 / AAC / FLAC ...) is told in the export's log: a PC without
//   it (the N editions of Windows) plays the game without that sound.
//   An asset that is a sound brings this plugin (and 音) into the game: a script may play it with no
//   音源 in any scene.

#include "editor/plugin/Software.h"

namespace rm::game_x_audio {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_audio
