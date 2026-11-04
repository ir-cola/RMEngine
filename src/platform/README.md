# platform (L3)

状態: **実装済み** (ウィンドウ / D3D11 + D2D / ファイルダイアログ / クリップボード / 原子的な書き込み)。

置くもの: Windows に触るもの。ここ以外で `<windows.h>` を直接広げない (editor は例外的に使ってよい)。
守ること: スワップチェーンは FLIP_DISCARD + SCALING_NONE。サイズ変更中も描き直す。COM は STA。
