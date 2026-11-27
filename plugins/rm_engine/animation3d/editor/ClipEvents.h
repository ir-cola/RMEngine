#pragma once
// 3D アニメーション - モデルのクリップに置くイベント (docs/ANIMATION.md 3-6) を、モデルの .meta の取り込みの
// 設定の中で足す・動かす・変える・消す純粋な計算 (ディスクも画面も使わない。テスト: tests/TestClipEvents.cpp)。
//
//   .meta → "import": {
//      ...モデルの取り込みの設定...,
//      "clips": {
//         "<クリップの名前 (モデルのファイルが付けたもの)>": {
//            "events": [ { "time": 0.5, "name": "Footstep", "number": 0, "text": "" }, ... ]
//         }
//      }
//   }
//
// 形はイベントを読んで出すランタイムの側と決めたもの (変えない)。time は秒、number は数、name / text は
// UTF-8 の文字。
//   - 知らないキーは "clips" の中・クリップの中・イベントの中のどこでも残す (後で足す ループ・範囲 など。憲法 6)
//   - 書くとき、イベントは時刻の順 (同じ時刻は前の順のまま)
//   - 最後のイベントを消すと "events" を消し、空になったクリップと "clips" も消す (null を返す = キーを消す)
//   - 時刻は [0, クリップの長さ] に収める
// 関数はどれも "import" の "clips" の値を受け取り、変えた後の値を返す (null: "clips" を消す)。

#include "base/Json.h"
#include "rm_engine/space3d/runtime/ClipSettings.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rm::animation3d {

// イベント 1 つの型・"import" の中のキー・時刻を収める式は、ゲームが読むときと同じもの (3D 空間の Skeleton.h /
// ClipSettings.h。定義は 1 か所: 憲法 1)。
using space3d::ClampEventTime;
using space3d::ClipEvent;
using space3d::kClipsKey;

// クリップ `clip` のイベント (ファイルの並びのまま。書いた物は時刻の順)。無ければ空。
std::vector<ClipEvent> ReadClipEvents(const json::Value& clips, std::string_view clip);

// `e` を足す (時刻は収める)。*index: 並べ直した後のその番号。
json::Value AddClipEvent(const json::Value& clips, std::string_view clip, const ClipEvent& e, float duration, size_t* index = nullptr);
// 番号 `index` のイベントの知っている欄を `e` に (知らないキーはそのまま)。*newIndex: 並べ直した後の番号。
// 番号が無ければそのまま返す。
json::Value SetClipEvent(const json::Value& clips, std::string_view clip, size_t index, const ClipEvent& e, float duration,
                         size_t* newIndex = nullptr);
// 番号 `index` のイベントの時刻だけを変える (ドラッグ)。
json::Value MoveClipEvent(const json::Value& clips, std::string_view clip, size_t index, float time, float duration,
                          size_t* newIndex = nullptr);
// 番号 `index` のイベントを消す。
json::Value RemoveClipEvent(const json::Value& clips, std::string_view clip, size_t index);

// --- ルートモーション (docs/ANIMATION.md 7-1): クリップの "root" (ClipRootMotion の欄の決まり) ---
using space3d::ClipRootMotion;
// クリップ `clip` のルートモーションの設定 (無い: 既定 = 使わない)。
ClipRootMotion ReadClipRoot(const json::Value& clips, std::string_view clip);
// クリップ `clip` の "root" を `root` に (知らないキーは残す。既定の値で知らないキーも無ければ "root" を消し、
// 空になったクリップと "clips" も消す)。
json::Value SetClipRoot(const json::Value& clips, std::string_view clip, const ClipRootMotion& root);

// --- 書き出し (docs/ANIMATION.md 7-3): クリップの "bake" (ClipBake の欄の決まり: 圧縮・いつも入れる) ---
using space3d::ClipBake;
// クリップ `clip` の書き出しの設定 (無い: 既定 = 圧縮ふつう・いつも入れない)。
ClipBake ReadClipBake(const json::Value& clips, std::string_view clip);
// クリップ `clip` の "bake" を `bake` に (SetClipRoot と同じ決まり)。
json::Value SetClipBake(const json::Value& clips, std::string_view clip, const ClipBake& bake);

}  // namespace rm::animation3d
