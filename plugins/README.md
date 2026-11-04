# plugins/

状態: **未着手**。

ここに置くもの: ソフト (ゲーム / イラスト / 作曲 / 3D モデル …) と機能を足すプラグイン。
形は TestEngine の設計書 RME_PLATFORM の決定に従う予定:

- `plugins/<作者>/<名前>/` に `plugin.json` (`author` / `name` / `displayName` / `requires` …)
- `runtime/` (ゲームに入る) と `editor/` (エディターだけ) に分ける
- 最初は静的リンク (ビルド時のモジュール)。DLL は後から

埋める条件: エディターに「ソフトの登録」の仕組みを作るとき (docs/ARCHITECTURE.md §7 の 1)。
