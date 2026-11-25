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
#include "core/reflect/Reflect.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rm::animation3d {

// イベント 1 つ。保存・読み込み・インスペクターの欄はこの定義から (憲法 9)。
RM_STRUCT(Label="イベント")
struct ClipEvent {
    RM_PROPERTY(Edit, Label="時刻 (秒)", Tooltip="クリップの始めからの秒。再生がここを通るとイベントが出ます", Min=0, Step=0.01)
    float time = 0;
    RM_PROPERTY(Edit, Label="名前", Tooltip="スクリプトの OnEvent に届く名前 (足音なら Footstep など)")
    std::string name = "Event";
    RM_PROPERTY(Edit, Label="数値", Tooltip="名前と一緒に届く数") float number = 0;
    RM_PROPERTY(Edit, Label="文字", Tooltip="名前と一緒に届く文字") std::string text;
};

inline constexpr char kClipsKey[] = "clips";  // "import" の中のキー

// 時刻を [0, duration] に (数でない・負の長さは 0)。
float ClampEventTime(float time, float duration);

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

}  // namespace rm::animation3d
