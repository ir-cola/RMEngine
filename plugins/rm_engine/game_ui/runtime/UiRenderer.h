#pragma once
// ゲーム UI - 配置した UI (UiLayout.h) を Direct2D と DirectWrite で、ウィンドウがすでに描いている
// デバイスコンテキスト (ゲームのウィンドウ。エディターのゲームビューと UI ビュー) に描く。色は
// ゲーム自身のデータ (エディターのテーマは決して使わない)。日本語は、フォントにない文字を DirectWrite が
// システムのフォントで補って出す。
//
//   画像  その色の長方形、またはその色でティントした絵 (WIC: platform/Image) - 9 スライス が
//         あるときは 9 つの部分で (角は大きさを保ち、真ん中が伸びる)
//   バー  後ろ、次に塗られた部分 (BarFill)
//   文字  DirectWrite: 名前で指定する Windows のフォント、またはゲームのフォントファイル (アセット:
//         .ttf / .otf、メモリから専用のフォントコレクションに読む)。その影、縁取り (まわりに描く)、本体
//
// ボタンは自分のエンティティの絵とバーを塗り (今の見た目: ButtonNow - 掛け合わせ、カラー
// マトリックスで置き換える色へ寄せる)、状態で絵を差し替え (ButtonImage)、箱のまわりに縁取りを描き
// (ButtonOutline)、子と一緒にそれらを拡大縮小する (レイアウトで)。

#include "base/Ref.h"
#include "platform/Gfx.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"
#include "rm_engine/game_ui/runtime/UiVideo.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>

namespace rm::game_ui {

// アセットのバイト列 (エディター: そのファイル。ゲーム: 書き出しが入れたもの - ゲーム制作 × 2D が
// 焼いた画像はヘッダー付き: core/asset/BakedImage.h)。なければ nullopt。
using AssetReader = std::function<std::optional<std::string>(const AssetRef&)>;

class UiRenderer {
public:
    UiRenderer() = default;
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;
    ~UiRenderer();

    // `tree` を奥から手前へ描く。`toDc` は画面の DIP をデバイスコンテキストのものへ写す。
    void Draw(ID2D1DeviceContext* dc, platform::Gfx& gfx, const UiTree& tree, const Affine& toDc, const AssetReader& asset);
    // ただの長方形 (エディターのゲーム画面の背景)、デバイスコンテキストの DIP で。
    void Fill(ID2D1DeviceContext* dc, const UiBox& box, Color color);
    // 次に描くときに絵とフォントを読み直す (ファイルが変わった)。
    void Forget();
    // ゲームが動いている (エディターの Play、書き出したゲーム): 動画 はそのとき再生する - そうでなければ
    // 最初の絵で止まり、音もなし。
    void SetPlaying(bool playing);
    // ツリーの何かが自分で動く (動画、虹色): 毎フレーム描き直す。
    static bool Animates(const UiTree& tree);
    // 画像のファイルのピクセルでの大きさ (0, 0: まだ読んでいない / 絵ではない)。
    Vec2 ImageSize(const AssetRef& ref) const;

private:
    struct Picture {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        uint32_t width = 0, height = 0;  // ファイルのピクセル
        bool tried = false;
    };
    struct Font {
        Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
        std::wstring family;
        bool tried = false;
        size_t hash = 0;  // 作ったときのファイルのバイト列の (Forget: 変わったときだけ作り直す)
    };

    void Begin(ID2D1DeviceContext* dc);
    ID2D1SolidColorBrush* Brush(Color c);
    const Picture* PictureOf(platform::Gfx& gfx, const AssetRef& ref, const AssetReader& asset);
    const Font* FontOf(platform::Gfx& gfx, const AssetRef& ref, const AssetReader& asset);
    IDWriteTextFormat* Format(platform::Gfx& gfx, const std::wstring& family, IDWriteFontCollection* collection, float size, bool bold, bool italic);
    void DrawImage(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint);
    void DrawPiece(ID2D1Bitmap* bitmap, const D2D1_RECT_F& src, const UiBox& dst, const Affine& full, const UiPaint& paint);
    void DrawOutline(const UiNode& n, const Affine& full, const UiStroke& stroke);
    // UI-5 (見た目): 形を作るもの - 背景、画像、バー、文字 - をそのまま描くか、影・ぼかし・光の
    // 元になるコマンドリストに描く (Direct2D の Shadow と GaussianBlur エフェクト)。
    void DrawBody(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint);
    void DrawVideo(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint);
    // 色の 1 つに対する今の 虹色 (UiRainbow のそのフラグ) と、それを付けた色 (alpha は保つ)。
    std::optional<Color> RainbowFor(const UiNode& n, bool UiRainbow::* part) const;
    static Color Rainbowed(Color c, const std::optional<Color>& rainbow);
    void DrawWithEffects(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint);
    void FillShape(const UiNode& n, const Affine& full, Color color);
    void DrawBorder(const UiNode& n, const Affine& full, const UiStroke& stroke);
    void DrawBevel(const UiNode& n, const Affine& full, bool inset);
    ID2D1Effect* Effect(Microsoft::WRL::ComPtr<ID2D1Effect>& slot, const CLSID& id);
    void DrawText(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset);
    void DrawFocus(const UiNode& n, const Affine& full);

    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1DeviceContext* owner_ = nullptr;  // brush_ と tint_ を作ったコンテキスト
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1Effect> tint_;
    Microsoft::WRL::ComPtr<ID2D1Effect> shadow_, glow_, blur_;
    // コマンドリストへの記録: 描いたブラシとエフェクトを (コピーでなく) そのまま持つので、描くたびに
    // 専用のものを使う (リストを描くまでここに保つ)。
    bool fresh_ = false;
    bool playing_ = false;
    double time_ = 0;  // seconds (虹色)
    struct Video {
        std::unique_ptr<VideoPlayer> player;
        bool failed = false;  // 1 度だけ知らせた
        bool seen = false;    // このフレームに描いた (描かれなかったものは止まる)
    };
    std::unordered_map<std::string, Video> videos_;  // エンティティとアセットごと
    std::vector<Microsoft::WRL::ComPtr<IUnknown>> keep_;
    std::unordered_map<std::string, Picture> pictures_;  // アセット ID ごと
    std::unordered_map<std::string, Font> fonts_;        // アセット ID ごと
    std::map<std::tuple<std::wstring, IDWriteFontCollection*, float, bool, bool>, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    uint64_t generation_ = 0;  // ビットマップ・ブラシ・エフェクトを作った platform::Gfx::Generation
    Microsoft::WRL::ComPtr<IUnknown> fontLoader_;  // IDWriteInMemoryFontFileLoader (ファクトリーに登録)
    Microsoft::WRL::ComPtr<IDWriteFactory> loaderFactory_;
};

}  // namespace rm::game_ui
