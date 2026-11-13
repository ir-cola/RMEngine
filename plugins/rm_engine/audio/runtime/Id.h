#pragma once
// The ID of the sound plugin (音) - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::audio {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "audio"};

}  // namespace rm::audio
