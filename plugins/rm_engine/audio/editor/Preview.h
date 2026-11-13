#pragma once
// 音 - the editor part: sounds heard from the アセット panel (the core's editor::kAssetPreviewPoint).
//
//   describe   "0:12.5 ・ 44100 Hz ステレオ ・ Ogg Vorbis" - read on a thread of its own (once per file
//              and time it was changed): the panel shows "長さを調べています…" until then
//   play       the file with its import settings (音量・ループの点 - a preview loops only when the
//              loop points are set), on an AudioEngine of its own (not the game's)

#include "editor/plugin/Software.h"

namespace rm::audio {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::audio
