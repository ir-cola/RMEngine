# plugins/

ソフト (ゲーム / イラスト / 作曲 / 3D モデル …) と機能を足すプラグイン。
形は TestEngine の設計書 RME_PLATFORM §3-a / §3-b / §10 の決定どおり。

```
plugins/
  BuiltinPlugins.cpp           エンジンに組み込むプラグインの一覧 (ここだけが全部のプラグインを知る)
  <作者>/<名前>/
    plugin.json                author / name / displayName / requires (+ needsConnector / connectorFor)
    runtime/                   ゲームに入る部分 (ID の定数 Id.h もここ)
    editor/                    エディターだけの部分 (RegisterEditor でソフトやパネルを登録する)
```

| 決まり | 中身 |
|---|---|
| ID | **作者名とプラグイン名の 2 つの値** (くっつけない)。英小文字・数字・`_`、先頭は英字、末尾に `_` を置かない、`__` 不可。作者 3〜20 文字 (Windows の予約名は不可) / 名前 5〜64 文字。公式の作者名は `rm_engine`。検査は `src/core/PluginId.h` の 1 か所だけ |
| ID を書く場所 | `runtime/Id.h` の `kPluginId` (違反はコンパイルエラー) と `plugin.json`。起動時に 2 つを照らし合わせ、違えば読み込まずに理由を出す |
| 前提 | `requires` に書いたプラグインのヘッダだけを読める。前提が無い・使えないプラグインは**有効にせず理由を出す** (Hub の「プラグイン」とエディターのメニューに出る) |
| 層 | `runtime/` はゲームに入る層 (`base` `core` `platform` `ui`) と、前提の `runtime/` だけを読める。`editor/` は何でも読める。**`src/` (核) はプラグインを読まない** |
| つなぎ | 2 つのプラグインの両方を知らないと作れない部分は、両方を `requires` にした**つなぎ**に入れる (`"connectorFor": {相手}`)。つなぎが要る側は `"needsConnector": true` (つなぎが 1 つも使えないと使えない)。つなぎは要るものがそろうと一緒に入る (RME_PLATFORM 4-f) |
| 差し出し | 別のプラグインへ物を渡すときは `registry.Contribute(口の名前, static な物)`。受け取る側が口の名前と物の型を決めて `Contributions<型>(口)` で読む |
| ランタイムだけ | エディターの部分が無いプラグインも `BuiltinPlugins.cpp` に `RegisterEditor` 無し (`nullptr`) で載せる |
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
| `rm_engine` / `game_production` (ゲーム制作) | ソフト「ゲーム」。ゲームの設定・シーン (エンティティの親子)・部品 (コンポーネント) の仕組み。**つなぎが要る** (空間を知らない)。部品の種類は口 `rm_engine.game_production.component` で受け取る |
| `rm_engine` / `space3d` (3D 空間) | Transform (位置・回転・大きさ) と行列の計算。ランタイムだけ |
| `rm_engine` / `game_x_3d` (ゲーム制作 × 3D) | つなぎ。Transform をゲームの部品として差し出す (新しいエンティティに付く) |
