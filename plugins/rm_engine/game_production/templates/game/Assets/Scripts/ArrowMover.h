// 見本のスクリプト: 矢印キー (WASD・ゲームパッドでも) で動く。エンティティのインスペクターの「＋ 部品を足す」→
// 「矢印キーで動く」で付けて、▶ (Ctrl+P) で Play し、矢印キーを押してください。キーはゲームの設定の「入力」で変えられます。
// A sample script: moves with the arrow keys (WASD, a game pad). Add it to an entity ("＋ 部品を足す" ->
// "矢印キーで動く"), press Play and use the arrow keys. The keys are the game settings' 入力 (actions by name).
#pragma once

#include "RM.h"

RM_SCRIPT(Label="矢印キーで動く")
struct ArrowMover {
    RM_PROPERTY(Edit, Label="速さ (1 秒に)", Min=0) float speed = 3.0f;
    RM_PROPERTY(Edit, Label="Jump で回す (度 / 秒)") float spin = 180.0f;
    RM_TRANSIENT() int frames = 0;

    void BeginPlay(rm::Entity self);
    void Tick(rm::Entity self, float dt);
    void EndPlay(rm::Entity self);
};
