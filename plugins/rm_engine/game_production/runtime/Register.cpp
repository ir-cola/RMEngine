#include "rm_engine/game_production/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/WorldSave.h"

namespace rm::game_production {

void RegisterRuntime(RuntimeRegistry& registry) {
    // 世界のセーブに入れる物の印 (rm::Save::WriteWorld。docs/SAVE.md)。
    static const ComponentInfo kSaveable{.type = &reflect::TypeOf<Saveable>, .folder = "ゲーム"};
    registry.Contribute(kComponentPoint, kSaveable);
}

}  // namespace rm::game_production
