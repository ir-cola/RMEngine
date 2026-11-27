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
    licenses/*.txt                 ランタイムが使う外のコードのライセンスの文章 (ファイル名 = ソフトの名前。書き出したゲームの LICENSES.txt に入る)
    help/*.md  help/en/*.md        ヘルプのページ (日本語と英語。フォルダーの入れ子 = 機能 → 細かい仕様。docs/HOW_TO_TRY.md の「ヘルプ」)
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
| 版とダウンロード | `plugin.json` の `"version"` は数 3 つの文字 (`"0.0.1"`)、`"engine": { "min": ... }` は動く一番古いエンジン、`"code": false` はテーマとテンプレートだけの物。コードの無い物は `tools/make_plugin_package.ps1` で zip と一覧 `index.json` にしてポートフォリオに置くと、Hub の「ダウンロードできるプラグイン」から落として入れられる ([docs/PLUGIN_DISTRIBUTION.md](../docs/PLUGIN_DISTRIBUTION.md) 段階 1) |

決まりは `tools/check_layers.ps1` がビルドと CI のたびに検査する。

## 新しいプラグインの足し方

1. `plugins/<作者>/<名前>/plugin.json` を書く
2. `runtime/Id.h` に `inline constexpr rm::PluginIdLiteral kPluginId{"<作者>", "<名前>"};`
3. `editor/` に `void RegisterEditor(rm::editor::PluginRegistry&)` を書く (ソフトを足すなら `AddSoftware`)
4. `BuiltinPlugins.cpp` の一覧に 1 行足す

## いま入っているもの

| プラグイン | 中身 |
|---|---|
| `rm_engine` / `game_production` (ゲーム制作) | ソフト「ゲーム」。シーンは 1 つずつ `Assets/Scenes/*.rmscene` (`runtime/SceneFile`。`.rmpf` にはシーンの一覧だけ)。テンプレート「ゲーム」(`templates/game`: 空のシーン。3D と 2D の両方が入る)。ゲームの設定・シーン (エンティティの親子)・部品 (コンポーネント) の仕組み。**つなぎが要る** (空間を知らない)。部品の種類は口 `rm_engine.game_production.component`、システム (毎フレーム動く処理) は口 `rm_engine.game_production.system` で受け取る。Play (シーンのコピーを動かす・ゲームビュー)・同じ部品を複数・Prefab (`.rmprefab`)。ランタイムの部分 (`runtime/Register.cpp`) が部品「セーブする」(世界のセーブ: [docs/SAVE.md](../docs/SAVE.md)) を差し出す |
| `rm_engine` / `space3d` (3D 空間) | Transform と行列の計算・カメラとレイ・Direct3D 11 での 3D の描画 (線と箱)。ランタイムだけ |
| `rm_engine` / `animation3d` (3D アニメーション) | 前提 = 3D 空間。ランタイムは純粋。骨の姿勢の計算: クリップを名前で骨につなぐ・サンプル (補間・ループ)・クロスフェードと重みの和・親から順の骨の行列・スキンの行列 (`runtime/Pose`)、時刻を進めて通ったイベントを数える (`runtime/ClipTime`)、1 体の再生 (`runtime/Player`)。エディターの部分は 3D モデルのインスペクターの「クリップとイベント」: クリップの一覧とイベントを置く帯 (核の口 `core.asset_inspect`。`.meta` の `"import"` の `"clips"` に書く。計算は純粋な `editor/ClipEvents`、テストあり)。設計 [docs/ANIMATION.md](../docs/ANIMATION.md) 3-3 / 3-6 |
| `rm_engine` / `theme_colors` (基本カラーテーマパック) | テーマのファイルだけ: 青・緑 (`themes/*.json`)。コードは `runtime/Id.h` だけ |
| `rm_engine` / `theme_seasons` (季節テーマパック) | テーマのファイル (スプリング・サマー・フォール・ウィンター、夜用の 夜桜・花火・お月見・クリスマス、季節で切り替える) と、その絵を描くコード (`editor/Seasons.cpp`。口 `core.theme_ornament` に差し出す。形は `editor/Ornament.*`、テストあり) |
| `rm_engine` / `theme_paper` (紙) | テーマのファイルだけ: 紙 |
| `rm_engine` / `theme_explore` (探検テーマパック) | テーマのファイル (宇宙・深海・火山) と、その**動く絵** (GPU のシェーダーで描く本物らしい背景。約 30 fps): `editor/Explore.cpp` (核に渡す)・`editor/Scenes.*` (HLSL と毎フレームの粒)・`editor/Motion.*` (粒の動き。純粋、テストあり)。描き方は核の `editor/widgets/MovingBackdrop.h` |
| `rm_engine` / `theme_gaming` (ゲーミング) | テーマのファイル (ゲーミング) と、その**動く絵**: 縞のある太陽・流れるネオンの格子・ウィンドウの縁を流れる虹 (`editor/Scenes.*`)、パネルの縁を流れる虹の線 (`editor/Gaming.cpp`)、虹の色と動き (`editor/Motion.*`。純粋、テストあり) |
| `rm_engine` / `game_x_3d` (ゲーム制作 × 3D) | つなぎ。前提 = ゲーム制作・3D 空間。Transform をゲームの部品として差し出し (3D のゲームの新しいエンティティに付く)、部品とシステム「回り続ける」(Play で確かめる用。`runtime/Spin.*`) を差し出し、3D のシーンビューを出す (口 `rm_engine.game_production.scene_view`、空間 `"3d"`)。ほかのプラグインに開く口: 世界に描く `rm_engine.game_x_3d.world_draw`・骨のあるモデルの姿勢を尋ねる `rm_engine.game_x_3d.mesh_pose` (`runtime/MeshPose.h`)・止まっているシーンビューの上の操作 `rm_engine.game_x_3d.scene_overlay` (`editor/SceneOverlay.h`)。テンプレート「3D のゲーム」(`templates/game_3d`: カメラ・光・床・箱) |
| `rm_engine` / `space2d` (2D 空間) | Transform 2D (位置・回転・大きさ・並び順) と行列・2D のカメラと当たり・Direct2D での 2D の描画 (`runtime/Renderer2D`)。エディターの部分は 2D のギズモ (`editor/Gizmo2D`。ほかのプラグインのエディターが使う。登録するものは無い) |
| `rm_engine` / `game_x_2d` (ゲーム制作 × 2D) | つなぎ。前提 = ゲーム制作・2D 空間。Transform 2D (2D のゲームの新しいエンティティに付く)・スプライト (アトラス)・スプライトのアニメーション・カメラ (2D)・回り続ける (2D) を差し出し、2D のシーンビューを出す (空間 `"2d"`)。3D と両方あるときは、シーンの上の帯でゲームの空間を選ぶ。テンプレート「2D のゲーム」(`templates/game_2d`: カメラ・地面・プレイヤー)。2D の描く物を置いて並べるのは 1 か所 (`runtime/World2D`: シーンビュー・ゲームビュー・書き出したゲーム)。ほかのプラグインに開く口: 2D の絵に描く `rm_engine.game_x_2d.world_draw` (`runtime/World2D.h`)・2D のシーンビューの道具 `rm_engine.game_x_2d.scene_tool` (`editor/SceneTool2D.h`) |
| `rm_engine` / `tilemap` (タイルマップ) | 前提 = 2D 空間 (ゲーム制作を知らない。純粋)。タイルセット `.rmtileset` (`runtime/Tileset`: 絵の元・切り方)・マスの値とマス目と区画 (`runtime/TileCell.h`)・層の置き場 (`runtime/TileStore`: 32 x 32 の区画のランレングス)・スタンプで塗る計算 (`runtime/TilePaint`)。エディターの部分はファイルの種類 (作成 ▸ タイルセット) とサムネイル。設計 [docs/TILEMAP.md](../docs/TILEMAP.md) |
| `rm_engine` / `game_x_tilemap` (ゲーム制作 × タイルマップ) | つなぎ。前提 = ゲーム制作・ゲーム制作 × 2D・2D 空間・タイルマップ。部品「タイルのグリッド」「タイルの層」(`runtime/TileLayer.h`)・区画をスプライトの束で描く (`runtime/TileDraw`。口 `world_draw`)・追加 ▸ 2D オブジェクト ▸ タイルマップ・「タイル」タブ (`editor/TilePanel`: パレットとタイルセット)・シーンビューの塗る道具 (`editor/TileTool`。口 `scene_tool`。Undo は `editor/TileEdit`: 触った区画だけ) |
| `rm_engine` / `physics` (物理) | 前提 = ゲーム制作・3D 空間・2D 空間。Jolt Physics (`src/third_party/JoltPhysics`、`src/RMJolt.vcxproj`) で剛体・当たり判定。部品 剛体・コライダー・コライダー (2D)・キャラクター と、固定の刻みのシステム「物理」を差し出す。スクリプトの部分 `rm_engine.physics.physics` (`RMPhysics.h`: 力・速さ・光線・キャラクター。フック OnHit / OnTriggerEnter / OnTriggerExit)。シーンビューにコライダーの線を出す (口 `rm_engine.game_production.scene_lines`)。テンプレート「物理の見本 (3D)」(`templates/physics_3d`: 落ちる箱と弾)。追加 ▸ 3D オブジェクトで作った物に、その形のコライダーを付ける (ゲーム制作の口 `rm_engine.game_production.created`) |
| `rm_engine` / `game_ui` (ゲーム UI) | 前提 = ゲーム制作 (空間を知らない)。ゲームの画面の UI: 部品 キャンバス・UI の配置 (アンカー)・文字 (フォントのファイルも)・画像 (9 スライス)・ボタン・バー。Play の間のサービス (マウス・キー・パッド・フォーカス・OnClick)、全部の空間の上に描く (口 `rm_engine.game_production.screen_draw`)、UI タブ (口 `rm_engine.game_production.view_panel`)、階層の「追加 ▸ UI ▸ キャンバス」など (口 `rm_engine.game_production.create_item`)。スクリプトの部分 `rm_engine.game_ui.ui` (`RMUI.h`。フック OnClick)。テンプレート「ゲーム UI の見本」(`templates/ui_sample`) |
| `rm_engine` / `audio` (音) | 音の読み込み (WAV は自作・OGG は stb_vorbis・MP3 / AAC / FLAC は Media Foundation) と XAudio2 での再生 (`runtime/AudioEngine`: 作業スレッドで読む・長い音は流す・フェード・X3DAudio)。エディターの部分はアセットのパネルの「鳴らしてみる」と長さ (核の口 `core.asset_preview`)。ゲーム制作を知らない |
| `rm_engine` / `game_x_audio` (ゲーム制作 × 音) | つなぎ。前提 = ゲーム制作・音。部品「音源」・Play の間の音 (ゲーム制作の口 `rm_engine.game_production.play_service`)・スクリプトの `RMAudio.h` (`rm::Audio` / `rm::AudioSource`)・書き出しで音を `.rmsound` に (取り込みの設定付き、作り直さない) |
| `rm_engine` / `particles` (パーティクル) | 前提なし。エフェクト `.rmparticle` (エミッターとモジュールの並び)・モジュール `.rmfx` (HLSL + `param` / `attr`。用意したモジュールも同じ形: `runtime/BuiltinModules`)・モジュールから 1 本のシェーダーを組み立てる (`runtime/ParticleShader`)・GPU で動かして描く (`runtime/ParticlePlayer`: Compute Shader)。ゲーム制作を知らない |
| `rm_engine` / `game_x_animation` (ゲーム制作 × アニメーション) | つなぎ。前提 = ゲーム制作・ゲーム制作 × 3D・3D 空間・3D アニメーション。部品「アニメーター」(`runtime/Animator.h`: 同じエンティティのメッシュの骨のあるモデルを、クリップ 1 つで流す)・その姿勢をゲーム制作 × 3D の口 `rm_engine.game_x_3d.mesh_pose` に答える (`runtime/AnimatorPoser.*`。決まりは純粋な `runtime/AnimatorLogic.*`、テストあり)・クリップの欄をモデルのクリップから選ぶ (ゲーム制作の `ComponentLook::choices`)・止まっているシーンビューの再生 / 一時停止 / 時間のつまみ (ゲーム制作 × 3D の口 `rm_engine.game_x_3d.scene_overlay`)。設計 [docs/ANIMATION.md](../docs/ANIMATION.md) 段階 1 |
| `rm_engine` / `game_x_particles` (ゲーム制作 × パーティクル) | つなぎ。前提 = ゲーム制作・パーティクル・ゲーム制作 × 3D・3D 空間。部品「パーティクル」・3D の絵への描き込み (ゲーム制作 × 3D の口 `rm_engine.game_x_3d.world_draw`)・「パーティクル」タブ (`editor/ParticleTab`: 並び・値・プレビュー) |
| `rm_engine` / `weather` (天候) | 前提 = ゲーム制作・ゲーム制作 × 3D・3D 空間。シーンの設定「天候」(雲の量・降水量・霧の量 0〜100・雪にする・積もる高さ cm・雷・風、テンプレート。`runtime/Weather.*`・テンプレートの行は `editor/Register.cpp`)・Play の間のシステム「天候」(フェード・濡れ・風で流れる距離)・空に雲と雷を伝える (ゲーム制作 × 3D の口 `rm_engine.game_x_3d.sky_weather`)・雨と雪と濡れの描画 (口 `rm_engine.game_x_3d.world_draw`。3D 空間の「向きの地図」と「面の塗りに足す口」を使う: `runtime/WeatherDraw.*`・`Falling.*`)・スクリプトの部分 `rm_engine.weather.weather` (`RMWeather.h`)・雨と雪が当たる形の口 `rm_engine.weather.blockers` |
| `rm_engine` / `climate` (気候) | 前提 = ゲーム制作・3D 空間・2D 空間。シーンの設定「気候」(エリアの外の気温・湿り気・高さで冷える・基準の高さ)・部品「気候エリア」(3D: 箱 / 円柱) /「気候エリア 2D」(長方形 / 円)。境目の幅でなめらかに重なり、優先度で上下 (`runtime/Climate.*`)・シーンの設定「気候の地図」(3D: 真上から塗った気候。下の層で、エリアはその上)・スクリプトの部分 `rm_engine.climate.climate` (`RMClimate.h`)・シーンビューのエリアの枠 (`editor/Register.cpp`)・「気候」タブ (`editor/ClimateTab`: 3D のシーンビューに真上から描かせた地図にペンで塗る・同じタブで天候。天候の側は口 `rm_engine.climate.tab_page` で受ける)。設計 [docs/CLIMATE.md](../docs/CLIMATE.md) |
| `rm_engine` / `weather_x_climate` (天候 × 気候) | つなぎ。前提 = 天候・気候 (ほか)。気温で雨 / みぞれ / 雪、湿り気で降る量を、天候の口 `rm_engine.weather.place` に答える (粒ごと)。Play の間のシステム「天候 × 気候」がカメラのいる所の降り方を天候に書く。エディターの部分は、気候のタブの「天候」の側にシーンの「天候」を出す (気候の口 `rm_engine.climate.tab_page`) |
| `rm_engine` / `navigation` (ナビゲーション) | 前提なし (純粋)。Recast / Detour (`src/third_party/RecastNavigation`、`src/RMRecast.vcxproj`) を包む: 焼く (`runtime/Bake`: 区画をスレッドに並べる)・道を探す (`runtime/Query`: A* とファネル・一番近い点・光線・でたらめな点・通れないエリア)・群れ (`runtime/Crowd`: DetourCrowd)・`.rmnav` (`runtime/NavFile`)。設計 [docs/NAVIGATION.md](../docs/NAVIGATION.md) |
| `rm_engine` / `game_x_navigation` (ゲーム制作 × ナビゲーション) | つなぎ。前提 = ゲーム制作・ナビゲーション・3D 空間・2D 空間。シーンの設定「ナビ」(エージェントの型・エリアの種類)・部品「ナビメッシュ」「ナビの形」「ナビのエリア」「ナビの動く者」・システム「ナビ」(`runtime/NavWorld`: Play の間に焼いた物を読み、DetourCrowd で歩かせ、着いたら `NavArrived`)・スクリプトの部分 `rm_engine.game_x_navigation.nav` (`RMNav.h`)・「ナビ」タブ (焼く・古い印・経路を試す・Play 中の動く者)・シーンビューの線・書き出しの焼き手 (`.rmnav`)。テンプレート「ナビの見本 (3D)」(`templates/nav_3d`)。ほかのプラグインに開く口: 焼く物 `rm_engine.game_x_navigation.source`・動かし方 `rm_engine.game_x_navigation.mover` (`runtime/NavPorts.h`) |
| `rm_engine` / `navigation_x_3d` (ナビ × 3D) | つなぎ。「メッシュ (3D モデル)」の形を焼く物に (口 `source`) |
| `rm_engine` / `navigation_x_physics` (ナビ × 物理) | つなぎ。動かないコライダーを焼く物に・動く剛体とキャラクターは焼かない・キャラクターで歩かせる (口 `mover`) |
| `rm_engine` / `weather_x_physics` (天候 × 物理) | つなぎ。前提 = 天候・物理 (ほか)。どの物が動くか (キャラクター・動かない ではない剛体) を天候に言う: そのメッシュは雪に足跡を残す (天候の口 `rm_engine.weather.movers`)。雨・雪が当たるのは見えているメッシュで、コライダーではない (2026-11-25 から) |
| `rm_engine` / `animation_x_physics` (アニメーション × 物理) | つなぎ (ゲーム制作 × アニメーションの)。前提 = ゲーム制作 × アニメーション・物理 (ほか)。アニメーターのルートモーションを、同じエンティティの物理のキャラクターの速さにする (ゲーム制作 × アニメーションの口 `rm_engine.game_x_animation.root_motion`。横だけ)。足の IK の地面を物理の光線で答える (口 `rm_engine.game_x_animation.ground`。自分のキャラクターには当たらない)。設計 [docs/ANIMATION.md](../docs/ANIMATION.md) 7-1 |
| `rm_engine` / `particles_x_physics` (パーティクル × 物理) | つなぎ。前提 = ゲーム制作 × パーティクル・物理 (ほか)。物理のコライダー (箱・球・カプセル。トリガーとメッシュは除く) とキャラクターを、粒が当たる形にして渡す (ゲーム制作 × パーティクルの口 `rm_engine.game_x_particles.collision_shapes`) |
