// 書き出すゲーム用に、スプライトの画像を焼く (game_production/editor/GameExport.h: ExportBaker):
// ファイルはそのまま (それ以上は圧縮しない: ゲームはエディターと同じく WIC で読む) に、ファイル自身の
// 大きさと、取り込みの設定 くっきり / 大きさの上限 を前に付ける (runtime/BakedImage.h) - ゲームが
// 2D シーンビューの見せるとおりの大きさと方法でスプライトを描くように。

#include "rm_engine/game_x_2d/editor/ExportBake2D.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "core/asset/ImportSettings.h"
#include "platform/Image.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "core/asset/BakedImage.h"

#include <algorithm>
#include <format>
#include <optional>
#include <thread>

namespace rm::game_x_2d {

using asset::BakedImageInfo;
using asset::kMaxImageSize;
using asset::ReadBakedImage;
using asset::WriteBakedImage;

namespace {

using game_production::BakeResult;

BakeResult Bake(const game_production::ExportAsset& a, game_production::ExportBaked& out, std::string* error) {
    if (asset::KindOf(PathToUtf8(a.file.filename()), false) != asset::Kind::Image) return BakeResult::NotMine;
    std::string why;
    const auto bytes = platform::ReadFileBytes(a.file, &why);
    if (!bytes) {
        if (error) *error = why;
        return BakeResult::Failed;
    }
    // ファイル自身の大きさ (2D シーンビューが読むとおりに) - 専用のスレッドで: その WIC ファクトリー
    // (と COM) はそれと一緒に終わる。どのスレッドが焼いても (ヘッドレスの書き出しではメインスレッド
    // で、その thread_local のファクトリーは、プロセスの最後に COM の後で手放されてしまう)。
    std::optional<platform::Image> whole;
    std::thread([&] { whole = platform::LoadImageFile(platform::ThreadImagingFactory(), a.file, platform::PixelOrder::PremultipliedBgra, {}, &why); }).join();
    if (!whole) {
        if (error) *error = why;
        return BakeResult::Failed;
    }
    const auto imp = asset::ReadImport<asset::ImageImport>(a.file);
    BakedImageInfo info;
    info.width = whole->width;
    info.height = whole->height;
    info.sharp = imp.filter == asset::TextureFilter::Sharp;
    info.maxSize = static_cast<uint32_t>(std::max(imp.maxSize, 1));
    out.path = "Baked/" + a.id + ".rmimage";
    out.files.push_back({out.path, WriteBakedImage(info, *bytes)});
    out.note = std::format("画像を焼きました: {} ({} x {}{}・上限 {})", a.rel, info.width, info.height, info.sharp ? "・くっきり" : "", info.maxSize);
    return BakeResult::Done;
}

}  // namespace

void RegisterExportBake(editor::PluginRegistry& registry) {
    static const game_production::ExportBaker kBaker{&Bake};
    registry.Contribute(game_production::kExportBakePoint, kBaker);
}

}  // namespace rm::game_x_2d
