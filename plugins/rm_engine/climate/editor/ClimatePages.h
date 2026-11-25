#pragma once
// 気候のタブの「天候」の側に並べるシーンの設定 (口は気候のもの)。気候は天候を知らないので、両方を
// 知るつなぎ (天候 × 気候) が、天候の設定をここに差し出す:
//
//   static const climate::ClimatePageInfo kPage{editor::TrKey(L"天候"), &reflect::TypeOf<weather::Weather>};
//   registry.Contribute(climate::kClimatePagePoint, kPage);
//
// タブはその型のシーンの設定を、インスペクターと同じ描き方で出す (ゲーム制作の SceneSettingLook の
// テンプレートの行も)。編集は Undo の 1 件。

#include "core/reflect/Reflect.h"

namespace rm::climate {

inline constexpr char kClimatePagePoint[] = "rm_engine.climate.tab_page";

struct ClimatePageInfo {
    const wchar_t* label = nullptr;                // 見出し (TrKey の文字列: 描くときに訳す)
    const reflect::TypeDesc& (*type)() = nullptr;  // シーンの設定の型 (game_production::SceneSettingInfo のもの)
};

}  // namespace rm::climate
