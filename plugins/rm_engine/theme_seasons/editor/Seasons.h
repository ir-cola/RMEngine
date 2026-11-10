#pragma once
// 季節テーマパック: the four seasons (スプリング / サマー / フォール / ウィンター). Their colors are theme
// files (themes/*.json, read by the core like every pack's); their pictures are this code, offered to
// the core at w::kOrnamentPoint under the names the theme files use ("spring" ...). The core draws
// them behind the content: on the window's background and again on each panel and bar.
//
//   spring  a cherry tree growing up on the right, in bloom up top, petals blowing off it, a blossom
//           (bottom-left), a flurry of petals (top-left)
//   summer  a mound of sand with a parasol and a sand castle (bottom-right), clouds (top-right), the
//           sun (bottom-left)
//   fall    leaves thick along the top and scattered below, a big maple leaf (bottom-left)
//   winter  falling snow (held still), icicles from the top edge, snow piled at the bottom of every
//           panel, a snowflake (bottom-left)
// A panel window of its own gets a lighter picture: spring's petals, summer's clouds, winter's snow.

#include "editor/plugin/Software.h"

namespace rm::theme_seasons {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::theme_seasons
