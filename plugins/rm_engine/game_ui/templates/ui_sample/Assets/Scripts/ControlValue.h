// 見本のスクリプト: 入力の部品 (入力欄・スライダー・トグル) の値をコンソールに出す。始めに 1 回と、
// 変わったとき (OnEvent の rm::UI::kChanged) と、入力欄で Enter を押したとき (rm::UI::kSubmitted)。
// このスクリプトは入力の部品のエンティティに付けます。
// A sample script: logs the value of an input part (input field / slider / toggle) at the start, when it
// changes and when Enter is pressed in an input field. Put it on the part's entity.
#pragma once

#include "RM.h"

RM_SCRIPT(Label="入力の部品の値を出す")
struct ControlValue {
    void BeginPlay(rm::Entity self);
    void OnEvent(rm::Entity self, const rm::Event& e);
};
