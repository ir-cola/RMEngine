#pragma once
// ヘルプの中身 (作業内容.md 2026-11-21 の決定): 上にフォルダーのタブ 2 つ (「Hub とエディター本体」/
// 「プラグイン」)、左にページの木 (核のツリービュー: 検索・開く / 閉じる)、右に本文。パネルが細い
// ときは木を隠し、左上の ≡ で出す (カーソルが外れて少しすると消える)。
// エディターのヘルプのパネル (core.help) と Hub のヘルプの窓が、同じこれで描く (憲法 3)。

#include "base/Json.h"
#include "editor/help/HelpLibrary.h"
#include "editor/tree/TreeView.h"
#include "editor/widgets/DocView.h"
#include "platform/Gfx.h"
#include "ui/Context.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace rm::editor {

class HelpView {
public:
    HelpView();
    ~HelpView();
    HelpView(const HelpView&) = delete;
    HelpView& operator=(const HelpView&) = delete;

    // 本を入れ替える (プラグインを入れた・消した・言語が変わった)。見ていたページは残す (まだあれば)。
    void SetLibrary(help::Library lib, bool english);
    // 画像を読むのに使う GPU (null: 画像は説明の文字だけ)。
    void SetGfx(platform::Gfx* gfx) { gfx_ = gfx; }

    void Draw(ui::Context& c, const ui::Rect& r);
    // そのページを開く (どちらのタブにあっても。木もそこまで開く)。無いページなら false。
    bool Open(const std::string& pageId);

    // 見え方 (UserSettings に残す: プロジェクトのデータではないので Undo・未保存にしない):
    // { "tab": 0, "pages": ["core/editor", "..."], "trees": [ {...}, {...} ] }
    json::Value SaveState() const;
    void LoadState(const json::Value& v);

private:
    class Source;
    struct Image {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        float width = 0, height = 0;
    };

    const std::vector<help::Node>& Roots(int tab) const { return tab == 0 ? lib_.core : lib_.plugins; }
    void Select(int tab, const std::string& pageId);  // ツリーが選んだ (ページを読む)
    void LoadDoc(const std::string& pageId);
    void DrawTree(ui::Context& c, const ui::Rect& r);
    void DrawPage(ui::Context& c, const ui::Rect& r);
    w::DocImage ImageOf(ui::Context& c, const std::string& path);

    help::Library lib_;
    bool english_ = false;
    platform::Gfx* gfx_ = nullptr;
    int tab_ = 0;
    std::array<std::string, 2> page_;  // タブごとに見ているページ
    std::array<tree::State, 2> trees_;
    std::array<std::unique_ptr<Source>, 2> sources_;
    std::optional<help::Doc> doc_;     // 見ているページ (読んだもの)
    std::string docId_;
    std::string docError_;
    // 細いとき: 木を出しているか、カーソルが外れた時刻 (少し待ってから消す)。
    bool narrowOpen_ = false;
    double leftAt_ = -1;
    // ページの画像 (ファイル -> 絵)。GPU が作り直されたら捨てる。
    std::map<std::filesystem::path, Image> images_;
    uint64_t imagesMadeIn_ = 0;
};

}  // namespace rm::editor
