#pragma once
// 書き出したゲームのフレーム (設計 NewEngineBluePrint 11-b / 11-b-2): <ゲーム>.exe は、これと、
// 核とプラグインが使うライブラリと、それらのプラグインを並べた小さな生成 main (editor/export:
// GameMainSource) でできている。ソフトもプラグインも知らない: プラグインが RuntimeRegistry に
// (ランタイム部分を、並んでいる順に) 登録し、ゲームを作るもの - ゲーム制作 - が kGamePoint で
// それを出す。何を描くか、ゲームがどう動くかは、そのプラグインの仕事。
//
//   <exe のフォルダ>/
//     <ゲーム>.exe        ゲーム本体 (エディターは入っていない。.pdb も無い)
//     Game.rmpak          アセット (core/pak: 詰めて scramble 済み。exe と同じビルド番号)
//     Scripts.dll         ゲームの C++ スクリプト (あるとき)
//     vcruntime140.dll …  C++ ランタイム (Visual Studio の再配布ファイル。無い PC でも遊べる)
//
// ゲームが読むパスは、すべて自分のフォルダの中 (作った PC のパスは決して使わない)。
// `<ゲーム>.exe --smoke` は数フレーム (5 フレーム以上・ゲームの時間で 0.1 秒以上) 描いて終了コード 0 で閉じる (書き出しのチェック)。
// 問題があれば 1 で閉じ、理由を exe の隣の game.log に書く。

#include "base/Math.h"
#include "core/RuntimeRegistry.h"
#include "platform/Gfx.h"
#include "ui/Types.h"

#include <d2d1_1.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// 生成された main の分 (同じ宣言がそこにも書かれる - editor/export/ExportPlan.cpp)。
extern "C" {
struct RmLinkedPlugin {
    const char* author;
    const char* name;
    void (*registerRuntime)(rm::RuntimeRegistry&);  // null: そのプラグインは何も登録しない
};
struct RmGameImage {
    const char* title;          // UTF-8: ゲームが別のものを言うまでのウィンドウのタイトル
    unsigned long long build;   // 書き出しの番号 (.rmpak も同じでなければならない)
    const RmLinkedPlugin* plugins;
    unsigned long long count;
};
const RmGameImage* rm_game_image();
}

namespace rm::game {

inline constexpr wchar_t kPakName[] = L"Game.rmpak";
inline constexpr wchar_t kScriptsDll[] = L"Scripts.dll";
inline constexpr wchar_t kLogName[] = L"game.log";

// フレームがゲームに渡すもの。
class Host {
public:
    virtual ~Host() = default;
    // .rmpak の中のファイル ("game.json", "Assets/…")。無いときは nullopt と *error (日本語)。
    virtual std::optional<std::string> Read(std::string_view path, std::string* error) const = 0;
    virtual const RuntimeRegistry& Registry() const = 0;
    virtual platform::Gfx& Graphics() = 0;
    virtual std::filesystem::path Folder() const = 0;  // exe のフォルダ (Scripts.dll)
    virtual void Log(int level, std::string_view textUtf8) = 0;  // 0 情報 / 1 警告 / 2 エラー
    virtual void SetTitle(std::string_view titleUtf8) = 0;
    // ウィンドウのクライアント領域の上のマウス (DIP。Draw の大きさと同じ)。外にあるとき、または
    // ゲームが手前にないときは nullopt。
    virtual std::optional<Vec2> Pointer() const { return std::nullopt; }
    // 前に呼んでから、ウィンドウの上で左ボタンが何回押された / 離されたか (そのメッセージの数。
    // 1 フレームより短いクリックは両方を持つ)。呼ぶと 0 に戻る。
    struct PointerPresses {
        int down = 0, up = 0;
    };
    virtual PointerPresses TakePointerPresses() { return {}; }
    // 前に呼んでから打たれた文字・キー・IME (SetTextInput で打っていると言われている間だけ集める)。
    // 呼ぶと空に戻る (変換中の文字は残る)。
    virtual ui::TypedText TakeTyped() { return {}; }
    // ゲームが文字を打っている (入力欄): IME をつなぎ、候補の窓を (x, y, w, h) (DIP: Draw の大きさ) の
    // 下に出す。false: IME を切る (ゲームのキーが IME に食べられない)。
    virtual void SetTextInput(bool want, float x, float y, float w, float h) {
        (void)want, (void)x, (void)y, (void)w, (void)h;
    }
    // ゲームが終わりを求める: このフレームのあとにウィンドウが閉じる (× を押したときと同じ)。
    virtual void Quit() {}
};

// 1 つのゲーム (それを出すプラグインが作る)。
class Game {
public:
    virtual ~Game() = default;
    // データを読む (ウィンドウが出る前)。false と *error (日本語) を返すとゲームは止まる。
    virtual bool Load(std::string* error) = 0;
    // ゲームがウィンドウをどうしたいか (ゲーム設定の 画面)。
    struct Display {
        int width = 1280, height = 720;  // ウィンドウのとき、クライアント領域の DIP
        // フルスクリーンは、モニター全体を覆う枠なしウィンドウとして出す (★ 排他モードではない)。
        enum class Mode { Windowed, Fullscreen, Borderless } mode = Mode::Windowed;
        bool vsync = true;   // 画面のリフレッシュに合わせる
        int targetFps = 60;  // vsync なし: 1 秒にこの数のフレームまで
    };
    virtual Display GetDisplay() const = 0;
    // 1 フレーム: 入力を読み、システム (とその固定ステップ)、スクリプトを動かす。
    virtual void Update(double dt) = 0;
    // ウィンドウに描く (その全体に)。
    virtual void Draw(ID2D1DeviceContext* dc, float widthDip, float heightDip) = 0;
    // ゲームが終わる (ウィンドウが閉じる)。
    virtual void Stop() {}
};

// RuntimeRegistry の口: 項目は GameInfo (静的)。最初に出したものがゲームを作る。
inline constexpr char kGamePoint[] = "rm.game.maker";
struct GameInfo {
    std::unique_ptr<Game> (*create)(Host& host);
};

// ゲーム全体: フレーム、レジストリ、.rmpak、ループ。終了コードを返す。
int Run(const RmGameImage& image, bool smoke);

}  // namespace rm::game
