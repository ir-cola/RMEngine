#pragma once
// The runtime part of ゲーム制作 × 音 (listed in plugins/RuntimePlugins.cpp; the exported game calls it
// by RM_RUNTIME_ENTRY): the component 音源 (AudioSource.h), the sounds of a Play (AudioService.h: a
// game production PlayService) and the scripts' part (sdk/RMAudio.h).

#include "core/RuntimeRegistry.h"

namespace rm::game_x_audio {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_audio
