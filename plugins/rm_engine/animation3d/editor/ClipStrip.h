#pragma once
// 3D アニメーション - 3D モデルのアセットのインスペクターの「クリップとイベント」(docs/ANIMATION.md 3-6・4-6 の
// 「クリップの帯」・段階 2):
//
//   ┌ クリップとイベント ──────────────────────────────────────────┐
//   │ Idle                                        2.00 秒      │  ← クリックで選ぶ
//   │ Wave                          イベント 2  ·  1.50 秒      │
//   │ Footstep        Hit                                      │  ← 印の名前
//   │ [──▲────────────▲─────────|──────────────]              │  ← 帯 (クリックで足す所、▲ をドラッグで時刻)
//   │ 0s      0.5s      1s      1.5s                           │
//   │ [＋ イベント] [削除]                 足す所 1.20 秒       │
//   └──────────────────────────────────────────────────────────┘
//   ┌ イベント ─ 時刻 / 名前 / 数 / 文字 (選んだイベントの欄) ┐
//
// クリップの一覧はモデルのファイルを 3D 空間の MeshLoader で別のスレッドで読んで覚える (ファイルと .meta の
// 時刻が変わったら読み直す。その間は前の一覧のまま)。イベントは .meta の "import" の "clips" に書く
// (形と計算は ClipEvents.h)。どの変更も Undo の 1 件 (ドラッグ 1 回で 1 件)。

#include "editor/assets/AssetChildren.h"
#include "editor/assets/AssetInspect.h"

#include <filesystem>
#include <functional>
#include <vector>

namespace rm::animation3d {

// アセット 1 つ分の「クリップとイベント」のブロックを作る関数 (AssetInspectPart::open)。
std::function<std::vector<editor::InspectBlock>()> OpenClipStrip(const editor::AssetImportAccess& access);

// アセットのパネルで、モデルのタイルの ▸ で並べるクリップ (docs/ANIMATION.md 7-3 の C1。AssetChildSource::children)。
// 同じ一覧 (別のスレッドで読んで覚えたもの) から。ドラッグするとクリップの名前 (w::kDragName): アニメーターの「クリップ」の欄・
// アニメーションのタブの状態のクリップの欄へ。
std::vector<editor::AssetChild> ClipChildren(const std::filesystem::path& file, bool* pending);

}  // namespace rm::animation3d
