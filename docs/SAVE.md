# 世界のセーブ (物の位置と状態。2026-11-24)

ユーザーの言葉: 「物の位置を含めたセーブも欲しい」。
いままでのセーブ (`rm::Save::Write` / `Read`) はグローバル変数だけだった (ARCHITECTURE「グローバル変数とセーブ」)。
ここでは、それに**世界** (どの物がどこにあって、どんな状態か) を足す。

## 1. 使い方 (ゲームを作る人から見て)

1. 残したい物に部品「セーブする」を付ける (部品を足す ▸ ゲーム ▸ セーブする)。欄は 3 つ:
   - **位置・回転・大きさ** (既定 オン): 置き場所の部品 (3D の Transform・Transform 2D) の値と、親
   - **有効** (既定 オン): 有効かどうか
   - **全部の部品の値** (既定 オフ): スクリプトも含めた全部の部品の値・名前・タグ・レイヤー。ゲームの中で足した部品も足し直す
2. スクリプトから `rm::Save::WriteWorld(1)` でセーブ、`rm::Save::ReadWorld(1)` で読む (`HasWorld(1)` で世界があるか)。`Exists` / `Delete` は今までと同じ
3. Prefab に付けておけば、ゲームの中で `rm::Spawn(prefab)` で作った物も残る (読むと作り直す)。ゲームの中で消した物は、読んだ後も消えたまま

## 2. 決めたこと

| 項目 | 決定 | なぜ |
|---|---|---|
| 何を残すか | **部品「セーブする」が付いた物だけ** | 全部を残すと、背景の木や床まで毎回書く。ゲームを作る人が選ぶ (Unity の「Save する物に印」と同じ考え) |
| 部品の値の書き方 | **リフレクションのまま** (`Scene::SaveComponent` = シーンのファイルと同じ `{"type", "data"}`) | 型ごとのセーブのコードを書かない (憲法 1・9)。スクリプトの部品も同じ道 |
| 3D と 2D | ゲーム制作は 3D / 2D を知らない。**「位置」= `ComponentInfo::placement` の部品** (Prefab の置き場所と同じ印。3D の Transform と Transform 2D が持つ) | 憲法 4・10。新しい空間のプラグインも `placement` を付ければそのまま残る |
| ファイル | 今の `save_<slot>.json` の中のキー `"world"` (グローバル変数と同じファイル) | 1 つのスロット = 1 つのファイル。消す (`Delete`) も 1 つ |
| `Write` と `WriteWorld` | `Write` はグローバル変数だけを書き、`"world"` は残す (知らないキーとして)。`WriteWorld` はグローバル変数と世界の両方 | 前からのゲームの動きを変えない |
| 読むとき | `ReadWorld` はグローバル変数をすぐ読み、世界は**フレームの切れ目**で: セーブしたシーンを読み直し (シーンの切り替えと同じ道 `SwitchAskedScene`)、その上にセーブの状態を当てる | フレームの途中でシーンを入れ替えない (スクリプトの物を壊さない)。今のシーンと同じでも読み直す (消した物・動いた物を全部元に戻してから当てる) |
| シーン | セーブに今のシーンの名前とアセットの ID を書き、読むときは ID → 名前の順でゲームのシーンの一覧から探す | 書き出したゲームとエディターの両方で同じ |
| ゲームの中で作った物 | id で見分ける: **シーンを読んだときの次の id (`ScriptWorld::sceneBase`) より大きい id はゲームの中で作った物**。id は使い回さない (Scene の決まり) ので、セーブした後でシーンに足した物 (ゲームを直した) とも混ざらない | 別の印を保存形式に足さずに済む |
| 消した物 | 読んだシーンの中で「セーブする」が付いていて、セーブに無く、id がセーブの `base` より小さい物 = ゲームの中で消した物 → 消す。`base` 以上の物 (セーブの後でシーンに足した物) は消さない | ゲームを更新しても古いセーブで新しい物が消えない |
| 壊れたセーブ | 無い物・使えない部品 (プラグインが無い)・形の違う値は**飛ばしてコンソールに 1 行**。落ちない | 前のエンジンの反省 (読み込みで落ちる) |

## 3. ファイルの形

```json
{ "kind": "rm_engine.save", "format": 1,
  "globals": { "score": 20 },
  "world": {
    "format": 1,
    "scene": { "name": "Main", "id": "<シーンのアセット ID>" },
    "base": 57,
    "entities": [
      { "id": 3, "parent": 0,
        "state": { "parent": 0, "active": true,
                   "components": [ {"type": "rm::game_production::Saveable", "data": {...}},
                                   {"type": "rm::space3d::Transform", "data": {"position": [1, 0, 4], ...}} ] } },
      { "id": 60, "parent": 3, "spawn": { "prefab": "<Prefab の ID>", "name": "Bullet" }, "state": {...} },
      { "id": 61, "parent": 60, "spawn": { "of": 60, "key": 2, "name": "Spark" }, "state": {...} },
      { "id": 70, "parent": 0, "spawn": { "name": "Marker" }, "state": {...} }
    ] } }
```

- `base`: そのシーンを読んだときの次の id。0 = 分からない (ゲームの中で作った物も消した物も扱わない)
- `entities` は**木の順** (親が先)。`state` があるのが「セーブする」の付いた物。`state` の無い行は、ゲームの中で作った物の親 (作り直すために要る。値は当てない)
- `spawn`: ゲームの中で作った物の作り方 — `prefab` = その Prefab を置き直す / `of` + `key` = 前の行で作り直した Prefab の中の物 (`key` は Prefab のファイルの中の id。入れ子の Prefab の一番上は外のファイルの id) / どちらも無い = その名前で新しいエンティティ
- `state.entity` (全部の部品の値のとき): エンティティの欄 (名前・タグ・レイヤー…。`id` と `parent` は読まない)
- `state.components`: 同じ型が 2 つ以上あれば、並びの順が「その型の何番目」
- 部品の中のエンティティの参照 (`EntityRef`) は、読むときに作り直した物の新しい id に付け替える。作り直さなかった物を指していれば 0
- `"world"` の中の知らないキーは書き直しても残す (憲法 6)。`format` が新しいセーブは「読める所だけ読む」とコンソールに出して読む

## 4. コードの場所

| 何 | どこ |
|---|---|
| 部品「セーブする」(`Saveable`)・取る (`CaptureWorld`)・当てる (`ApplyWorld`)。純粋 | `plugins/rm_engine/game_production/runtime/WorldSave.*` |
| 部品をゲームに出す (`ComponentInfo`) | `game_production/runtime/Register.cpp` (`RegisterRuntime`。エディターは `plugins/RuntimePlugins.cpp`、書き出したゲームは `GamePlayer.cpp` の `RegisterForGame` から) |
| ファイル (`save_<slot>.json` に `"world"` を書く・読む) | `game_production/runtime/Globals.*` (`WriteSave(…, world)`・`ReadSaveDoc`) |
| スクリプトの API | 部分 `rm_engine.game_production.save` 版 2 (`RmSaveApi2`: 版 1 の 4 つ + `writeWorld` / `readWorld` / `hasWorld`)。`RM.h` の `rm::Save::WriteWorld` / `ReadWorld` / `HasWorld`。`RmCoreApi` と `RM_ABI_VERSION` は変えない (部分は自分の版を持つ) |
| フレームの切れ目で当てる | `Scripts.cpp` の `SwitchAskedScene` (エディターの Play も書き出したゲームも同じ関数) |
| `sceneBase` を決める | Play の始め (`GameSoftware`)・ゲームの始め (`GamePlayer`)・シーンの切り替え (`SwitchAskedScene`) |
| テスト | `tests/TestWorldSave.cpp` (取る・当てる・作り直す・参照の付け替え・壊れたセーブ・スクリプトの部分からファイルに書いてシーンの切り替えで戻るまで)。書き出しの検査 `tools/check_export.ps1` の `game_world_save` (本物のスクリプトの DLL で書き出したゲームが WriteWorld → ReadWorld で場所を戻す) |
| ヘルプ | `plugins/rm_engine/game_production/help/save_world.md` (と `en/`) |

## 5. ★ まだ (やっていないこと)

- 兄弟の並びの順は残さない (親が変わった物は、新しい親の子の最後に入る)
- ゲームの中で外した部品は、読んだ後に戻る (全部の部品の値のときも、セーブに無い部品を外しはしない: ゲームを直して足した部品を古いセーブで消さないため)
- 「セーブする」の付いていない物は何も残らない (消したことも)。消える物を残したいときは、その物に付ける
- 物理の速さ・パーティクルの途中・音の再生位置は残らない (プラグインの Play の間の物。部品の値ではない)
- エディターの画面からセーブを見る・消すのは、今までの「セーブを消す (Library/Saves)」だけ
