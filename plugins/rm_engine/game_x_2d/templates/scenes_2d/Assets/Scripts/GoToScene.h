// 見本のスクリプト: Enter でシーンを切り替える (タイトル → ステージ → タイトル)。
// 「切り替え先のシーン」はシーンの名前 (Assets/Scenes のファイルの名前。.rmscene は付けない)。シーンは
// 「シーンの一覧」パネルの一覧にあるものだけに切り替えられます。
// A sample script: Enter switches the scene (Title -> Stage -> Title). "切り替え先のシーン" is a scene's
// name (its file's, without .rmscene); only the scenes in the シーンの一覧 panel's list can be switched to.
#pragma once

#include "RM.h"

#include <string>

RM_SCRIPT(Label="シーンを切り替える")
struct GoToScene {
    RM_PROPERTY(Edit, Label="切り替え先のシーン", Tooltip="シーンの名前 (ファイルの名前。.rmscene は付けない)") std::string next = "Stage";
    RM_PROPERTY(Edit, Label="自動で切り替える (秒)", Tooltip="0 なら Enter を押したときだけ切り替えます", Min=0) float after = 0;
    RM_TRANSIENT() float time = 0;

    void BeginPlay(rm::Entity self);
    void Tick(rm::Entity self, float dt);
    void EndPlay(rm::Entity self);
};
