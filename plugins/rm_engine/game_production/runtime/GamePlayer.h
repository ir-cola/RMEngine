#pragma once
// ゲーム制作が作る、書き出したゲーム (game/Player.h: 枠はソフトを知らない)。ランタイム側
// (runtime/Register.cpp) から rm::game::kGamePoint に出す。
//
// .rmpak から読むもの (書き出しが書く: editor/GameExport.cpp):
//   game.json   { "kind": "rm_engine.game", "format": 1, "title": "My Game",
//                 "settings": { … GameSettings … },
//                 "scenes": ["Assets/Scenes/Main.rmscene", …], "open": 0,
//                 "assets": { "<asset id>": "Assets/…" } }          ゲームが使うすべてのアセットを id で
//   シーンファイルとアセットが、指定のパスにある (どれもゲーム自身のアセットのフォルダ)
//
// 1 フレーム: スクリプト (exe の隣の Scripts.dll: その入力と Tick)、次にシステムとその固定ステップ
// (SystemRunner)、そしてゲームの空間の描画担当による描画 (GameDraw.h)。

#include "base/Json.h"

namespace rm::game_production {

inline constexpr char kGameDataPath[] = "game.json";
inline constexpr char kGameDataKind[] = "rm_engine.game";
inline constexpr int64_t kGameDataFormat = 1;

}  // namespace rm::game_production
