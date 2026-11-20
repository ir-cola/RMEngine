#pragma once
// 音 - エディターの部分: アセット パネルで聴く音 (核の editor::kAssetPreviewPoint)。
//
//   describe   "0:12.5 ・ 44100 Hz ステレオ ・ Ogg Vorbis" - 専用のスレッドで読む (ファイルと
//              更新時刻ごとに 1 回): それまでパネルは "長さを調べています…" と出す
//   play       ファイルを取り込みの設定 (音量・ループの点 - プレビューがループするのは
//              ループの点が設定されているときだけ) つきで、専用の AudioEngine (ゲームのものではない) で鳴らす

#include "editor/plugin/Software.h"

namespace rm::audio {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::audio
