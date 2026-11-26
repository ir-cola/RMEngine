#pragma once
// アニメーション - ルールの並びを調べる (docs/ANIMATION.md 4-7)。純粋 (描画もファイルもシーンもない)。
// tests/TestAnimationRules.cpp でテストする。
//
// 毎フレーム:
//   1. 今の状態の出口で、調べる行を決める (自由: 全部 / 自分のルールだけ: 元がこの状態の行と ⚡ の行 /
//      終わるまで自分のルールだけ: 終わるまでは自分のルールだけ、終わったら全部)
//   2. 上から調べ、元が合って条件が真の最初の 1 行で決まる。どれも当たらなければそのまま
//   3. 先が今の状態と同じなら何もしない (行の「やり直す」があれば始めから)
//   4. 当たった行が使ったトリガーを消す。使われなかったトリガーは kTriggerHold 秒で消える
//   5. 1 フレームに移るのは 1 回まで

#include "rm_engine/animation/runtime/Condition.h"
#include "rm_engine/animation/runtime/Controller.h"

#include <string>
#include <string_view>
#include <vector>

namespace rm::animation {

inline constexpr float kTriggerHold = 0.2f;  // 使われなかったトリガーが消えるまで (秒。押しっぱなしの先行入力が残らない)

// 読み込んだときに 1 回、名前を番号にし、式を解いたもの。
struct CompiledRule {
    int from = -1;  // 元の状態 (-1: どこでも)
    int to = -1;    // 先の状態 (-1: 無い状態の名前 - 当たらない)
    float fade = 0;
    bool interrupt = false, restart = false;
    Condition condition;
    std::string problem;  // 当たらない理由 (式の間違い・無い状態・知らない名前)。空: 無し
};
struct CompiledController {
    std::vector<AnimParam> params;
    std::vector<AnimState> states;
    std::vector<CompiledRule> rules;
    int FindState(std::string_view name) const;
    int FindParam(std::string_view name) const;
};
CompiledController Compile(const AnimController& c);

// パラメーターの今の値 (CompiledController::params と同じ並び)。トリガーは立っている間 1。
struct ParamValues {
    std::vector<float> values;
    std::vector<float> age;  // トリガーが立ってからの秒 (立っていない: -1)
};
ParamValues StartValues(const CompiledController& c);
// 名前のパラメーターに入れる (真偽は 0 / 1、整数は丸める、トリガーは 0 でなければ立てる)。無い名前: false。
bool SetParam(ParamValues& v, const CompiledController& c, std::string_view name, float value);
bool SetTrigger(ParamValues& v, const CompiledController& c, std::string_view name);
// 名前のパラメーターの値。無い名前: 0。
float ParamOf(const ParamValues& v, const CompiledController& c, std::string_view name);

// 今の状態の中のこと (条件の t と ended)。
struct StateNow {
    int state = 0;
    float t = 0;         // 0..1 (クリップの長さに対する割合)
    bool ended = false;  // 1 回のクリップが終わった・ループが 1 周した
};

// 行ごとの調べた結果 (タブで光らせる・理由を出す)。
enum class RuleCheck {
    NotHere,   // 元が今の状態ではない
    Blocked,   // 今の状態の出口が「自分のルールだけ」で、この行は調べない
    False,     // 条件が偽
    Chosen,    // これで決まった
    Below,     // 上の行で決まったので調べていない
    Broken,    // 当たらない (式の間違い・無い状態)
};

struct RuleStep {
    int rule = -1;   // 当たった行 (-1: どれも当たらない)
    int to = -1;     // 移る先 (-1: 移らない)
    float fade = 0;
    bool restart = false;  // 同じ状態を始めから
};
// 1 フレーム (dt 秒)。トリガーを古くし、当たった行のトリガーを消す。`checks`: 行ごとの結果 (null: 要らない)。
RuleStep StepRules(const CompiledController& c, ParamValues& v, const StateNow& now, float dt, std::vector<RuleCheck>* checks = nullptr);

}  // namespace rm::animation
