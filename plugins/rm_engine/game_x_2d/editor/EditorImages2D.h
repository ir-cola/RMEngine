#pragma once
// エディターの 2D の画像の覚え方 (シーンビュー (2D)・パネル「タイル」が使う 1 本): アセットの id ごとに 1 つ、
// 最初に求められたときに核の画像の読み方 (platform/Image: WIC) で作業スレッドに読ませ、届いたら UI の
// スレッドで Direct2D のビットマップにする (できるまで前の絵のまま。読んでいる間はフレームを頼む)。
// 1 秒ごとにファイル (と .meta) の更新時刻を見て、変わっていれば読み直す。10 秒使われなければ捨てる。
// 読めない・無いときは null で、コンソールに日本語で 1 回。

#include "editor/plugin/Software.h"
#include "rm_engine/game_x_2d/runtime/World2D.h"
#include "ui/Context.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace rm::game_x_2d {

class EditorImages2D {
public:
    EditorImages2D();
    ~EditorImages2D();
    EditorImages2D(const EditorImages2D&) = delete;
    EditorImages2D& operator=(const EditorImages2D&) = delete;

    // フレームの始めに: GPU のデバイスが作り直されたらビットマップを捨て、しばらく使わない物を忘れる。
    // `who` はコンソールの文の頭 (「2D のシーンビュー」)。
    void Begin(double now, uint64_t generation, std::string who);
    // `ref` が指す画像 (今どこにあっても)。null: 無い・まだ届いていない・読めない。
    const Image2D* Get(ui::Context& c, const AssetRef& ref, editor::SoftwareHost& host);
    // 今のフレームの Get を、関数にしたもの (World2D の ImageLookup2D)。
    ImageLookup2D Lookup(ui::Context& c, editor::SoftwareHost& host) {
        return [this, &c, &host](const AssetRef& ref) { return Get(c, ref, host); };
    }

private:
    struct Entry;
    std::unordered_map<std::string, std::unique_ptr<Entry>> entries_;
    uint64_t generation_ = 0;
    double now_ = 0;
    std::string who_;
};

}  // namespace rm::game_x_2d
