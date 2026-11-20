#pragma once
// パーティクル - そこから始められる出来合いのエフェクト (炎・煙・火花 ...): 組み込みモジュールだけで
// 作るので、絵もファイルもいらない (前のエンジンの教訓: "○を放つだけで炎が作れない" - 炎は
// ワンクリックで作れなければならない)。パーティクルタブは、これらから新しいエフェクトを作るか、
// エフェクトにそのエミッターを足す。名前はここでは日本語で、エディターは Tr を通して見せる (プラグインの lang/en.json)。

#include "rm_engine/particles/runtime/ParticleEffect.h"

#include <span>
#include <string_view>

namespace rm::particles {

struct EffectPreset {
    std::string_view id;
    std::string_view title;  // 日本語
    ParticleEffect (*make)();
};

// 出す順番 (最初のもの: NewEffect のやわらかい光)。
std::span<const EffectPreset> EffectPresets();

}  // namespace rm::particles
