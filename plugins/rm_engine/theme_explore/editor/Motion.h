#pragma once
// 探検テーマパック - 動く絵の粒の動き (宇宙の星と流れ星、深海の泡・マリンスノー・生き物の光、火山の火の粉)。
// 純粋: 時刻 `t` (秒) と番号から、その時の場所・大きさ・明るさを計算するだけ (tests/TestTheme.cpp でテストする)。
// 粒は覚えておかない: 毎フレーム、番号と決まった種から作り直す (何フレーム飛ばしても、同じ時刻なら同じ絵)。
// 画面の端から出た粒は反対の端に回り込む (Wrap)、または一生を終えて生まれ直す (LifeAt)。
// 描くのは Scenes.cpp (粒を w::BackdropSprite にして、GPU の動く絵に足す)。

#include "ui/Types.h"

#include <cstdint>

namespace rm::theme_explore::motion {

// --- 共通 ---------------------------------------------------------------------------------------
// (seed, index, salt) から決まる 0 .. 1 の数 (何度呼んでも同じ)。
float Rand(uint32_t seed, uint32_t index, uint32_t salt = 0);
// v を [lo, hi) に回り込ませる (hi <= lo なら lo)。
float Wrap(float v, float lo, float hi);
// またたき: 1 - depth .. 1 を行き来する明るさの倍率 (いくつかの波を重ねて、規則正しく見えないように)。
float Twinkle(double t, float phase, float speed, float depth);
// ちらつき (火): 0 .. 1。速い波の重なり。
float Flicker(double t, float phase);
// ゆらぎ: -1 .. 1 のなめらかな揺れ (2 つの波の重なり)。
float Sway(double t, float phase, float speed);
// 一生を繰り返す粒: 周期 `period` 秒、ずれ `offset` (0 .. 1)。age = 今の一生の 0 .. 1、cycle = 何回目の一生か
// (生まれ直すたびに場所を変えるのに使う)。
struct Life {
    float age = 0;
    uint32_t cycle = 0;
};
Life LifeAt(double t, float period, float offset);

// --- 宇宙 ---------------------------------------------------------------------------------------
// 星: 3 つの深さ (0 = 遠い: 小さく暗く遅い、2 = 近い)。ゆっくり左下へ流れ、端で回り込む。
struct Star {
    Vec2 at;
    float radius = 0;
    float brightness = 0;   // 0 .. 1 (またたきを掛けた後)
    float temperature = 0;  // 0 = 青白い .. 1 = 赤っぽい (星の色)
    int layer = 0;
};
Star StarAt(const ui::Rect& window, uint32_t index, double t, uint32_t seed);
// 星の色 (温度 0 .. 1 → 青白い → 白 → 黄 → 橙)。
Color StarColor(float temperature);
// 流れ星: 時間を `slot` 秒ずつに分け、それぞれの枠でたまに 1 つ (見えていれば visible)。
struct Meteor {
    bool visible = false;
    Vec2 head;
    Vec2 tail;  // 頭から尾の先へのずれ
    float brightness = 0;
};
Meteor MeteorAt(const ui::Rect& window, double t, uint32_t seed, float slot = 9.f);

// --- 深海 ---------------------------------------------------------------------------------------
// 泡: 海の底のいくつかの噴き出し口から上る (左右に揺れ、上るほど少し大きく、上の端で消える)。
struct Bubble {
    Vec2 at;
    float radius = 0;
    float alpha = 0;  // 0 .. 1
};
Bubble BubbleAt(const ui::Rect& window, uint32_t index, double t, uint32_t seed);
// 漂う小さな粒: マリンスノー (ゆっくり沈む、揺れる) と、生き物の光 (ゆっくり漂い、明滅する)。
struct Mote {
    Vec2 at;
    float radius = 0;
    float alpha = 0;  // 0 .. 1
};
Mote SnowAt(const ui::Rect& window, uint32_t index, double t, uint32_t seed);
Mote GlowAt(const ui::Rect& window, uint32_t index, double t, uint32_t seed);

// --- 火山 ---------------------------------------------------------------------------------------
// 火の粉: 火口から噴き上がる (割合 ventShare) か、下の端から舞い上がる (横へ流され、渦を巻きながら)。
// 熱 (heat) は生まれた時が 1、冷めて 0。最後は消える。
struct Ember {
    Vec2 at;
    Vec2 velocity;  // DIP / 秒 (速い火の粉の尾に使う)
    float radius = 0;
    float heat = 0;   // 1 = 白っぽい黄 .. 0 = 暗い赤
    float alpha = 0;  // 0 .. 1 (ちらつきを掛けた後)
};
Ember EmberAt(const ui::Rect& window, uint32_t index, double t, uint32_t seed, Vec2 vent, float ventShare);
// 火の色 (熱 0 .. 1 → 暗い赤 → 橙 → 黄 → 白っぽい黄)。
Color EmberColor(float heat);

}  // namespace rm::theme_explore::motion
