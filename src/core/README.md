# core (L2)

状態: **一部**。テストは `tests/TestCore.cpp` / `tests/TestReflect.cpp`。

- `UndoHistory` — Undo / Redo (積んだ = 未保存)
- `PluginId.h` — プラグインの ID の決まり (1 か所)
- `paint/` — マス目に塗る計算 (どのソフトの言葉も知らない): ランレングス `Runs` (気候の地図・タイルマップ)・線 (ブレゼンハム) と四角 `GridPaint`。テストは `tests/TestTilemap.cpp` / `TestClimate.cpp`
- `reflect/` — **リフレクション** (設計 R1)。型を 1 か所に書けば、保存・読み込み・インスペクター・比較・コピー・差分・Undo・範囲のクランプがそこから出る
  - `Reflect.h` — 印のマクロ (`RM_STRUCT` / `RM_ENUM` / `RM_PROPERTY` …) と型の表 (`TypeDesc`)。表は `tools/rmgen` がビルドのたびに作る
  - `Data.h` — 表を辿る保存・読み込み (知らないキー・知らない列挙の名前は保持して書き戻す)・比較・差分・**数の入り方の決まり 1 本** (`SanitizeNumber`)
  - `FieldCommand.h` — 「フィールドを変えた」の Undo コマンド 1 つ (どの型でも)
  - `Object.h` — 型を実行中に決める値 (型の表で作る・写す・壊す)。ゲームの部品などに使う

置くもの: どのソフトにも依らない核の仕組み。「ゲーム」「3D」などソフトの言葉が要るものは置かない (それはプラグイン)。
