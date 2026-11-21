// プラグインのダウンロード (docs/PLUGIN_DISTRIBUTION.md 段階 1) のテスト: zip と deflate を読む・一覧
// index.json・版の比べ・何を出すか・zip の中のパスの検査・展開・plugin.json の版・ローカルのファイルを読む。
// zip と deflate のバイト列は Python の zlib / zipfile で作った (固定ハフマン・動的ハフマン・そのままの塊)。

#include "Test.h"

#include "base/Json.h"
#include "base/Utf.h"
#include "base/Zip.h"
#include "editor/plugin/PluginDownload.h"
#include "editor/plugin/PluginIndex.h"
#include "editor/plugin/PluginManifest.h"
#include "platform/Http.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace rm;
using namespace rm::editor;
namespace fs = std::filesystem;

namespace {

const char kFixedHex[] =
    "cb48cdc9c957c8409000";
const char kDynamicHex[] =
    "7d57cb8ddc300cbdab0a17a00ad44db019240136b34192450e024f03a48905721eb88674b3a7ed2236bf8f923630662c5b24c5cfe3c70f8f4fcf1fb7"
    "cbf5d397eb657bbdfd7ebdfd7dbdfdd968fbf9f9f2f5b27d7b7c3e76b6b2bdbddcede18159ba529c1b3fae4fbf3243488a956e31b1ae4fe6201009e5"
    "108da4745c76583fae911e0e3fc51533e6fb87a487be053bba90081319816ff6e31dcb167b41e31644a61613bebdec074fb6a92afbb9d7f95f9ef968"
    "e6aa76723b96e81cd02ff81aaf79c3ddb474a60ab5f3ddfc7d72209980ca22fa712fca65675ac8c58aca7787862e54d9fd20778972403b2ede25766a"
    "09135a829c9c62a1dcb3e63dccdc31f25524b90ff9e50a7e05b55ebae3b41e859581fb3e2b2d2a12bce1f33396f4496f0aea04db9d518fc0e53fd99e"
    "92f3d4c422d2b3979a2ada396d5a3aa032cceb8093ffafc0e7b4ce3c4cd5932c9b9e6b4542455fa4e492bc88f03e839bc44dc61599961343228f3f66"
    "cbd1d1a7e698773c97e3015405db2310eefea0a881188a2c3903e4699d78e5a186e4f357818ecc0f95af62f8e71b62d50b09888afa21da998e6e6514"
    "a4965c59dd25a90e2e630fe5d2df9d102d8043724329e24373dfa86106c00af7296b545362e6cc6ee1fe9c8c7a4b89d6d56e8a17455fd2584d254a49"
    "8a7916caa0348dc98462a12b580d22108e130d74c7de91b3b8641d1a049058ff911cd41e9681e0a28e7037986975ed45097743ab85c3f06be20d92a7"
    "a48938900b3530d14916dfe7ad1d1ad55dff15d28e73ccac1d293c5bfd5d59236f826a1cac5b3476a13ccff0293570ebc58346806137f2046b6b0dbc"
    "2696d483dad038c6de3574ae45aab7d4980d58772fa6565246c912037769aec979d4c11122451dfd50d77d99aca22e673d03c2ee66e05991abef4519"
    "c8604ec2fed1cde1c851e669259c1287e81c9772578a054a4427a44a155942e1e79a8a87409c920c2bc0506fb074590931f7a52ee205d9a15ed26440"
    "7e66594d560dd21b1a45e11991e62820f2861ae70ab477c23f90cdd31c99ffb14644a7cac5cdfd095ec364893ebdace529ad1110cb59cb83494323af"
    "d9aa8cb5c97b7dca4c504b7193bf382470358a49e3be337fcf34749186b647042d62457144a919379d6b204feddba3f0f1c64e3ced34d623ab8a5075"
    "6553828f937f740cf23bb8d8dd1e00a851012d171c4df0d0f0a3acc6508313150e64cd93644fa04cb3b60ab35199a648a5a95163e91f5a7d2e2a3bac"
    "94dcf5d7be049f65d00c636ac626fc0f";
const char kStoredHex[] =
    "010d00f2ff73746f72656420627974657321";
const char kZipHex[] =
    "504b0304140000000800000021005c97abdc44000000490000000b000000706c7567696e2e6a736f6e0dca310a80300c05d0bb6496521cbd4c09fab5"
    "85268526ba887737ebe3bdc4b7d73169a329057a35052da42c08f20a417198873d98d68606e794d31ab28f23d6c9ddf0fd504b030414000000000000"
    "002100000000000000000000000000070000007468656d65732f504b0304140000000000000021005c59eb3b07000000070000000d0000007468656d"
    "65732f612e6a736f6e7b2278223a317d504b030414000000080000002100472d879b93030000381000000c0000006c616e672f656e2e6a736f6e7d57"
    "cb8ddc300cbdab0a17a00ad44db019240136b34192450e024f03a48905721eb88674b3a7ed2236bf8f923630662c5b24c5cfe3c70f8f4fcf1fb7cbf5"
    "d397eb657bbdfd7ebdfd7dbdfdd968fbf9f9f2f5b27d7b7c3e76b6b2bdbddcede18159ba529c1b3fae4fbf3243488a956e31b1ae4fe6201009e5108d"
    "a4745c76583fae911e0e3fc51533e6fb87a487be053bba90081319816ff6e31dcb167b41e31644a61613bebdec074fb6a92afbb9d7f95f9ef968e6aa"
    "76723b96e81cd02ff81aaf79c3ddb474a60ab5f3ddfc7d72209980ca22fa712fca65675ac8c58aca7787862e54d9fd20778972403b2ede25766a0913"
    "5a829c9c62a1dcb3e63dccdc31f25524b90ff9e50a7e05b55ebae3b41e859581fb3e2b2d2a12bce1f33396f4496f0aea04db9d518fc0e53fd99e92f3"
    "d4c422d2b3979a2ada396d5a3aa032cceb8093ffafc0e7b4ce3c4cd5932c9b9e6b4542455fa4e492bc88f03e839bc44dc61599961343228f3f66cbd1"
    "d1a7e698773c97e3015405db2310eefea0a881188a2c3903e4699d78e5a186e4f357818ecc0f95af62f8e71b62d50b09888afa21da998e6e6514a496"
    "5c59dd25a90e2e630fe5d2df9d102d8043724329e24373dfa86106c00af7296b545362e6cc6ee1fe9c8c7a4b89d6d56e8a17455fd2584d254a498a79"
    "16caa0348dc98462a12b580d22108e130d74c7de91b3b8641d1a049058ff911cd41e9681e0a28e7037986975ed45097743ab85c3f06be20d92a7a489"
    "38900b3530d14916dfe7ad1d1ad55dff15d28e73ccac1d293c5bfd5d59236f826a1cac5b3476a13ccff0293570ebc58346806137f2046b6b0dbc2696"
    "d483dad038c6de3574ae45aab7d4980d581686dd4bca285962e02ecd35398f3a3842a4a8a31ffc29f765b28aba9cf50c08bb9b816745aebe17652083"
    "3909fb473787234741508e8e8b43748e4bb92bc50225a21352a58a2ca1f0734dc543204e49861560a83758baac8498fb5217f182ec502f6932203fb3"
    "ac26ab06e90d8da2f08c4873141079438d7305da3be11fc8e6698eccff5823a253e5e2e6fe04af61b2449f5ed6f294d60888e5ace5c1a4a191d76c55"
    "c6dae4bd3e6526a8a5b8c95f1c12b81ac5a471df99bf671aba4843db238216b1a238a2d48c9bce3590a7f6ed51f87863279e761aeb915545a8bab229"
    "c1c7c93f3a06f91d5cec6e0f00d4a880960b8e267868f851e63c254d543890354f923d8132cdda2acc46659a2295a6468da57f68f5b9a8ecb05272d7"
    "5ffb127c9641338ca9199bf03f504b01021400140000000800000021005c97abdc44000000490000000b000000000000000000000080010000000070"
    "6c7567696e2e6a736f6e504b010214001400000000000000210000000000000000000000000007000000000000000000000080016d0000007468656d"
    "65732f504b01021400140000000000000021005c59eb3b07000000070000000d00000000000000000000008001920000007468656d65732f612e6a73"
    "6f6e504b0102140014000000080000002100472d879b93030000381000000c00000000000000000000008001c40000006c616e672f656e2e6a736f6e"
    "504b05060000000004000400e3000000810400000000";
const char kEvilHex[] =
    "504b03041400000000000000210043bfa6a302000000020000000b000000706c7567696e2e6a736f6e7b7d504b0304140000000000000021008316dc"
    "8c01000000010000000b0000002e2e2f6576696c2e74787478504b010214001400000000000000210043bfa6a302000000020000000b000000000000"
    "0000000000800100000000706c7567696e2e6a736f6e504b01021400140000000000000021008316dc8c01000000010000000b000000000000000000"
    "000080012b0000002e2e2f6576696c2e747874504b0506000000000200020072000000550000000000";
const char kNomanifestHex[] =
    "504b030414000000000000002100ac2a93d802000000020000000a000000726561646d652e7478746869504b0102140014000000000000002100ac2a"
    "93d802000000020000000a0000000000000000000000800100000000726561646d652e747874504b05060000000001000100380000002a0000000000";

std::string FromHex(const char* hex) {
    std::string out;
    auto v = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) out += static_cast<char>(v(hex[i]) * 16 + v(hex[i + 1]));
    return out;
}

json::Value Parse(const char* text) { return *json::Parse(text); }

const char* kIndex = R"({
  "kind": "rm_engine.plugin_index", "formatVersion": 1, "updated": "2026-11-21", "future": 1,
  "plugins": [
    { "author": "rm_engine", "name": "theme_paper", "displayName": "紙",
      "versions": [
        { "version": "0.0.2", "engine": { "min": "0.0.3" }, "code": false, "url": "plugins/paper-0.0.2.zip", "size": 10,
          "sha256": "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff" },
        { "version": "0.0.9", "engine": { "min": "9.0.0" }, "code": false, "url": "plugins/paper-0.0.9.zip", "size": 10,
          "sha256": "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff" },
        { "version": "0.1", "url": "x", "size": 1, "sha256": "00" } ] },
    { "author": "someone", "name": "theme_new", "versions": [
        { "version": "1.0.0", "url": "https://example.com/new.zip", "size": 5,
          "sha256": "00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff",
          "requires": [ { "author": "rm_engine", "name": "game_production" } ] } ] },
    { "author": "rm_engine", "name": "weather", "versions": [
        { "version": "0.0.5", "code": true, "url": "w.zip", "size": 5,
          "sha256": "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff" } ] },
    { "author": "Bad Author", "name": "x" }
  ],
  "engine": { "version": "0.0.4", "url": "engine.zip", "size": 0, "sha256": "" }
})";

PluginManifest Manifest(const char* author, const char* name, const char* version) {
    PluginManifest m;
    m.id = {author, name};
    m.displayName = name;
    m.version = version;
    return m;
}

}  // namespace

RM_TEST(Zip_InflateFixedDynamicAndStored) {
    zip::Problem why = zip::Problem::None;
    auto fixed = zip::Inflate(FromHex(kFixedHex), 1000, &why);
    CHECK(fixed && *fixed == "hello hello hello");
    auto dynamic = zip::Inflate(FromHex(kDynamicHex), 1u << 20, &why);
    CHECK(dynamic && dynamic->size() == 4152 && zip::Crc32(*dynamic) == 0x9b872d47u);
    auto stored = zip::Inflate(FromHex(kStoredHex), 1000, &why);
    CHECK(stored && *stored == "stored bytes!");
    // 大きさの上限・途中で切れた物・形の違う物
    CHECK(!zip::Inflate(FromHex(kDynamicHex), 100, &why) && why == zip::Problem::TooBig);
    const std::string cut = FromHex(kDynamicHex).substr(0, 300);
    CHECK(!zip::Inflate(cut, 1u << 20, &why) && why == zip::Problem::Corrupt);
    CHECK(!zip::Inflate(std::string("\x07", 1), 100, &why) && why == zip::Problem::Corrupt);  // (塊の種類 3: 無い)
    CHECK(zip::Crc32("") == 0 && zip::Crc32("123456789") == 0xCBF43926u);
}

RM_TEST(Zip_DirectoryAndEntries) {
    const std::string bytes = FromHex(kZipHex);
    zip::Problem why = zip::Problem::None;
    auto entries = zip::ReadDirectory(bytes, &why);
    CHECK(entries && entries->size() == 4);
    if (!entries || entries->size() != 4) return;
    CHECK((*entries)[0].name == "plugin.json" && (*entries)[0].method == 8);
    CHECK((*entries)[1].name == "themes/" && (*entries)[1].directory);
    CHECK((*entries)[2].name == "themes/a.json" && (*entries)[2].method == 0);
    auto manifest = zip::ReadEntry(bytes, (*entries)[0], 1u << 20, &why);
    CHECK(manifest && manifest->find("theme_test") != std::string::npos);
    auto stored = zip::ReadEntry(bytes, (*entries)[2], 1u << 20, &why);
    CHECK(stored && *stored == "{\"x\":1}");
    auto lang = zip::ReadEntry(bytes, (*entries)[3], 1u << 20, &why);
    CHECK(lang && lang->size() == 4152);
    CHECK(!zip::ReadEntry(bytes, (*entries)[3], 100, &why) && why == zip::Problem::TooBig);
    // 中身を 1 バイト変える: CRC が合わない
    std::string broken = bytes;
    const size_t at = broken.find("{\"x\":1}");
    broken[at + 5] = '2';
    CHECK(!zip::ReadEntry(broken, (*entries)[2], 1u << 20, &why) && why == zip::Problem::Crc);
    CHECK(!zip::ReadDirectory("not a zip at all, just text....", &why) && why == zip::Problem::NotZip);
    CHECK(!zip::ReadDirectory(bytes.substr(0, bytes.size() - 30), &why));
}

RM_TEST(PluginIndex_Versions) {
    CHECK(ParseVersion("0.0.3") && (*ParseVersion("1.20.300"))[1] == 20);
    CHECK(!ParseVersion("") && !ParseVersion("1.2") && !ParseVersion("1.2.3.4") && !ParseVersion("1.a.3") && !ParseVersion("1..3") &&
          !ParseVersion("v1.2.3") && !ParseVersion("1.2.3 "));
    CHECK(CompareVersions("0.10.0", "0.9.0") == 1 && CompareVersions("0.0.3", "0.0.3") == 0 && CompareVersions("0.0.2", "0.1.0") == -1);
    CHECK(!CompareVersions("x", "0.0.1"));
    CHECK(IsNewerVersion("0.0.2", "0.0.1") && !IsNewerVersion("0.0.1", "0.0.1") && !IsNewerVersion("bad", "0.0.1"));
    CHECK(IsNewerVersion("0.0.1", "") && !IsNewerVersion("", ""));  // (版を書いていない古い plugin.json より新しい)
}

RM_TEST(PluginIndex_Parse) {
    std::string err;
    auto index = ParsePluginIndex(Parse(kIndex), &err);
    CHECK(index.has_value());
    if (!index) return;
    CHECK(index->plugins.size() == 3);  // (author の形が違う物は飛ばす)
    CHECK((index->plugins[0].id == PluginId{"rm_engine", "theme_paper"} && index->plugins[0].displayName == "紙"));
    CHECK(index->plugins[0].versions.size() == 2);  // ("0.1" は飛ばす)
    CHECK(index->plugins[1].displayName == "theme_new" && index->plugins[1].versions[0].requirements.size() == 1);
    CHECK(index->skipped.size() == 2);
    CHECK(index->engine && index->engine->version == "0.0.4");
    // 一覧ではない・新しすぎる形
    CHECK((!ParsePluginIndex(Parse(R"({"kind":"other","formatVersion":1})"), &err) && !err.empty()));
    CHECK((!ParsePluginIndex(Parse(R"({"kind":"rm_engine.plugin_index","formatVersion":2})"), &err)));
    CHECK((!ParsePluginIndex(Parse(R"({"kind":"rm_engine.plugin_index"})"), &err)));
    // 同じプラグインが 2 回
    auto twice = ParsePluginIndex(Parse(R"({"kind":"rm_engine.plugin_index","formatVersion":1,"plugins":[
        {"author":"rm_engine","name":"theme_paper","versions":[]},{"author":"rm_engine","name":"theme_paper","versions":[]}]})"), &err);
    CHECK(twice && twice->plugins.size() == 1 && twice->skipped.size() == 1);
}

RM_TEST(PluginIndex_FitsAndOffers) {
    std::string err;
    auto index = ParsePluginIndex(Parse(kIndex), &err);
    if (!index) {
        CHECK(false);
        return;
    }
    const IndexPlugin& paper = index->plugins[0];
    CHECK(EngineFits(paper.versions[0], "0.0.3") && !EngineFits(paper.versions[0], "0.0.2") && !EngineFits(paper.versions[1], "0.0.3"));
    CHECK(NewestFitting(paper, "0.0.3", false) == &paper.versions[0]);  // (0.0.9 はエンジン 9.0.0 から)
    CHECK(NewestFitting(paper, "9.1.0", false) == &paper.versions[1]);
    CHECK(NewestFitting(paper, "0.0.1", false) == nullptr);
    CHECK(NewestFitting(index->plugins[2], "0.0.3", false) == nullptr && NewestFitting(index->plugins[2], "0.0.3", true) != nullptr);
    CHECK(EngineUpdateAvailable(*index, "0.0.3") && !EngineUpdateAvailable(*index, "0.0.4"));

    // 入っていない・エンジンの写しは古い: 両方出る (紙は「インストール」、新しい物も)
    const std::vector<PluginManifest> engineCopies{Manifest("rm_engine", "theme_paper", "0.0.1")};
    auto offers = PluginOffers(*index, {}, engineCopies, "0.0.3");
    CHECK(offers.size() == 2 && offers[0].kind == OfferKind::Install && offers[0].package->version == "0.0.2");
    // 入っていて古い: 更新。同じ版: 出ない。コードのある物 (天候) は出ない
    auto update = PluginOffers(*index, {Manifest("rm_engine", "theme_paper", "0.0.1")}, engineCopies, "0.0.3");
    CHECK(update.size() == 2 && update[0].kind == OfferKind::Update && update[0].currentVersion == "0.0.1");
    auto same = PluginOffers(*index, {Manifest("rm_engine", "theme_paper", "0.0.2"), Manifest("someone", "theme_new", "1.0.0")}, engineCopies, "0.0.3");
    CHECK(same.empty());
    // エンジンの写しが同じ版で入っていない: ふつうのインストールで足りる
    auto builtinSame = PluginOffers(*index, {}, {Manifest("rm_engine", "theme_paper", "0.0.2")}, "0.0.3");
    CHECK(builtinSame.size() == 1 && builtinSame[0].plugin->id.name == "theme_new");
}

RM_TEST(PluginIndex_UrlsAndHashes) {
    CHECK(ResolveUrl("https://ir-cola.github.io/rm_engine/index.json", "plugins/a.zip") == "https://ir-cola.github.io/rm_engine/plugins/a.zip");
    CHECK(ResolveUrl("https://ir-cola.github.io/rm_engine/index.json", "/files/a.zip") == "https://ir-cola.github.io/files/a.zip");
    CHECK(ResolveUrl("https://ir-cola.github.io/rm_engine/index.json", "https://github.com/x/a.zip") == "https://github.com/x/a.zip");
    CHECK(ResolveUrl("C:\\site\\rm_engine\\index.json", "plugins/a.zip") == "C:\\site\\rm_engine\\plugins/a.zip");
    CHECK(ResolveUrl("C:\\site\\index.json", "D:\\other\\a.zip") == "D:\\other\\a.zip");
    const std::string h = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    CHECK(SameSha256(h, "00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff"));
    CHECK(!SameSha256(h, h.substr(1)) && !SameSha256(h, "") && !SameSha256(std::string(64, 'g'), std::string(64, 'g')));
    std::array<uint8_t, 32> d{};
    d[0] = 0xab;
    d[31] = 0x01;
    CHECK(Sha256Hex(d).size() == 64 && Sha256Hex(d).substr(0, 2) == "ab" && Sha256Hex(d).substr(62) == "01");
}

RM_TEST(PluginIndex_ZipPaths) {
    for (const char* ok : {"plugin.json", "themes/", "themes/a.json", "templates/game/Assets/Scenes/Main.rmscene", "lang/en.json", "a.b.c"}) {
        CHECK(ZipPathProblem(ok) == nullptr);
    }
    for (const char* bad : {"", "/", "/etc/x", "../x", "a/../../b", "a/./b", "C:/x", "a:b", "x//y", "con", "CON.txt", "a/nul.json", "lpt1",
                            "a/b.", "a/b ", "a\\b", "a*b", "a?b", "a<b", "a\x01" "b"}) {
        CHECK(ZipPathProblem(bad) != nullptr);
    }
    CHECK(ZipPathProblem(std::string(241, 'a')) != nullptr && ZipPathProblem(std::string(240, 'a')) == nullptr);
    CHECK(NormalizeZipPath("themes\\a.json") == "themes/a.json" && ZipPathProblem(NormalizeZipPath("..\\x")) != nullptr);
    CHECK(ZipPathProblem("console.json") == nullptr && ZipPathProblem("com10") == nullptr);  // (予約名に似ているだけ)
}

RM_TEST(PluginDownload_Extract) {
    const fs::path root = fs::temp_directory_path() / L"rm_download_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const PackageFetch ok = ExtractPackage(FromHex(kZipHex), root / L"a");
    CHECK(ok.fail == PackageFetch::Fail::None);
    CHECK(fs::is_regular_file(root / L"a" / L"plugin.json") && fs::is_regular_file(root / L"a" / L"themes" / L"a.json") &&
          fs::file_size(root / L"a" / L"lang" / L"en.json") == 4152);
    // ../ を含む zip: 何も書かない
    const PackageFetch evil = ExtractPackage(FromHex(kEvilHex), root / L"b");
    CHECK(evil.fail == PackageFetch::Fail::Path && evil.name == "../evil.txt" && evil.pathProblem != nullptr);
    CHECK(!fs::exists(root / L"b") && !fs::exists(root / L"evil.txt"));
    CHECK(ExtractPackage(FromHex(kNomanifestHex), root / L"c").fail == PackageFetch::Fail::NoManifest);
    CHECK(ExtractPackage("plain text", root / L"d").fail == PackageFetch::Fail::Zip);
    // 落とす (ローカルのファイル): 大きさと SHA-256 を確かめる
    const fs::path zipFile = root / L"pkg.zip";
    {
        const std::string bytes = FromHex(kZipHex);
        std::ofstream(zipFile, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const std::string bytes = FromHex(kZipHex);
    const std::string url = PathToUtf8(zipFile);
    CHECK(FetchPackage(url, bytes.size() + 1, std::string(64, '0'), root / L"e").fail == PackageFetch::Fail::Size);
    CHECK(FetchPackage(url, bytes.size(), std::string(64, '0'), root / L"e").fail == PackageFetch::Fail::Hash);
    fs::remove_all(root, ec);
}

RM_TEST(Http_LocalFiles) {
    CHECK(platform::LocalPathOf("file:///C:/site/index.json") == L"C:\\site\\index.json");
    CHECK(platform::LocalPathOf("file:///C:/my%20site/a.json") == L"C:\\my site\\a.json");
    CHECK(platform::LocalPathOf("file://server/share/a.json") == L"\\\\server\\share\\a.json");
    CHECK(platform::LocalPathOf("C:\\site\\index.json") == L"C:\\site\\index.json");
    CHECK(platform::LocalPathOf("https://ir-cola.github.io/rm_engine/index.json").empty());
    CHECK(platform::LocalPathOf("index.json").empty());
    const fs::path file = fs::temp_directory_path() / L"rm_http_test.json";
    std::ofstream(file, std::ios::binary) << "{\"a\":1}";
    const auto read = platform::Fetch(PathToUtf8(file), 100);
    CHECK(read.body && *read.body == "{\"a\":1}");
    CHECK(platform::Fetch(PathToUtf8(file), 3).fail == platform::FetchResult::Fail::TooBig);
    CHECK(platform::Fetch(PathToUtf8(file.parent_path() / L"rm_http_missing.json"), 100).fail == platform::FetchResult::Fail::File);
    CHECK(platform::Fetch("ftp://example.com/x", 100).fail == platform::FetchResult::Fail::BadUrl);
    std::error_code ec;
    fs::remove(file, ec);
}

RM_TEST(PluginManifest_VersionAndCode) {
    std::string err;
    auto m = ParsePluginManifest(Parse(R"({"author":"rm_engine","name":"theme_paper","version":"0.0.2","engine":{"min":"0.0.3"},"code":false})"), &err);
    CHECK(m && m->version == "0.0.2" && m->engineMin == "0.0.3" && !m->code);
    auto old = ParsePluginManifest(Parse(R"({"author":"rm_engine","name":"theme_paper"})"), &err);
    CHECK(old && old->version.empty() && old->code);  // (書いていなければコードがある)
    CHECK((!ParsePluginManifest(Parse(R"({"author":"rm_engine","name":"theme_paper","version":"1.0"})"), &err)));
    CHECK((!ParsePluginManifest(Parse(R"({"author":"rm_engine","name":"theme_paper","code":"no"})"), &err)));
    // 入っている写しがエンジンの写しより新しい: 準備できている (写し直さない)。古い・違う: もう一度写す
    const std::vector<PluginManifest> engine{Manifest("rm_engine", "theme_paper", "0.0.1")};
    CHECK((ReadyPlugins({Manifest("rm_engine", "theme_paper", "0.0.2")}, engine).size() == 1));
    CHECK((ReadyPlugins({Manifest("rm_engine", "theme_paper", "0.0.1")}, engine).size() == 1));
    CHECK((ReadyPlugins({Manifest("rm_engine", "theme_paper", "")}, engine).empty()));
    CHECK(NewerThanEngine(Manifest("rm_engine", "theme_paper", "0.0.2"), engine) && !NewerThanEngine(Manifest("rm_engine", "theme_paper", "0.0.1"), engine));
    CHECK(!NewerThanEngine(Manifest("someone", "theme_new", "9.0.0"), engine));  // (エンジンに無い物)
}
