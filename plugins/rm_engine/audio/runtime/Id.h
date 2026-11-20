#pragma once
// 音 (サウンド プラグイン) の ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::audio {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "audio"};

}  // namespace rm::audio
