#include "rm_engine/game_x_3d/runtime/SkyPaint.h"

#include "base/Log.h"

#include <algorithm>

namespace rm::game_x_3d {

std::string SkyPainter::Code(const Sky& sky, const SkyLook& look, const Sources& sources, std::string& fileName) {
    if (look.builtin) {
        fileName = space3d::BuiltinSkyName(*look.builtin);
        return std::string(space3d::BuiltinSkyCode(*look.builtin));
    }
    // 自分のシェーダー: the .rmsky, read again now and then (an edit saved in the text editor shows).
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
    if (hlsl != compiled_ || !draw_.program) {
        compiled_ = hlsl;
        std::string error;
        draw_.program = renderer.MakeSky(hlsl, &error);
        if (!draw_.program && told_.insert("compile:" + hlsl).second) log::Warn("空のシェーダー「{}」を作れません:\n{}", fileName, error);
    }
    if (!draw_.program) return nullptr;
    // The pictures: one around, six of a cube, or the one a user's shader reads as sky0.
    draw_.textures = {};
    auto picture = [&](size_t slot, const AssetRef& ref) {
        if (!ref.Empty() && sources.picture) draw_.textures[slot] = sources.picture(ref);
    };
    if (sky.kind == SkyKind::Panorama || sky.kind == SkyKind::Custom) picture(0, sky.picture);
    if (sky.kind == SkyKind::SixImages) {
        const AssetRef* faces[space3d::kSkyTextures] = {&sky.right, &sky.left, &sky.up, &sky.down, &sky.back, &sky.front};
        for (size_t k = 0; k < space3d::kSkyTextures; ++k) picture(k, *faces[k]);
    }
    // A picture sky whose pictures are not there (yet: being read): the clear colour, not black.
    const bool anyPicture = std::any_of(draw_.textures.begin(), draw_.textures.end(), [](const auto& t) { return t != nullptr; });
    if ((sky.kind == SkyKind::Panorama || sky.kind == SkyKind::SixImages) && !anyPicture) return nullptr;
    draw_.constants = look.constants;
    return &draw_;
}

}  // namespace rm::game_x_3d
