#pragma once
// ゲーム制作 × 2D - スプライトのアニメーション: ゲームが動く間、エンティティのスプライトの コマ を
// 1 秒のコマ数 で コマの並び に沿って進める (スプライトの画像をセルに切ったもの: Sprite.h)。
// 純粋 (I/O も描画もしない)。tests/TestSpace2d.cpp でテストする。

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include <vector>

namespace rm::game_x_2d {

RM_STRUCT(Label="スプライトのアニメーション")
struct SpriteAnimation {
    RM_PROPERTY(Edit, Label="コマの並び", Tooltip="順に見せるコマの番号。空なら、スプライトの全部のコマを 0 から順に")
    std::vector<int> frames;
    RM_PROPERTY(Edit, Label="1 秒のコマ数", Tooltip="速さ。0 で止まる", Min=0, Max=240) float framesPerSecond = 8.f;
    RM_PROPERTY(Edit, Label="繰り返す", Tooltip="外すと、最後のコマで止まる") bool loop = true;
    RM_TRANSIENT() double time = 0;  // 再生した秒数 (動いているコピーだけ)
    // スクリプトが頼んだこと (RM2D.h の rm::SpriteAnimation): どのステップから再生するか、止めておく、止める。
    RM_TRANSIENT() int startStep = 0;
    RM_TRANSIENT() bool paused = false;
    RM_TRANSIENT() bool stopped = false;
};

// スクリプトの部分 (RM2D.h: rm::SpriteAnimation): 再生、一時停止、停止、速さ、フレーム。
const game_production::ScriptApiPart& ScriptSpriteAnimationPart();

// システム (Frame): すべての スプライトのアニメーション が進んで、エンティティのスプライトの コマ を決める。
void RunSpriteAnimation(game_production::SystemContext& ctx);

}  // namespace rm::game_x_2d
