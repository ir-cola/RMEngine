#pragma once
// 3D アニメーション - モデルのインスペクターの「ブレンドシェイプ」のつまみで試す重み (docs/ANIMATION.md 7-2-2。見本 S1 + S2)。
// つまみはデータではない (Undo に積まない・保存しない) ので、ここに覚えておくだけ。止まっているシーンビューは、選んだ
// エンティティのモデルの重みにこれを上書きして見せる (ゲーム制作 × アニメーション の AnimatorPoser)。ゲームでは空のまま。
// 1 つのスレッド (UI) だけから使う。

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rm::animation3d {

using MorphTry = std::vector<std::pair<std::string, float>>;  // 形の名前と重み

// モデル (アセットの id) の試している重み。無い: null。
const MorphTry* TriedMorphs(std::string_view model);
// 形 `name` を `weight` で試す (負の数: やめる)。
void TryMorph(std::string_view model, std::string_view name, float weight);
// そのモデルの試すのを全部やめる。
void ClearTriedMorphs(std::string_view model);
// 試す重みが変わるたびに増える (絵を描き直す・影を描き直すため)。
uint64_t TriedMorphsVersion();

}  // namespace rm::animation3d
