// Tests for a sprite's image as the exported game gets it (plugins/rm_engine/game_x_2d/runtime/
// BakedImage): the file as it is with its size and import settings in front, read back the same; a
// picture not baked given back as it is; a broken header refused; keys of a newer version skipped.

#include "Test.h"

#include "rm_engine/game_x_2d/runtime/BakedImage.h"

#include <cstring>

using namespace rm;
using namespace rm::game_x_2d;

RM_TEST(BakedImage_RoundTrip) {
    const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + std::string("\0\x01\x02 the picture", 16);
    BakedImageInfo info;
    info.width = 4000;
    info.height = 30;
    info.sharp = true;
    info.maxSize = 2048;
    const std::string bytes = WriteBakedImage(info, png);
    std::string err;
    const auto back = ReadBakedImage(bytes, &err);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->info.width == 4000 && back->info.height == 30 && back->info.sharp && back->info.maxSize == 2048);
    CHECK(back->picture == png);
}

RM_TEST(BakedImage_NotBakedAndBroken) {
    std::string err;
    // A picture file as it is (a game exported before): itself, nothing known.
    const std::string png = "\x89PNG\r\n\x1a\n....";
    const auto plain = ReadBakedImage(png, &err);
    CHECK(plain && plain->picture == png && plain->info.width == 0 && !plain->info.sharp && plain->info.maxSize == 0);
    // Broken: cut short, the header longer than the bytes, the header not JSON.
    const std::string bytes = WriteBakedImage({10, 20, false, 64}, "pic");
    CHECK(!ReadBakedImage(std::string_view(bytes).substr(0, 10), &err) && !err.empty());
    std::string longer = bytes;
    const uint32_t big = 1000;
    std::memcpy(longer.data() + 8, &big, 4);
    CHECK(!ReadBakedImage(longer, &err));
    std::string notJson = bytes;
    notJson[12] = 'x';
    CHECK(!ReadBakedImage(notJson, &err));
    // A key of a newer version: skipped.
    const std::string head = R"({"width":5,"height":6,"sharp":true,"maxSize":0,"later":[1,2]})";
    std::string newer("RMIMAGE\0", 8);
    const uint32_t n = static_cast<uint32_t>(head.size());
    newer.append(reinterpret_cast<const char*>(&n), 4);
    newer += head + "pic";
    const auto read = ReadBakedImage(newer, &err);
    CHECK(read && read->info.width == 5 && read->info.height == 6 && read->info.sharp && read->picture == "pic");
}
