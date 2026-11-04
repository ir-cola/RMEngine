# plugins/

ソフト (ゲーム / イラスト / 作曲 / 3D モデル …) と機能を足すプラグイン。
形は TestEngine の設計書 RME_PLATFORM §3-a / §3-b / §10 の決定どおり。

```
plugins/
  BuiltinPlugins.cpp           エンジンに組み込むプラグインの一覧 (ここだけが全部のプラグインを知る)
  <作者>/<名前>/
    plugin.json                author / name / displayName / requires
    runtime/                   ゲームに入る部分 (ID の定数 Id.h もここ)
    editor/                    エディターだけの部分 (RegisterEditor でソフトやパネルを登録する)
```

| 決まり | 中身 |
|---|---|
| ID | **作者名とプラグイン名の 2 つの値** (くっつけない)。英小文字・数字・`_`、先頭は英字、末尾に `_` を置かない、`__` 不可。作者 3〜20 文字 (Windows の予約名は不可) / 名前 5〜64 文字。公式の作者名は `rm_engine`。検査は `src/core/PluginId.h` の 1 か所だけ |
| ID を書く場所 | `runtime/Id.h` の `kPluginId` (違反はコンパイルエラー) と `plugin.json`。起動時に 2 つを照らし合わせ、違えば読み込まずに理由を出す |
| 前提 | `requires` に書いたプラグインのヘッダだけを読める。前提が無い・使えないプラグインは**有効にせず理由を出す** (Hub の「プラグイン」とエディターのメニューに出る) |
| 層 | `runtime/` はゲームに入る層 (`base` `core` `platform` `ui`) と、前提の `runtime/` だけを読める。`editor/` は何でも読める。**`src/` (核) はプラグインを読まない** |
| ビルド | 全部 `RM_Engine.exe` に静的リンク。ソースはワイルドカードで拾うので vcxproj を書き直さなくてよい。`plugin.json` はビルドで `build/bin/.../Plugins/<作者>/<名前>/` へ写る (= このエンジンが持っているもの) |
| インストール | Hub で入れると `%LOCALAPPDATA%\RM_Engine\Plugins\<作者>\<名前>\` へフォルダごと写る。入っていないプラグインは初期化されない。最初の起動では全部入る |

決まりは `tools/check_layers.ps1` がビルドと CI のたびに検査する。

## 新しいプラグインの足し方

1. `plugins/<作者>/<名前>/plugin.json` を書く
2. `runtime/Id.h` に `inline constexpr rm::PluginIdLiteral kPluginId{"<作者>", "<名前>"};`
3. `editor/` に `void RegisterEditor(rm::editor::PluginRegistry&)` を書く (ソフトを足すなら `AddSoftware`)
4. `BuiltinPlugins.cpp` の一覧に 1 行足す

## いま入っているもの

| プラグイン | 中身 |
|---|---|
| `rm_engine` / `game_production` (ゲーム制作) | ソフト「ゲーム」の空の枠。パネルはシーン / ゲーム (中身はまだ無い) + 核のパネル |
