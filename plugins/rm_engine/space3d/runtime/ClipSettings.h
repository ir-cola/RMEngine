#pragma once
// 3D 空間 - モデルの .meta の取り込みの設定に書いた、クリップごとの設定 (docs/ANIMATION.md 3-1・3-6)。
// 今はイベントだけ。形 (知らないキーは残す。編集はアニメーションのプラグインのインスペクター):
//
//   "import": { ...,
//     "clips": { "<クリップの名前>": { "events": [ { "time": 0.5, "name": "Footstep", "number": 0, "text": "" } ] } } }
//
// 読む (LoadMeshFile) ときに MeshData::clips に入れる。焼いたモデル (.rmmesh の EVNT) にも入るので、
// ゲームは .meta を読まない。純粋。tests/TestSkeleton.cpp でテストする。

#include "base/Json.h"
#include "rm_engine/space3d/runtime/Skeleton.h"

#include <vector>

namespace rm::space3d {

// 1 つのクリップのイベントの上限 (壊れた / 大きすぎる .meta で膨らまないように)。
inline constexpr size_t kMaxClipEvents = 1024;

// "import" の中のキー。
inline constexpr char kClipsKey[] = "clips";

// イベントの時刻を [0, duration] に (数でない時刻・負の長さは 0)。読むときもエディターで置くときも。
float ClampEventTime(float time, float duration);

// `import` (.meta の "import") の "clips" を、名前の同じクリップに入れる (それまでの events は置き換える)。
// イベントは ClipEvent の欄の決まりで読み (無い欄は既定の値)、時刻を収め、時刻の順に並べる (同じ時刻は
// 書いた順)。オブジェクトでないもの・名前が空のものは捨てる。名前の同じクリップが 2 つあれば両方に。
void ApplyClipSettings(std::vector<Clip>& clips, const json::Value& import);

}  // namespace rm::space3d
