#pragma once
// The ID of the seasons theme pack (spring / summer / fall / winter): theme files and the code of their pictures - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::theme_seasons {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "theme_seasons"};

}  // namespace rm::theme_seasons
