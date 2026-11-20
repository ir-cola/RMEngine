#pragma once
// ゲーム制作 × 音 のランタイムの部分 (plugins/RuntimePlugins.cpp に並べる。書き出したゲームは
// RM_RUNTIME_ENTRY で呼ぶ): 部品 音源 (AudioSource.h)、Play の音 (AudioService.h: game
// production の PlayService)、スクリプトの部分 (sdk/RMAudio.h)。

#include "core/RuntimeRegistry.h"

namespace rm::game_x_audio {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_audio
