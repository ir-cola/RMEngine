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
// A button tints the picture and the bar of its own entity (ButtonColor) and scales them with their
// children (in the layout).

#include "base/Ref.h"
#include "platform/Gfx.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"

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
    };

    void Begin(ID2D1DeviceContext* dc);
    ID2D1SolidColorBrush* Brush(Color c);
    const Picture* PictureOf(platform::Gfx& gfx, const AssetRef& ref, const AssetReader& asset);
    const Font* FontOf(platform::Gfx& gfx, const AssetRef& ref, const AssetReader& asset);
    IDWriteTextFormat* Format(platform::Gfx& gfx, const std::wstring& family, IDWriteFontCollection* collection, float size, bool bold);
    void DrawImage(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, Color tint);
    void DrawPiece(ID2D1Bitmap* bitmap, const D2D1_RECT_F& src, const UiBox& dst, const Affine& full, Color color);
    void DrawText(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset);

    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1DeviceContext* owner_ = nullptr;  // the context brush_ and tint_ were made for
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1Effect> tint_;
    std::unordered_map<std::string, Picture> pictures_;  // by asset id
    std::unordered_map<std::string, Font> fonts_;        // by asset id
    std::map<std::tuple<std::wstring, IDWriteFontCollection*, float, bool>, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    Microsoft::WRL::ComPtr<IUnknown> fontLoader_;  // IDWriteInMemoryFontFileLoader (registered on the factory)
    Microsoft::WRL::ComPtr<IDWriteFactory> loaderFactory_;
};

}  // namespace rm::game_ui
