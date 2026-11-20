#pragma once
// ゲーム制作 × 音 のエディターの部分:
//   音源 ("audio") を持つエンティティの階層の絵
//   書き出すゲームのためのベイク (game_production の ExportBaker): 音は、取り込みの設定を
//   前に付けたファイルとして入れる (runtime/SoundPack.h: Baked/<id>.rmsound)。デコードも
//   作り直しもしない。
//   Media Foundation でしか読めない音 (MP3 / AAC / FLAC など) は書き出しのログで知らせる:
//   それがない PC (Windows の N エディション) では、その音だけなしでゲームが動く。
//   音であるアセットは、このプラグイン (と 音) をゲームに連れてくる: どのシーンにも
//   音源がなくても、スクリプトが鳴らせる。

#include "editor/plugin/Software.h"

namespace rm::game_x_audio {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_audio
