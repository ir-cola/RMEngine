#pragma once
// The ID of the basic color theme pack (青 / 緑): only theme files - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::theme_colors {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "theme_colors"};

}  // namespace rm::theme_colors
