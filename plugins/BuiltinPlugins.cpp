// この exe に組み込むプラグイン (静的リンク: 設計 RME_PLATFORM 10 (a))。
//
// すべてのプラグインを知っている唯一のファイル。核は BuiltinPlugins() を宣言するだけで、
// プラグインを include しない。エディターの部分を持つプラグインがすべてここに並んでいるかは
// tools/check_layers.ps1 が調べる。
// 登録するものが何もないプラグイン (2D 空間: そのランタイムと、ほかのプラグインのエディターの
// ためのライブラリでしかないエディターの部分 - ギズモ。ファイルだけのテーマ集) もここに並べる。
// RegisterEditor は持たない - ほかと同じようにインストールでき、requires にも書ける。
// (3D 空間のエディターの部分もほぼライブラリで、登録するのは 3D モデルのサムネイルだけ。)
// 順番は関係ない: ローダーが requires を先に有効にする。

#include "editor/plugin/Software.h"
#include "rm_engine/animation/editor/Register.h"
#include "rm_engine/animation/runtime/Id.h"
#include "rm_engine/animation3d/editor/Register.h"
#include "rm_engine/animation3d/runtime/Id.h"
#include "rm_engine/audio/editor/Preview.h"
#include "rm_engine/audio/runtime/Id.h"
#include "rm_engine/particles/runtime/Id.h"
#include "rm_engine/game_x_particles/editor/Connector.h"
#include "rm_engine/game_x_particles/runtime/Id.h"
#include "rm_engine/particles_x_physics/runtime/Id.h"
#include "rm_engine/animation_x_physics/runtime/Id.h"
#include "rm_engine/weather/editor/Register.h"
#include "rm_engine/weather/runtime/Id.h"
#include "rm_engine/weather_x_physics/runtime/Id.h"
#include "rm_engine/climate/editor/Register.h"
#include "rm_engine/climate/runtime/Id.h"
#include "rm_engine/weather_x_climate/editor/Register.h"
#include "rm_engine/weather_x_climate/runtime/Id.h"
#include "rm_engine/game_production/editor/GameSoftware.h"
#include "rm_engine/game_production/runtime/Id.h"
#include "rm_engine/game_x_3d/editor/Connector.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/game_x_animation/editor/Connector.h"
#include "rm_engine/game_x_animation/runtime/Id.h"
#include "rm_engine/game_x_2d/editor/Connector.h"
#include "rm_engine/game_x_navigation/editor/Connector.h"
#include "rm_engine/game_x_navigation/runtime/Id.h"
#include "rm_engine/navigation/runtime/Id.h"
#include "rm_engine/navigation_x_3d/runtime/Id.h"
#include "rm_engine/navigation_x_animation/runtime/Id.h"
#include "rm_engine/navigation_x_physics/runtime/Id.h"
#include "rm_engine/game_x_2d/runtime/Id.h"
#include "rm_engine/physics/editor/Connector.h"
#include "rm_engine/physics/runtime/Id.h"
#include "rm_engine/game_x_audio/editor/Connector.h"
#include "rm_engine/game_x_audio/runtime/Id.h"
#include "rm_engine/game_ui/editor/Connector.h"
#include "rm_engine/game_ui/runtime/Id.h"
#include "rm_engine/space2d/runtime/Id.h"
#include "rm_engine/space3d/editor/Thumbnail.h"
#include "rm_engine/space3d/runtime/Id.h"
#include "rm_engine/theme_colors/runtime/Id.h"
#include "rm_engine/theme_explore/editor/Explore.h"
#include "rm_engine/theme_explore/runtime/Id.h"
#include "rm_engine/theme_gaming/editor/Gaming.h"
#include "rm_engine/theme_gaming/runtime/Id.h"
#include "rm_engine/theme_paper/runtime/Id.h"
#include "rm_engine/theme_seasons/editor/Seasons.h"
#include "rm_engine/theme_seasons/runtime/Id.h"

namespace rm::editor {

std::span<const BuiltinPlugin> BuiltinPlugins() {
    static constexpr BuiltinPlugin kPlugins[] = {
        {game_production::kPluginId, &game_production::RegisterEditor},
        {audio::kPluginId, &audio::RegisterEditor},
        {particles::kPluginId, nullptr},
        {game_x_particles::kPluginId, &game_x_particles::RegisterEditor},
        {particles_x_physics::kPluginId, nullptr},
        {weather::kPluginId, &weather::RegisterEditor},
        {weather_x_physics::kPluginId, nullptr},
        {climate::kPluginId, &climate::RegisterEditor},
        {weather_x_climate::kPluginId, &weather_x_climate::RegisterEditor},
        {space3d::kPluginId, &space3d::RegisterEditor},
        {animation::kPluginId, &animation::RegisterEditor},  // 状態とルール (.rmanim) とその計算 (3D も 2D も知らない)
        {animation3d::kPluginId, &animation3d::RegisterEditor},  // 姿勢の計算 (ランタイム) と、モデルのクリップの帯 (エディター)
        {game_x_3d::kPluginId, &game_x_3d::RegisterEditor},
        {game_x_animation::kPluginId, &game_x_animation::RegisterEditor},
        {animation_x_physics::kPluginId, nullptr},  // ルートモーションを物理のキャラクターの速さに (ランタイムだけ)
        {space2d::kPluginId, nullptr},
        {game_x_2d::kPluginId, &game_x_2d::RegisterEditor},
        {physics::kPluginId, &physics::RegisterEditor},
        {game_x_audio::kPluginId, &game_x_audio::RegisterEditor},
        {game_ui::kPluginId, &game_ui::RegisterEditor},
        {navigation::kPluginId, nullptr},  // 焼く・道を探す (Recast / Detour を包む。3D もゲーム制作も知らない)
        {game_x_navigation::kPluginId, &game_x_navigation::RegisterEditor},
        {navigation_x_3d::kPluginId, nullptr},
        {navigation_x_physics::kPluginId, nullptr},
        {navigation_x_animation::kPluginId, nullptr},
        {theme_colors::kPluginId, nullptr},  // テーマのファイルだけ (themes/*.json)
        {theme_paper::kPluginId, nullptr},
        {theme_seasons::kPluginId, &theme_seasons::RegisterEditor},
        {theme_explore::kPluginId, &theme_explore::RegisterEditor},  // 宇宙・深海・火山 (テーマのファイルと絵)
        {theme_gaming::kPluginId, &theme_gaming::RegisterEditor},
    };
    return kPlugins;
}

}  // namespace rm::editor
