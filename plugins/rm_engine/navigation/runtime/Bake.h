#pragma once
// ナビゲーション - 焼く: 三角形 (NavInput) と体の形 (NavAgent) と範囲から、ナビメッシュ (NavData) を作る。
// 中身は Recast (src/third_party/RecastNavigation。型はこのプラグインの .cpp の中だけ: 設計 docs/NAVIGATION.md 2 章)。
//
//   範囲を区画 (タイル: tileCells x tileCells マス) に分け、区画ごとに Recast の 7 段:
//     1 細かい箱 (ボクセル) に三角形を塗る       4 つながった床を領域に分ける
//     2 歩ける箱を選ぶ (坂・天井・段差)         5 縁をたどって線に (輪郭)
//     3 壁から半径の分だけ削る                  6 凸の多角形にする       7 細かい高さの三角形を足す
//   区画は互いに関係しないので、スレッドに並べる (CPU の数 - 1、多くて 8)。結果は区画の順に並べるので、
//   同じ入力ならスレッドの数によらず同じバイトになる (時刻も入れない)。
//
// 作業スレッドで呼ぶ (エディターの「焼く」)。`stop` が立つと途中でやめて false (*error は空)。
// 純粋 (ファイルも描画もない)。tests/TestNavigation.cpp でテストする。

#include "rm_engine/navigation/runtime/NavTypes.h"

#include <functional>
#include <stop_token>
#include <string>

namespace rm::navigation {

struct BakeOptions {
    NavAgent agent;
    NavBounds bounds;
    int tileCells = 64;  // 区画の一辺のマスの数
    int threads = 0;     // 0: CPU の数 - 1 (多くて 8)。1: このスレッドだけ
};

// 区画の数の上限 (細かすぎる・広すぎるときは焼かずに理由を返す)。
inline constexpr int kMaxTilesPerSide = 512;

// 体の形の値を、焼けるように直したもの (負・0・NaN を既定へ、細かさを下限へ)。
NavAgent Sanitized(const NavAgent& a);

// `progress(済んだ区画, 全部の区画)` は作業スレッドのどれかから呼ばれる (null 可)。
// 歩ける所が 1 つも無いときも true (out.tiles が空)。
bool Bake(const NavInput& input, const BakeOptions& options, std::stop_token stop, const std::function<void(int, int)>& progress, NavData& out,
          std::string* error);

}  // namespace rm::navigation
