#pragma once
// The editor's options (Hub > オプション). Written once here: saving (hub.json "options"), the
// options page (drawn by the inspector from this description) and the defaults all come from it.

#include "core/reflect/Reflect.h"

namespace rm::editor {

RM_ENUM(Label="言語")
enum class UiLanguage {
    RM_ITEM(Label="日本語") Japanese,
    RM_ITEM(Label="English") English,
};

RM_ENUM(Label="自動保存の間隔")
enum class AutoSaveInterval {
    RM_ITEM(Label="1 分") Min1,
    RM_ITEM(Label="2 分") Min2,
    RM_ITEM(Label="3 分") Min3,
    RM_ITEM(Label="4 分") Min4,
    RM_ITEM(Label="5 分") Min5,
    RM_ITEM(Label="10 分") Min10,
    RM_ITEM(Label="30 分") Min30,
    RM_ITEM(Label="60 分") Min60,
};

RM_STRUCT(Label="オプション")
struct Options {
    // The id of one of theme::kThemes (chosen on its own screen, so not in the inspector). The ids are the
    // names of the old three-way choice, so a hub.json from before keeps its theme.
    RM_PROPERTY(Label="色のテーマ") std::string theme = "Dark";
    RM_PROPERTY(Edit, Label="言語") UiLanguage language = UiLanguage::Japanese;
    RM_PROPERTY(Edit, Label="自動保存の間隔", Tooltip="開いているプロジェクトに未保存の変更があれば、この間隔で保存します") AutoSaveInterval autoSave = AutoSaveInterval::Min5;
    RM_PROPERTY(Edit, Label="エディターの自動アップデート", Tooltip="新しい版を自動で入れます (配布の仕組みができたら効きます)") bool autoUpdate = true;
};

inline int Minutes(AutoSaveInterval a) {
    switch (a) {
        case AutoSaveInterval::Min1: return 1;
        case AutoSaveInterval::Min2: return 2;
        case AutoSaveInterval::Min3: return 3;
        case AutoSaveInterval::Min4: return 4;
        case AutoSaveInterval::Min5: return 5;
        case AutoSaveInterval::Min10: return 10;
        case AutoSaveInterval::Min30: return 30;
        case AutoSaveInterval::Min60: return 60;
    }
    return 5;
}

}  // namespace rm::editor
