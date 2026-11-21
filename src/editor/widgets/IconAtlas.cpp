#include "editor/widgets/IconAtlas.h"

#include <array>
#include <format>

namespace rm::editor::w {

namespace {

struct Pair {
    Icon icon;
    std::string_view picture;
};

// どの印にどの絵を使うか (1 か所)。絵の名前は resources/icons/source/ のファイルの名前。
constexpr Pair kTable[] = {
    // 物の種類 (階層・シーンビューの印・アセット)
    {Icon::Camera, "Camera"},
    {Icon::Light, "Light"},
    {Icon::Cube, "Box"},
    {Icon::Scene, "Layer"},
    {Icon::Folder, "Folder"},
    {Icon::Image, "Picture"},
    {Icon::Audio, "Sound"},
    // ボタン
    {Icon::Dots, "TabOption"},
    {Icon::Plus, "NewProject"},
    // 道具 (シーンビューの左の列)・インスペクターの項目
    {Icon::Cursor, "Cursor"},
    {Icon::Move, "Position"},
    {Icon::Rotate, "Rotation"},
    {Icon::Scale, "Scale"},
    {Icon::Grid, "Grid"},
    {Icon::RotationLock, "RotationLock"},
    // 知らせ (色付き)
    {Icon::Warning, "Warning"},
    {Icon::Error, "Error"},
    // 絵はあるが、まだどこにも出していない物 (使う所ができたら呼ぶ)
    {Icon::Back, "Back"},
    {Icon::Undo, "Undo"},
    {Icon::Redo, "Redo"},
    {Icon::Lock, "Lock"},
    {Icon::Unlock, "Unlock"},
    {Icon::Search, "Search"},
    {Icon::Trash, "Trash"},
    {Icon::Color, "Color"},
    {Icon::Settings, "Option"},
    {Icon::Help, "Help"},
    {Icon::Human, "Human"},
    {Icon::Prefab, "Prefab"},
    {Icon::BoneMoveX, "BonePositionX"},
    {Icon::BoneMoveY, "BonePositionY"},
    {Icon::BoneMoveZ, "BonePositionZ"},
    {Icon::BoneRotateX, "BoneRotationX"},
    {Icon::BoneRotateY, "BoneRotationY"},
    {Icon::BoneRotateZ, "BoneRotationZ"},
};

constexpr auto kPictures = [] {
    std::array<std::string_view, std::size(kTable)> out{};
    for (size_t i = 0; i < std::size(kTable); ++i) out[i] = kTable[i].picture;
    return out;
}();

}  // namespace

const IconCell* IconAtlas::Find(std::string_view name) const {
    const auto it = cells.find(name);
    return it == cells.end() ? nullptr : &it->second;
}

std::optional<IconAtlas> ParseIconAtlas(const json::Value& v, std::string* error) {
    auto fail = [&](std::string why) -> std::optional<IconAtlas> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    if (!v.IsObject() || v.Get("kind").AsString() != "rm_engine.icons") return fail("kind が rm_engine.icons ではありません");
    IconAtlas a;
    a.cell = static_cast<float>(v.Get("cell").AsDouble());
    if (!(a.cell > 0)) return fail("cell (1 マスの大きさ) がありません");
    for (const json::Value& e : v.Get("icons").Items()) {
        const std::string& name = e.Get("name").AsString();
        if (name.empty() || !e.Get("x").IsNumber() || !e.Get("y").IsNumber()) return fail("名前か位置の無い絵があります");
        IconCell cell;
        cell.x = static_cast<float>(e.Get("x").AsDouble());
        cell.y = static_cast<float>(e.Get("y").AsDouble());
        cell.color = e.Get("color").AsBool();
        if (!a.cells.emplace(name, cell).second) return fail(std::format("同じ名前の絵が 2 つあります: {}", name));
    }
    return a;
}

std::string_view IconPicture(Icon icon) {
    for (const Pair& p : kTable) {
        if (p.icon == icon) return p.picture;
    }
    return {};
}

std::optional<Icon> IconNamed(std::string_view picture) {
    for (const Pair& p : kTable) {
        if (p.picture == picture) return p.icon;
    }
    return std::nullopt;
}

std::span<const std::string_view> IconPictures() { return kPictures; }

}  // namespace rm::editor::w
