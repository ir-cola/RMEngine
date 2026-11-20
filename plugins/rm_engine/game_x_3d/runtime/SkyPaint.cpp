#include "rm_engine/game_x_3d/runtime/SkyPaint.h"

#include "base/Log.h"

#include <algorithm>

namespace rm::game_x_3d {

std::string SkyPainter::Code(const Sky& sky, const SkyLook& look, const Sources& sources, std::string& fileName) {
    if (look.builtin) {
        fileName = space3d::BuiltinSkyName(*look.builtin);
        return std::string(space3d::BuiltinSkyCode(*look.builtin));
    }
    // 自分のシェーダー: .rmsky を、ときどき読み直す (テキストエディターで保存した編集が出る)。
    if (sky.shader.Empty()) return {};
    if (sky.shader.guid != codeGuid_ || (sources.readEvery >= 0 && sources.now - codeReadAt_ >= sources.readEvery)) {
        codeGuid_ = sky.shader.guid;
        codeReadAt_ = sources.now;
        code_ = sources.code ? sources.code(sky.shader) : std::nullopt;
    }
    fileName = sources.name ? sources.name(sky.shader) : sky.shader.guid;
    if (!code_) {
        if (told_.insert("missing:" + sky.shader.guid).second) log::Warn("空のシェーダー「{}」が見つかりません", fileName);
        return {};
    }
    return *code_;
}

const space3d::SkyDraw* SkyPainter::Prepare(space3d::Renderer& renderer, const Sky& sky, const SkyLook& look, const Sources& sources) {
    if (sky.kind == SkyKind::None || !renderer.Ready()) return nullptr;
    std::string fileName;
    const std::string code = Code(sky, look, sources, fileName);
    if (code.empty()) return nullptr;
    const std::string hlsl = space3d::AssembleSky(code, fileName);
    // (間違いのあるシェーダーは毎フレーム作り直さない - 2026-11-18: 作り直していた)
    if (hlsl != compiled_ || (!draw_.program && !failed_)) {
        compiled_ = hlsl;
        std::string error;
        draw_.program = renderer.MakeSky(hlsl, &error);
        failed_ = !draw_.program;
        // 1 行で: コンソールはメッセージの 1 行目だけを出す (2026-11-18 の画面: 「…を作れません:」だけだった)。
        std::replace(error.begin(), error.end(), '\n', ' ');
        while (!error.empty() && (error.back() == ' ' || error.back() == '\r')) error.pop_back();
        std::erase(error, '\r');
        if (!draw_.program && told_.insert("compile:" + hlsl).second) log::Warn("空のシェーダー「{}」を作れません: {}", fileName, error);
    }
    if (!draw_.program) return nullptr;
    // 画像: ぐるりと 1 枚、立方体の 6 枚、またはユーザーのシェーダーが sky0 として読む 1 枚。
    draw_.textures = {};
    auto picture = [&](size_t slot, const AssetRef& ref) {
        if (!ref.Empty() && sources.picture) draw_.textures[slot] = sources.picture(ref);
    };
    if (sky.kind == SkyKind::Panorama || sky.kind == SkyKind::Custom) picture(0, sky.picture);
    if (sky.kind == SkyKind::SixImages) {
        const AssetRef* faces[space3d::kSkyTextures] = {&sky.right, &sky.left, &sky.up, &sky.down, &sky.back, &sky.front};
        for (size_t k = 0; k < space3d::kSkyTextures; ++k) picture(k, *faces[k]);
    }
    // 画像の空で、その画像がまだ無い (読み込み中): 黒でなく、クリアの色。
    const bool anyPicture = std::any_of(draw_.textures.begin(), draw_.textures.end(), [](const auto& t) { return t != nullptr; });
    if ((sky.kind == SkyKind::Panorama || sky.kind == SkyKind::SixImages) && !anyPicture) return nullptr;
    draw_.constants = look.constants;
    return &draw_;
}

}  // namespace rm::game_x_3d
