#pragma once
// 今のエンジンで読むヘルプの本: 核の本と、入っていて使えるプラグインの本 (つなぎは元の本の下)。
// エディターのヘルプのパネルと Hub のヘルプの窓が、プラグインを入れた・消した・言語を変えたときに読み直す。

#include "editor/help/HelpLibrary.h"
#include "editor/plugin/Plugins.h"

namespace rm::editor {

help::Library LoadHelpLibrary(const Plugins& plugins);

}  // namespace rm::editor
