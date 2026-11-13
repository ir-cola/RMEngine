# plugins/

ソフト (ゲーム / イラスト / 作曲 / 3D モデル …) と機能を足すプラグイン。
形は TestEngine の設計書 RME_PLATFORM §3-a / §3-b / §10 の決定どおり。

```
plugins/
  BuiltinPlugins.cpp           エンジンに組み込むプラグインの一覧 (ここだけが全部のプラグインを知る)
  RuntimePlugins.cpp           ランタイムの部分が登録するプラグインの一覧 (書き出したゲームもリンクする。runtime/ のヘッダーだけ)
  <作者>/<名前>/
    plugin.json                author / name / displayName / requires (+ needsConnector / connectorFor)
    runtime/                   ゲームに入る部分 (ID の定数 Id.h もここ)
    editor/                    エディターだけの部分 (RegisterEditor でソフトやパネルを登録する)
    lang/en.json               画面の文字の英語
    themes/*.json              テーマ (テーマパック)
    templates/<id>/template.json   新しいプロジェクトのテンプレート (Hub の新規作成の木)
```

| 決まり | 中身 |
|---|---|
| ID | **作者名とプラグイン名の 2 つの値** (くっつけない)。英小文字・数字・`_`、先頭は英字、末尾に `_` を置かない、`__` 不可。作者 3〜20 文字 (Windows の予約名は不可) / 名前 5〜64 文字。公式の作者名は `rm_engine`。検査は `src/core/PluginId.h` の 1 か所だけ |
| ID を書く場所 | `runtime/Id.h` の `kPluginId` (違反はコンパイルエラー) と `plugin.json`。起動時に 2 つを照らし合わせ、違えば読み込まずに理由を出す |
| 前提 | `requires` に書いたプラグインのヘッダだけを読める。前提が無い・使えないプラグインは**有効にせず理由を出す** (Hub の「プラグイン」とエディターのメニューに出る) |
| 層 | `runtime/` はゲームに入る層 (`base` `core` `platform` `ui`) と、前提の `runtime/` だけを読める。`editor/` は何でも読める。**`src/` (核) はプラグインを読まない** |
| つなぎ | 2 つのプラグインの両方を知らないと作れない部分は、両方を `requires` にした**つなぎ**に入れる (`"connectorFor": {相手}`)。つなぎが要る側は `"needsConnector": true` (つなぎが 1 つも使えないと使えない)。つなぎは要るものがそろうと一緒に入る (RME_PLATFORM 4-f)。Hub の一覧でも別の見出し「つなぎのプラグイン」に出て、インストール・削除のボタンは無い |
| 差し出し | 別のプラグインへ物を渡すときは `registry.Contribute(口の名前, static な物)`。受け取る側が口の名前と物の型を決めて `Contributions<型>(口)` で読む |
| テーマパック | `themes/*.json` に 1 テーマ 1 ファイル (形は `src/editor/widgets/ThemeFile.h`)。核がプラグインを読むたびに、使えるプラグインのインストール先の `themes/` を読む。名前の英語はそのプラグインの `lang/en.json` (`check_lang.ps1` が確かめる)。絵が要るなら、コードで `w::OrnamentInfo` を口 `w::kOrnamentPoint` に差し出し、テーマのファイルの `"ornament"` にその名前を書く (同じプラグインの物だけを探す) |
| ランタイムの登録 | ゲームにも要る物 (システムなど) は `runtime/Register.cpp` の `RegisterRuntime(RuntimeRegistry&)` で差し出し、`RuntimePlugins.cpp` に 1 行足す (`src/core/RuntimeRegistry.h`)。エディターはランタイムの部分 → `RegisterEditor` の順に呼ぶ。部品の型 (`ComponentInfo`) もここで差し出す。エディターだけの見せ方 (階層の絵・ローカル / ワールド) は `RegisterEditor` から `ComponentLook` で |
| テンプレート | `templates/<id>/template.json` に 1 テンプレート 1 ファイル (形は `src/editor/project/Templates.h`): 木の場所 `path` (ID の配列。a-z 0-9 _)・`name` / `description`・`plugins` (中身が使うもの。自分は書かなくてよい)・`open` (最初に開くソフト)・`project` (新しい `.rmpf` に入れるキー。ゲームなら `softwareData` に設定とシーンの一覧)・横の `Assets/` (新しいプロジェクトの `Assets/` に写す。ゲームなら `Scenes/Main.rmscene` と `.meta`)・`userSettings` (`Layout.json` の `softwares` に入れる最初の配置)。核は**エンジンが持つ方**の `templates/` を読むので、入っていなくても木に出る。名前と説明の英語はそのプラグインの `lang/en.json` (`check_lang.ps1` が確かめる)。テンプレートが使わないプラグインは `plugin.json` に `"software": ["game"]` を書くと、Hub の「ソフトで絞る」でそのソフトに出る (書かなければ「共通」) |
| スクリプトの API | ゲームのスクリプト (C++ の DLL) に渡す機能のうち、空間などプラグインが知る物は、ランタイムの部分が口 `rm_engine.game_production.script_api` に `ScriptApiPart{名前, 版, C の関数の表}` を差し出す。スクリプトが読むヘッダーは `runtime/sdk/*.h` に置く (ビルドで `<exe>/Sdk/` に写る。C の表の形は `*Abi.h`、使い方の C++ は `RM3D.h` のように) |
| 書き出したゲーム | 書き出したゲームは使うプラグインの `runtime/` の .lib だけをリンクする (ビルドの `GameKit`)。ゲームだけが要る登録 (ゲーム制作の「ゲーム」・空間の描き手 `game_production::GameDrawInfo`) は、共通の `RegisterRuntime` を呼んだうえで足す関数を `RM_RUNTIME_ENTRY(作者, 名前, 関数)` で C の名前の入口にする (生成した main が呼ぶ。エディターとテストはリンクしない) |
| ランタイムだけ | エディターの部分が無いプラグインも `BuiltinPlugins.cpp` に `RegisterEditor` 無し (`nullptr`) で載せる |
| ビルド | 全部 `RM_Engine.exe` に静的リンク。ソースはワイルドカードで拾うので vcxproj を書き直さなくてよい。`plugin.json` はビルドで `build/bin/.../Plugins/<作者>/<名前>/` へ写る (= このエンジンが持っているもの) |
| インストール | Hub で入れると `%LOCALAPPDATA%\RM_Engine\Engines\<エンジン>\Plugins\<作者>\<名前>\` へフォルダごと写る。入っていないプラグインは初期化されない。最初の起動では全部入る |

決まりは `tools/check_layers.ps1` がビルドと CI のたびに検査する。

## 新しいプラグインの足し方

1. `plugins/<作者>/<名前>/plugin.json` を書く
2. `runtime/Id.h` に `inline constexpr rm::PluginIdLiteral kPluginId{"<作者>", "<名前>"};`
3. `editor/` に `void RegisterEditor(rm::editor::PluginRegistry&)` を書く (ソフトを足すなら `AddSoftware`)
4. `BuiltinPlugins.cpp` の一覧に 1 行足す

## いま入っているもの

| プラグイン | 中身 |
|---|---|
| `rm_engine` / `game_production` (ゲーム制作) | ソフト「ゲーム」。シーンは 1 つずつ `Assets/Scenes/*.rmscene` (`runtime/SceneFile`。`.rmpf` にはシーンの一覧だけ)。テンプレート「ゲーム」(`templates/game`: 空のシーン。3D と 2D の両方が入る)。ゲームの設定・シーン (エンティティの親子)・部品 (コンポーネント) の仕組み。**つなぎが要る** (空間を知らない)。部品の種類は口 `rm_engine.game_production.component`、システム (毎フレーム動く処理) は口 `rm_engine.game_production.system` で受け取る。Play (シーンのコピーを動かす・ゲームビュー)・同じ部品を複数・Prefab (`.rmprefab`) |
| `rm_engine` / `space3d` (3D 空間) | Transform と行列の計算・カメラとレイ・Direct3D 11 での 3D の描画 (線と箱)。ランタイムだけ |
| `rm_engine` / `theme_colors` (基本カラーテーマパック) | テーマのファイルだけ: 青・緑 (`themes/*.json`)。コードは `runtime/Id.h` だけ |
| `rm_engine` / `theme_seasons` (季節テーマパック) | テーマのファイル (スプリング・サマー・フォール・ウィンター) と、その絵を描くコード (`editor/Seasons.cpp`。口 `core.theme_ornament` に差し出す。形は `editor/Ornament.*`、テストあり) |
| `rm_engine` / `theme_paper` (紙) | テーマのファイルだけ: 紙 |
| `rm_engine` / `game_x_3d` (ゲーム制作 × 3D) | つなぎ。前提 = ゲーム制作・3D 空間。Transform をゲームの部品として差し出し (3D のゲームの新しいエンティティに付く)、部品とシステム「回り続ける」(Play で確かめる用。`runtime/Spin.*`) を差し出し、3D のシーンビューを出す (口 `rm_engine.game_production.scene_view`、空間 `"3d"`)。テンプレート「3D のゲーム」(`templates/game_3d`: カメラ・光・床・箱) |
| `rm_engine` / `space2d` (2D 空間) | Transform 2D (位置・回転・大きさ・並び順) と行列・2D のカメラと当たり・Direct2D での 2D の描画 (`runtime/Renderer2D`)。エディターの部分は 2D のギズモ (`editor/Gizmo2D`。ほかのプラグインのエディターが使う。登録するものは無い) |
| `rm_engine` / `game_x_2d` (ゲーム制作 × 2D) | つなぎ。前提 = ゲーム制作・2D 空間。Transform 2D (2D のゲームの新しいエンティティに付く)・スプライト (アトラス)・スプライトのアニメーション・カメラ (2D)・回り続ける (2D) を差し出し、2D のシーンビューを出す (空間 `"2d"`)。3D と両方あるときは、シーンの上の帯でゲームの空間を選ぶ。テンプレート「2D のゲーム」(`templates/game_2d`: カメラ・地面・プレイヤー) |
| `rm_engine` / `physics` (物理) | 前提 = ゲーム制作・3D 空間・2D 空間。Jolt Physics (`src/third_party/JoltPhysics`、`src/RMJolt.vcxproj`) で剛体・当たり判定。部品 剛体・コライダー・コライダー (2D)・キャラクター と、固定の刻みのシステム「物理」を差し出す。スクリプトの部分 `rm_engine.physics.physics` (`RMPhysics.h`: 力・速さ・光線・キャラクター。フック OnHit / OnTriggerEnter / OnTriggerExit)。シーンビューにコライダーの線を出す (口 `rm_engine.game_production.scene_lines`)。テンプレート「物理の見本 (3D)」(`templates/physics_3d`: 落ちる箱と弾) |
| `rm_engine` / `audio` (音) | 音の読み込み (WAV は自作・OGG は stb_vorbis・MP3 / AAC / FLAC は Media Foundation) と XAudio2 での再生 (`runtime/AudioEngine`: 作業スレッドで読む・長い音は流す・フェード・X3DAudio)。エディターの部分はアセットのパネルの「鳴らしてみる」と長さ (核の口 `core.asset_preview`)。ゲーム制作を知らない |
| `rm_engine` / `game_x_audio` (ゲーム制作 × 音) | つなぎ。前提 = ゲーム制作・音。部品「音源」・Play の間の音 (ゲーム制作の口 `rm_engine.game_production.play_service`)・スクリプトの `RMAudio.h` (`rm::Audio` / `rm::AudioSource`)・書き出しで音を `.rmsound` に (取り込みの設定付き、作り直さない) |
