#pragma once
// ゲーム UI - drawing the laid-out UI (UiLayout.h) with Direct2D and DirectWrite, onto the device context
// the window already draws with (the game's window; the editor's game view and UI view). The colours are
// the game's own data (never the editor's theme). Japanese comes out as DirectWrite falls back to the
// system's fonts for the characters a font has not.
//
//   画像  a rectangle of its colour, or the picture (WIC: platform/Image) tinted by it - in 9 pieces
//         when it has 9 スライス (the corners keep their size, the middle stretches)
//   バー  the back, then the filled part (BarFill)
//   文字  DirectWrite: a font of Windows by name, or a font file of the game (an asset: .ttf / .otf,
//         read from memory into a font collection of its own); its shadow, outline (drawn round it) and itself
//
// A button paints the picture and the bar of its own entity (its look now: ButtonNow - multiplied, and
// moved toward a replacing colour by a colour matrix), swaps its picture by state (ButtonImage), draws
// its outline round its box (ButtonOutline) and scales them with their children (in the layout).

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

// The bytes of an asset (the editor: its file; the game: what the export put in for it - an image baked
// by ゲーム制作 × 2D comes with its header: core/asset/BakedImage.h); nullopt when it is not there.
using AssetReader = std::function<std::optional<std::string>(const AssetRef&)>;

class UiRenderer {
public:
    UiRenderer() = default;
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;
    ~UiRenderer();

    // Draws `tree` (back to front); `toDc` maps the screen's DIPs to the device context's.
    void Draw(ID2D1DeviceContext* dc, platform::Gfx& gfx, const UiTree& tree, const Affine& toDc, const AssetReader& asset);
    // A plain rectangle (the editor's backdrop of the game screen), in the device context's DIPs.
    void Fill(ID2D1DeviceContext* dc, const UiBox& box, Color color);
    // Reads the pictures and fonts again when next drawn (their files changed).
    void Forget();
    // The game runs (the editor's Play, the exported game): 動画 play then - else they stop on their first
    // picture, silent.
    void SetPlaying(bool playing);
    // Something in the tree moves by itself (a 動画, 虹色): drawn again every frame.
    static bool Animates(const UiTree& tree);
    // The size of an image's file in pixels (0, 0: not read yet / not a picture).
    Vec2 ImageSize(const AssetRef& ref) const;

private:
    struct Picture {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        uint32_t width = 0, height = 0;  // the file's pixels
        bool tried = false;
    };
    struct Font {
        Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
        std::wstring family;
        bool tried = false;
        size_t hash = 0;  // of the file's bytes it was made from (Forget: made again only when they changed)
    };

    void Begin(ID2D1DeviceContext* dc);
    ID2D1SolidColorBrush* Brush(Color c);
    const Picture* PictureOf(platform::Gfx& gfx, const AssetRef& ref, const AssetReader& asset);
    const Font* FontOf(platform::Gfx& gfx, const AssetRef& ref, const AssetReader& asset);
    IDWriteTextFormat* Format(platform::Gfx& gfx, const std::wstring& family, IDWriteFontCollection* collection, float size, bool bold, bool italic);
    void DrawImage(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint);
    void DrawPiece(ID2D1Bitmap* bitmap, const D2D1_RECT_F& src, const UiBox& dst, const Affine& full, const UiPaint& paint);
    void DrawOutline(const UiNode& n, const Affine& full, const UiStroke& stroke);
    // UI-5 (見た目): what makes its shape - 背景, 画像, バー, 文字 - drawn as it is, or into a command list
    // that the 影, the ぼかし and the 光 are made from (Direct2D's Shadow and GaussianBlur effects).
    void DrawBody(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint);
    void DrawVideo(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint);
    // 虹色 now for one of its colours (UiRainbow's flag for it), and a colour with it (its alpha kept).
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
    ID2D1DeviceContext* owner_ = nullptr;  // the context brush_ and tint_ were made for
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1Effect> tint_;
    Microsoft::WRL::ComPtr<ID2D1Effect> shadow_, glow_, blur_;
    // Recording a command list: it keeps the brushes and effects it was drawn with - not copies - so each
    // draw then gets one of its own (kept here until the list is drawn).
    bool fresh_ = false;
    bool playing_ = false;
    double time_ = 0;  // seconds (虹色)
    struct Video {
        std::unique_ptr<VideoPlayer> player;
        bool failed = false;  // told once
        bool seen = false;    // drawn this frame (one not drawn stops)
    };
    std::unordered_map<std::string, Video> videos_;  // by entity and asset
    std::vector<Microsoft::WRL::ComPtr<IUnknown>> keep_;
    std::unordered_map<std::string, Picture> pictures_;  // by asset id
    std::unordered_map<std::string, Font> fonts_;        // by asset id
    std::map<std::tuple<std::wstring, IDWriteFontCollection*, float, bool, bool>, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    uint64_t generation_ = 0;  // platform::Gfx::Generation the bitmaps, the brush and the effect were made in
    Microsoft::WRL::ComPtr<IUnknown> fontLoader_;  // IDWriteInMemoryFontFileLoader (registered on the factory)
    Microsoft::WRL::ComPtr<IDWriteFactory> loaderFactory_;
};

}  // namespace rm::game_ui
