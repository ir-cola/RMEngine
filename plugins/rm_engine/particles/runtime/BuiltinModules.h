#pragma once
// パーティクル - エンジンが持つモジュール (ユーザーのものと同じ .rmfx で書く: ModuleSource.h)。
// エフェクトは ID でモジュールを 1 つ指す (ParticleModule::builtin)。モジュールのタイトルや
// パラメーターの名前はここでは日本語で、エディターは Tr を通して見せる (プラグインの lang/en.json)。

#include "rm_engine/particles/runtime/ModuleSource.h"

#include <span>
#include <string_view>

namespace rm::particles {

struct BuiltinModule {
    std::string_view id;    // エフェクトに保存される: 出荷したら変えない
    std::string_view text;  // .rmfx の中身
};

std::span<const BuiltinModule> BuiltinModules();
// この ID のモジュールを解析したもの (解析は 1 度だけ); null: そのモジュールは無い。
const ModuleSource* FindBuiltin(std::string_view id);

}  // namespace rm::particles
