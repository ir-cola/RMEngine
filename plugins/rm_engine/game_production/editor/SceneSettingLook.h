#pragma once
// シーン設定のエディター側 (型と、ゲームにとっての意味はランタイム側のもの: SceneSettingInfo、
// runtime/Components.h): インスペクターのそのブロックに出るメニュー。設定を出すプラグインが、
// 自分の RegisterEditor から足す。
//
//   static const game_production::SceneSettingLook kSkyLook{.type = &reflect::TypeOf<Sky>, .menu = &SkyMenu};
//   registry.Contribute(game_production::kSceneSettingLookPoint, kSkyLook);
//
// 見た目 (Look) のない設定は、そのまま (フィールドだけ) 出る。編集は他のフィールドと同じく履歴に入る。

#include "core/reflect/Data.h"
#include "editor/plugin/Software.h"
#include "editor/widgets/Widgets.h"

#include <functional>
#include <string>
#include <vector>

namespace rm::game_production {

// PluginRegistry の口: 中身は SceneSettingLook (static)。
inline constexpr char kSceneSettingLookPoint[] = "rm_engine.game_production.scene_setting_look";

// メニューの行ができること。行にコピーして持たせる (行はメニューが閉じた後に動くため)。
struct SceneSettingMenuHost {
    editor::SoftwareHost* host = nullptr;
    // Play 中: 出ている設定は Play 中のコピー。それを変える行はグレーにする。
    bool play = false;
    // 開いているシーンの設定を変える: `change` に設定 (この Look の型の値) が渡り、どのフィールドも
    // 変えてよい。Undo は `label` (UTF-8、日本語) という名前の 1 件。
    std::function<void(std::string label, std::function<void(void* setting)> change)> edit;
    // 同じことを、Play 中は Play 中のコピーにも、すぐ行う (Undo には積まない: ■ で元に戻る)。
    std::function<void(std::string label, std::function<void(void* setting)> change)> apply;
};

struct SceneSettingLook {
    const reflect::TypeDesc& (*type)();
    // ブロックの ⋮ メニューの行 (空なら、メニューなし)。
    std::vector<editor::w::Action> (*menu)(const SceneSettingMenuHost& host) = nullptr;
    // できあいの値をまとめて設定する (天候のテンプレート): フィールドの上に presetLabel() という名前の
    // 行と、この行のドロップダウンが出る (各行は host.apply で設定を変える)。空なら、行なし。
    std::vector<editor::w::Action> (*presets)(const SceneSettingMenuHost& host) = nullptr;
    std::wstring (*presetLabel)() = nullptr;
};

}  // namespace rm::game_production
