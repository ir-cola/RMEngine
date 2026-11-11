// Tests for asset ids (core/asset): kinds, .meta text, Reconcile (which file has which id after files
// came, went, moved and were renamed), the order of the folder tree, and the files on a real folder
// (a temporary one: scan, write .meta, move a file without its .meta, read the ids back).

#include "Test.h"

#include "base/Utf.h"
#include "core/asset/AssetFiles.h"
#include "core/asset/AssetIndex.h"
#include "core/reflect/Reflect.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::asset;

namespace {

// Ids "00..01", "00..02" ... in order: what a new id is is known in advance.
struct Counter {
    uint64_t next = 1;
    std::string operator()() { return GuidFromNumbers(0, next++); }
};

const Entry* At(const Plan& p, std::string_view path) {
    for (const Entry& e : p.entries) {
        if (e.path == path) return &e;
    }
    return nullptr;
}

std::string G(uint64_t n) { return GuidFromNumbers(0xabc, n); }

}  // namespace

RM_TEST(Asset_Kinds) {
    CHECK(KindOf("Tree.FBX", false) == Kind::Model);
    CHECK(KindOf("a/b/sky.png", false) == Kind::Image);
    CHECK(KindOf("song.ogg", false) == Kind::Audio);
    CHECK(KindOf("notes", false) == Kind::Other);
    CHECK(KindOf("Models", true) == Kind::Folder);
    CHECK(KindKey(Kind::Model) == "model" && KindFromKey("image") == Kind::Image && !KindFromKey("mesh"));
    CHECK(KindMatches(Kind::Model, "model"));
    CHECK(KindMatches(Kind::Image, "model, image"));
    CHECK(!KindMatches(Kind::Audio, "model,image"));
    CHECK(KindMatches(Kind::Other, ""));    // empty = any file ...
    CHECK(!KindMatches(Kind::Folder, ""));  // ... but not a folder
    CHECK(ValidKindFilter("") && ValidKindFilter("image,model") && !ValidKindFilter("image,mesh"));
}

RM_TEST(Asset_Paths) {
    CHECK(ParentOf("a/b/c.png") == "a/b" && ParentOf("c.png").empty());
    CHECK(FileNameOf("a/b/c.png") == "c.png" && FileNameOf("c.png") == "c.png");
    CHECK(Extension("a/B.PNG") == ".png" && Extension("Makefile").empty() && Extension(".gitignore").empty());
    CHECK(Join("", "x") == "x" && Join("a/b", "x") == "a/b/x");
    CHECK(Ignored(".git") && Ignored("desktop.ini") && Ignored("Thumbs.db") && Ignored("a.png.meta") && !Ignored("a.png"));
    CHECK(IsGuid(GuidFromNumbers(1, 2)) && !IsGuid("ABCDEF0123456789abcdef0123456789") && !IsGuid("12"));
}

RM_TEST(Asset_MetaKeepsOtherKeys) {
    const std::string g = G(1);
    const std::string text = MetaText("", g);
    CHECK(GuidInMeta(text) == g);
    // A newer version (or a plugin) wrote more keys: they stay when the id is written again.
    const std::string withMore = R"({"guid": "0000000000000abc0000000000000001", "import": {"scale": 2}})";
    const std::string again = MetaText(withMore, G(2));
    CHECK(GuidInMeta(again) == G(2));
    CHECK(again.find("\"scale\"") != std::string::npos);
    CHECK(!GuidInMeta("not json") && !GuidInMeta(R"({"guid": "short"})") && !GuidInMeta("[]"));
}

RM_TEST(Asset_Reconcile_NewAndKept) {
    Counter ids;
    // First look: nothing has an id yet.
    std::vector<Found> found = {{"Models", true, ""}, {"Models/Tree.fbx", false, ""}, {"sky.png", false, ""}};
    Plan p = Reconcile({}, found, {}, ids);
    CHECK(p.entries.size() == 3 && p.writes.size() == 3 && p.moves.empty());
    CHECK(At(p, "Models/Tree.fbx")->guid == GuidFromNumbers(0, 2));
    // Next look: every one has its .meta - nothing to write.
    for (Found& f : found) f.metaGuid = At(p, f.path)->guid;
    Plan q = Reconcile(p.entries, found, {}, ids);
    CHECK(q.writes.empty() && q.moves.empty() && q.entries == p.entries);
}

RM_TEST(Asset_Reconcile_MovedWithoutMeta) {
    // Explorer moved Tree.fbx into Props/ but its .meta stayed behind (an orphan).
    Counter ids;
    const std::vector<Entry> before = {{"Models", G(1), true}, {"Models/Tree.fbx", G(2), false}, {"Props", G(3), true}};
    const std::vector<Found> found = {{"Models", true, G(1)}, {"Props", true, G(3)}, {"Props/Tree.fbx", false, ""}};
    const std::vector<Orphan> orphans = {{"Models/Tree.fbx", G(2)}};
    const Plan p = Reconcile(before, found, orphans, ids);
    CHECK(At(p, "Props/Tree.fbx")->guid == G(2));  // the same id: references still find it
    CHECK(p.moves.size() == 1 && p.moves[0].from == "Models/Tree.fbx" && p.moves[0].to == "Props/Tree.fbx");
    CHECK(p.writes.empty());
    CHECK(!p.notes.empty());
}

RM_TEST(Asset_Reconcile_RenamedWithoutMeta) {
    Counter ids;
    // Renamed in the same folder (rock.png -> stone.png): the only one gone and the only one new there.
    const std::vector<Found> found = {{"Tex", true, G(1)}, {"Tex/stone.png", false, ""}, {"Tex/grass.png", false, G(3)}};
    const std::vector<Orphan> orphans = {{"Tex/rock.png", G(2)}};
    const Plan p = Reconcile({}, found, orphans, ids);
    CHECK(At(p, "Tex/stone.png")->guid == G(2));
    CHECK(p.moves.size() == 1 && p.moves[0].to == "Tex/stone.png");
    // Two new ones with that extension: not clear which - both get new ids, the orphan stays.
    const std::vector<Found> two = {{"Tex", true, G(1)}, {"Tex/a.png", false, ""}, {"Tex/b.png", false, ""}};
    const Plan q = Reconcile({}, two, orphans, ids);
    CHECK(At(q, "Tex/a.png")->guid != G(2) && At(q, "Tex/b.png")->guid != G(2));
    CHECK(q.moves.empty() && q.writes.size() == 2);
    // Another extension is another thing.
    const std::vector<Found> other = {{"Tex", true, G(1)}, {"Tex/stone.jpg", false, ""}};
    CHECK(At(Reconcile({}, other, orphans, ids), "Tex/stone.jpg")->guid != G(2));
}

RM_TEST(Asset_Reconcile_FolderRenamed) {
    // A folder renamed in Explorer: what is in it moved with its .meta files; the folder's own .meta
    // ("Old.meta", next to it) stayed - the folder keeps its id by the rename rule.
    Counter ids;
    const std::vector<Found> found = {{"New", true, ""}, {"New/a.png", false, G(2)}};
    const std::vector<Orphan> orphans = {{"Old", G(1)}};
    const std::vector<Entry> before = {{"Old", G(1), true}, {"Old/a.png", G(2), false}};
    const Plan p = Reconcile(before, found, orphans, ids);
    CHECK(At(p, "New")->guid == G(1));
    CHECK(At(p, "New/a.png")->guid == G(2));
}

RM_TEST(Asset_Reconcile_MetaDeletedOrCopied) {
    Counter ids;
    // The .meta was deleted, the file is where it was: its id from the last look comes back.
    const std::vector<Entry> before = {{"a.png", G(1), false}};
    const Plan p = Reconcile(before, std::vector<Found>{{"a.png", false, ""}}, {}, ids);
    CHECK(At(p, "a.png")->guid == G(1) && p.writes.size() == 1 && p.writes[0].guid == G(1));
    // A copy made with its .meta: the one at the id's old path keeps it, the copy gets a new one.
    const std::vector<Found> copied = {{"a - copy.png", false, G(1)}, {"a.png", false, G(1)}};
    const Plan q = Reconcile(before, copied, {}, ids);
    CHECK(At(q, "a.png")->guid == G(1));
    CHECK(At(q, "a - copy.png")->guid != G(1) && IsGuid(At(q, "a - copy.png")->guid));
    CHECK(q.writes.size() == 1 && q.writes[0].path == "a - copy.png");
    // An orphan whose id a file still has (a stray copy of a .meta) is not used again.
    const Plan r = Reconcile({}, std::vector<Found>{{"a.png", false, G(1)}, {"b.png", false, ""}},
                             std::vector<Orphan>{{"b2.png", G(1)}}, ids);
    CHECK(At(r, "b.png")->guid != G(1));
}

RM_TEST(Asset_DisplayOrder) {
    const std::vector<Entry> e = {{"z.txt", G(1), false}, {"B", G(2), true}, {"a", G(3), true}, {"a/x.png", G(4), false},
                                  {"B/c", G(5), true},   {"Y.png", G(6), false}};
    const std::vector<size_t> order = DisplayOrder(e);
    std::vector<std::string> paths;
    for (const size_t i : order) paths.push_back(e[i].path);
    // folders first (names ignoring case), each followed by what is in it; then the files
    const std::vector<std::string> want = {"a", "a/x.png", "B", "B/c", "Y.png", "z.txt"};
    CHECK(paths == want);
    CHECK(NumberOf(G(1)) == NumberOf(G(1)) && NumberOf(G(1)) != NumberOf(G(2)) && NumberOf("") != 0);
}

RM_TEST(Asset_Index) {
    Index idx;
    idx.Set({{"a.png", G(1), false}, {"M", G(2), true}});
    CHECK(idx.ByGuid(G(2)) && idx.ByGuid(G(2))->path == "M");
    CHECK(idx.ByPath("a.png") && idx.ByPath("a.png")->guid == G(1));
    CHECK(!idx.ByGuid(G(3)) && !idx.ByPath("b.png"));
}

RM_TEST(Asset_FilesOnDisk) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / L"rm_asset_test" / L"Assets";
    fs::remove_all(root.parent_path(), ec);
    fs::create_directories(root / L"モデル", ec);
    auto write = [](const fs::path& p, const char* text) { std::ofstream(p, std::ios::binary) << text; };
    write(root / L"モデル" / L"木.fbx", "x");
    write(root / L"sky.png", "y");
    write(root / L"desktop.ini", "ignored");

    std::vector<std::string> notes;
    const std::vector<Entry> first = Update(root, {}, notes);
    CHECK(first.size() == 3);  // the folder, the model, the picture (not desktop.ini)
    CHECK(fs::exists(root / L"モデル.meta") && fs::exists(root / L"sky.png.meta"));
    std::string treeGuid;
    for (const Entry& e : first) {
        if (e.path == "モデル/木.fbx") treeGuid = e.guid;
    }
    CHECK(IsGuid(treeGuid));
    // What the game reads (nothing written): the same ids.
    const std::vector<Entry> read = ReadIndex(root);
    CHECK(read.size() == 3);

    // Moved outside the editor without its .meta: the id goes along, and so does the .meta.
    fs::rename(root / L"モデル" / L"木.fbx", root / L"木.fbx", ec);
    const std::vector<Entry> second = Update(root, first, notes);
    std::string movedGuid;
    for (const Entry& e : second) {
        if (e.path == "木.fbx") movedGuid = e.guid;
    }
    CHECK(movedGuid == treeGuid);
    CHECK(fs::exists(root / L"木.fbx.meta") && !fs::exists(root / L"モデル" / L"木.fbx.meta"));
    // Looking again changes nothing.
    CHECK(Update(root, second, notes) == second);
    fs::remove_all(root.parent_path(), ec);
}

RM_TEST(Asset_KindsOfReflectedFields) {
    // Every AssetKind="..." of every reflected type names kinds that exist (rmgen checks only the form).
    for (const reflect::TypeDesc* t : reflect::AllTypes()) {
        for (const reflect::FieldDesc& f : t->fields) {
            if (f.assetKinds && !ValidKindFilter(f.assetKinds)) CHECK(!"unknown AssetKind");
        }
    }
}
