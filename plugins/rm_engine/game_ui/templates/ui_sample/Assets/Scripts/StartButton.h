// 見本のスクリプト: ボタンを押すとシーンを切り替える (このスクリプトはボタンのエンティティに付けます)。
// A sample script: pressing the button switches the scene (put it on the button's entity).
#pragma once

#include "RM.h"

#include <string>

RM_SCRIPT(Label="シーンへ進むボタン")
struct StartButton {
    RM_PROPERTY(Edit, Label="切り替え先のシーン", Tooltip="シーンの名前 (Assets/Scenes のファイルの名前。.rmscene は付けない)") std::string next = "Game";
    RM_PROPERTY(Edit, Label="自動で押す (秒)", Tooltip="0 なら押したときだけ。書き出しの確かめ用", Min=0) float after = 0;
    RM_TRANSIENT() float time = 0;

    void BeginPlay(rm::Entity self);
    void Tick(rm::Entity self, float dt);
    // ボタンが押された (マウス・Enter・パッドの A)。ゲーム UI のボタンが、同じエンティティのスクリプトに呼びます。
    void OnClick(rm::Entity self);
};
