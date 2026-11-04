# RM Engine

デジタルクリエイターのための、**プラグインで形を変える制作ソフト**。
ゲーム制作を中心に、イラスト・作曲・3D モデルなどの機能を「ソフト」としてプラグインで足していく。

- 対象: **Windows 10 / 11 + Visual Studio 2022** (C++ のデスクトップ開発)
- 言語・描画: C++20 / Direct3D 11 + Direct2D + DirectWrite
- UI: **ImGui は使わない。自作の軽い UI** (変わったときだけ描き直す。何もしていないときの CPU はほぼ 0)

## いまできること (0.0.1)

| 画面 | 内容 |
|---|---|
| **Hub** | 最近のプロジェクト一覧 (名前 / 更新日時 / 場所で並べ替え・検索)、新規作成 (テンプレート・名前・保存先)、`.rmpf` を開く、右クリック / 「…」メニュー (開く・エクスプローラーで表示・一覧から外す)、プラグインページ (枠だけ) |
| **エディター** | Hub から開くと最大化で起動。メニューバー / ソフトのタブ枠 / 階層・スタート・インスペクター・コンソール・アセットのパネル。保存 (Ctrl+S)、閉じるときの未保存確認 (保存して閉じる / 保存せずに閉じる / キャンセル)、✕ で Hub に戻る |
| **パネルのドッキング** | タブをドラッグして並べ替え・分割・別のパネルのタブへ。仕切りをドラッグして大きさを変える。目印の外で離すと**別ウィンドウ**になり、そのタブやタイトルバーを持ってエディターへ戻せる。× / 中クリック / 右クリックで閉じる。「ウィンドウ」メニューで開き直し・配置を元に戻す。配置はプロジェクトごとに `UserSettings/Layout.json` に保存 |

Hub とエディターは同じ `RM_Engine.exe` の別ウィンドウ。`RM_Engine.exe --project <ファイル.rmpf>` で Hub を飛ばして直接開ける (プラグインを入れた後の再起動で使う予定)。

## ビルド

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1            # Debug + テスト + 層チェック
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Config Release
```

Visual Studio で `RM_Engine.sln` を開いてもよい。出力は `build\bin\x64\<構成>\RM_Engine.exe`。

## フォルダ

```
src/
  base/       L0  文字列・UTF・JSON・ログ (標準ライブラリだけ・テストあり)
  core/       L2  Undo 履歴など、どのソフトにも依らない核
  platform/   L3  Win32 ウィンドウ・D3D11/D2D・ダイアログ・ファイル
  ui/         L4  核の UI の土台 (入力・フォーカス・文字入力・描画。色を持たない)
  editor/     L6  Hub・エディターの枠・エディター用の部品 (ゲームには入らない)
tests/        純粋計算のテスト (GPU もファイルも使わない)
tools/        build.ps1 / check_layers.ps1 / make_resources.ps1
resources/    アイコン・ロゴ・マニフェスト
plugins/      これから (ソフトごとのプラグイン)
docs/         設計と決めたこと
layers.txt    フォルダ → 層 の唯一の出所 (CI で検査)
```

詳しくは [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
