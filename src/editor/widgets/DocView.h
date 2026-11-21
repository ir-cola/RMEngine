#pragma once
// ヘルプの 1 ページ (help::Doc) を描く部品。ページの見た目 (見出し・文・箇条書き・注意書き・表・
// コード・画像・横線・リンク) はここだけで描く (憲法 3)。

#include "editor/help/HelpDoc.h"
#include "ui/Context.h"

#include <d2d1_1.h>

#include <functional>
#include <string>
#include <string_view>

namespace rm::editor::w {

// ページの中の画像 (null: 読めない - 説明の文字だけを出す)。
struct DocImage {
    ID2D1Bitmap* bitmap = nullptr;
    float width = 0, height = 0;  // 元の大きさ (DIP)
};

// `doc` を (x, y) から幅 `width` で上から描き、描いた高さを返す。見えている所 (`visible`) の外の
// ブロックは測るだけ。リンクを押したら、書いてあるとおりのリンク先を *clicked に入れる。
float DocView(ui::Context& c, std::string_view id, float x, float y, float width, const ui::Rect& visible, const help::Doc& doc,
              const std::function<DocImage(const std::string&)>& image, std::string* clicked);

}  // namespace rm::editor::w
