# RM Engine — 作業する人 (Claude を含む) へ

まず [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) を読むこと。前のエンジンの反省は
`C:\Users\akatu\Desktop\school\TestEngine\新エンジンへの引き継ぎメモ.txt` と `TestEngine/docs/` (DESIGN_SUMMARY.md / RME_PLATFORM.md)。

## 前提 (ユーザーの決定)

- 対象は **Windows + Visual Studio 2022**。Windows の機能や VS に頼ってよい
- **ImGui は一切使わない**。UI は `src/ui` (土台) + `src/editor/widgets` (見た目) の自作
- 一番のメインはゲーム制作。ただしイラスト・作曲・3D モデルなども同じくらい力を入れる。機能はプラグインで足す
- GitHub: https://github.com/ir-cola/RMEngine (private)。作業はコミットしてプッシュする

## ビルドと確認

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 [-Config Release]
```

- 成否は**終了コードだけ**で判断する (出力を grep しない)
- ビルド → `RMTests.exe` → `check_layers.ps1` → `check_lang.ps1` の順に全部通ること
- 画面に関わる変更は、実際に起動して操作して確かめる。確かめていないことは「未確認」と書く
- 画面の確認はユーザーが自分で行う。機能を足したら [docs/HOW_TO_TRY.md](docs/HOW_TO_TRY.md) の試し方を書き足す
- 画面に出す文字は日本語で書いて `Tr(L"…")` / `TrF` / `TrU8` を通し、英語を `resources/lang/en.json` (プラグインは `plugins/<作者>/<名前>/lang/en.json`) に足す。足し忘れは `tools/check_lang.ps1` (build.ps1 が走らせる) が止める。ログは日本語のまま
- 色は `theme::` の変数だけを使う (直に `Color::Hex` を書かない。黒と白のテーマで切り替わるため)
- 画面は `w::` の部品で描く (ボタン・行・タブ・見出し・印・箱は `editor/widgets` にある)。画面のコードで四角を自分で塗らない。新しい見た目が要るときは widgets に部品を足す
- 目に見える変更をしたら `resources/updates.json` (Hub の「アップデート情報」) に日本語と英語で 1 行足す
- 新しい技術 (API・ライブラリ・作り方) を使ったら [docs/TECHNOLOGIES.md](docs/TECHNOLOGIES.md) に説明を書き足し、末尾の「追記の記録」に 1 行足す (ユーザーが人に説明するためのメモ)

## 憲法 (前のエンジンの失敗から。破らない)

1. データの定義は 1 か所。保存・読み込み・インスペクター・Undo はそこから作る
2. 同じ式・同じ値を 2 か所に書かない
3. 1 つのものを描くコードは 1 本
4. ランタイム (ゲームに入る層) はエディターを知らない — `layers.txt` と `check_layers.ps1` が柵
5. 純粋計算は I/O と描画から離し、テストを書く
6. 保存形式は自己記述型 (JSON)。知らないキーは捨てずに書き戻す
7. Undo に積んだ編集 = 未保存。編集 UI を足したら必ず Undo に積む
8. 新しいフォルダを作るときは先に `layers.txt` に足す
9. データの型は `RM_STRUCT` + `RM_PROPERTY` で書く (保存・読み込み・インスペクター・Undo を手で書かない。`src/core/reflect/Reflect.h`)
10. 核 (`src/`) はプラグインを知らない。プラグインは `plugins/<作者>/<名前>/` に置き、読めるのは `requires` に書いたものだけ ([plugins/README.md](plugins/README.md))

## 書き方

- C++20 / `/W4` で警告 0 を保つ / UTF-8 (`/utf-8`)
- データの文字列は UTF-8 `std::string`、Windows と DirectWrite に渡すときだけ `std::wstring` (`base/Utf.h`)
- `std::filesystem::path` を `std::string` から作らない (`PathFromUtf8` を使う)
- 画面の切り替えや OS のダイアログは、描画中に直接呼ばず `App::Request*` / `ScreenWindow::Defer` を通す
