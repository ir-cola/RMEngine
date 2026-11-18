#pragma once
// ゲーム制作 × 2D - スプライトのアニメーション: while the game plays, steps the コマ of the entity's sprite
// through コマの並び at 1 秒のコマ数 (the sprite's image cut into cells: Sprite.h). Pure - tested in
// tests/TestSpace2d.cpp.

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
    RM_TRANSIENT() double time = 0;  // seconds played (only in the copy that plays)
    // What scripts asked (RM2D.h's rm::SpriteAnimation): from which step it plays, held still, stopped.
    RM_TRANSIENT() int startStep = 0;
    RM_TRANSIENT() bool paused = false;
    RM_TRANSIENT() bool stopped = false;
};

// The scripts' part (RM2D.h: rm::SpriteAnimation): play, pause, stop, the speed, the frames.
const game_production::ScriptApiPart& ScriptSpriteAnimationPart();

// The system (Frame): every スプライトのアニメーション moves on and sets its entity's sprite's コマ.
void RunSpriteAnimation(game_production::SystemContext& ctx);

}  // namespace rm::game_x_2d
