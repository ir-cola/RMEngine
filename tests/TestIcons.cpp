// アイコンの絵 (editor/widgets/IconAtlas): 1 枚の画像の目次を読む・表の絵がすべて 1 枚にそろっている。

#include "Test.h"

#include "editor/widgets/IconAtlas.h"
#include "platform/Shell.h"

#include <cstdio>
#include <filesystem>

using namespace rm;
using namespace rm::editor::w;

RM_TEST(Icons_ParseAtlas) {
    const auto v = json::Parse(R"({"kind":"rm_engine.icons","format":1,"cell":128,
        "icons":[{"name":"Camera","x":0,"y":0,"color":false},{"name":"Warning","x":128,"y":256,"color":true}]})");
    std::string err;
    const auto a = ParseIconAtlas(*v, &err);
    CHECK(a && err.empty() && a->cell == 128.f && a->cells.size() == 2);
    CHECK(a->Find("Warning") && a->Find("Warning")->x == 128.f && a->Find("Warning")->y == 256.f && a->Find("Warning")->color);
    CHECK(a->Find("Camera") && !a->Find("Camera")->color);
    CHECK(!a->Find("Nothing"));
    // 形が違う: 種類・大きさ・名前・同じ名前 2 つ。
    CHECK(!ParseIconAtlas(*json::Parse(R"({"kind":"x","cell":128,"icons":[]})"), &err));
    CHECK(!ParseIconAtlas(*json::Parse(R"({"kind":"rm_engine.icons","icons":[]})"), &err));
    CHECK(!ParseIconAtlas(*json::Parse(R"({"kind":"rm_engine.icons","cell":128,"icons":[{"x":0,"y":0}]})"), &err));
    CHECK(!ParseIconAtlas(*json::Parse(R"({"kind":"rm_engine.icons","cell":128,"icons":[{"name":"A","x":0,"y":0},{"name":"A","x":128,"y":0}]})"), &err) &&
          !err.empty());
}

RM_TEST(Icons_TableNames) {
    // 印 -> 絵 -> 印 が戻る。線で描く印には絵が無い。
    CHECK(IconPicture(Icon::Camera) == "Camera" && IconPicture(Icon::Scene) == "Layer" && IconPicture(Icon::Move) == "Position");
    CHECK(IconNamed("RotationLock") == Icon::RotationLock && IconNamed("Position") == Icon::Move);
    CHECK(IconPicture(Icon::Close).empty() && !IconNamed("Nothing"));
    for (const std::string_view name : IconPictures()) CHECK(IconNamed(name) && IconPicture(*IconNamed(name)) == name);
}

RM_TEST(Icons_EveryPictureIsInTheAtlas) {
    // 表の絵は、リポジトリの 1 枚の画像 (tools/make_icon_atlas.ps1 が作る) にすべてある。
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    std::string err;
    const auto bytes = platform::ReadFileBytes(root / L"resources" / L"icons" / L"icons.json", &err);
    CHECK(bytes.has_value());
    if (!bytes) return;
    const auto v = json::Parse(*bytes);
    CHECK(v.has_value());
    if (!v) return;
    const auto a = ParseIconAtlas(*v, &err);
    CHECK(a.has_value());
    if (!a) return;
    for (const std::string_view name : IconPictures()) {
        if (!a->Find(name)) {
            std::printf("  1 枚の画像に無い絵: %.*s\n", static_cast<int>(name.size()), name.data());
            CHECK(false);
        }
    }
    // 色付きの絵は警告とエラーとボーン (ほかは白 1 色でテーマの色で塗る)。
    CHECK(a->Find("Warning")->color && a->Find("Error")->color && !a->Find("Camera")->color && !a->Find("Folder")->color);
}
