// Tests for asset ids (core/asset): kinds, .meta text, Reconcile (which file has which id after files
// came, went, moved and were renamed), the order of the folder tree, and the files on a real folder
// (a temporary one: scan, write .meta, move a file without its .meta, read the ids back).

#include "Test.h"

#include "base/Utf.h"
#include "core/asset/AssetFiles.h"
#include "core/asset/AssetIndex.h"
#include "core/asset/ImportSettings.h"
#include "editor/app/Worker.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "core/reflect/Reflect.h"

#include <windows.h>

#include <chrono>
#include <format>
#include <filesystem>
#include <stop_token>
#include <thread>
#include <fstream>
#include <iterator>
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
    CHECK(p.writes.size() == 1 && p.writes[0].guid == G(2) && p.writes[0].name == "Tree.fbx");  // the moved .meta says its name
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

RM_TEST(Asset_ImportSettingsInMeta) {
    // The "import" object of a .meta, and a new text with another one: the id and other keys stay.
    const std::string meta = R"({"guid": "0000000000000abc0000000000000001", "memo": 1, "import": {"scale": 0.5, "future": true}})";
    const json::Value imp = ImportInMeta(meta);
    CHECK(imp.Get("scale").AsDouble() == 0.5);
    CHECK(ImportInMeta("{}").IsObject() && ImportInMeta("broken").IsObject());
    json::Value changed = imp;
    changed.Set("scale", 2.0);
    const std::string text = MetaWithImport(meta, changed);
    CHECK(GuidInMeta(text) == G(1));
    CHECK(text.find("\"memo\"") != std::string::npos && text.find("\"future\"") != std::string::npos);
    CHECK(ImportInMeta(text).Get("scale").AsDouble() == 2.0);
    // Writing the id again keeps the settings (MetaText keeps every other key).
    CHECK(ImportInMeta(MetaText(text, G(2))).Get("scale").AsDouble() == 2.0);
    // Typed: what a loader reads (defaults for what is missing, the range kept).
    ModelImport m;
    reflect::Load(m, *json::Parse(R"({"scale": 0, "up": "Z"})"));
    CHECK(m.scale == 0.0001f && m.up == UpAxis::Z);
    CHECK(ImportTypeOf(Kind::Model) == &reflect::TypeOf<ModelImport>() && ImportTypeOf(Kind::Image) == &reflect::TypeOf<ImageImport>());
    CHECK(ImportTypeOf(Kind::Audio) == nullptr && ImportTypeOf(Kind::Folder) == nullptr);
}

RM_TEST(Asset_ImportSettingsOnDisk) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / L"rm_asset_import_test";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const fs::path file = root / L"木.obj";
    std::ofstream(file, std::ios::binary) << "v 0 0 0\n";
    std::vector<std::string> notes;
    Update(root, {}, notes);  // the id
    const auto before = ChangedAt(file, ec);
    CHECK(!ec);
    CHECK(ReadImport<ModelImport>(file).scale == 1.f);  // none yet: the defaults
    json::Value imp = json::Value::MakeObject();
    imp.Set("scale", 0.01);
    imp.Set("up", "Z");
    CHECK(WriteImportJson(file, imp));
    const ModelImport m = ReadImport<ModelImport>(file);
    CHECK(m.scale == 0.01f && m.up == UpAxis::Z);
    CHECK(ChangedAt(file, ec) >= before);
    // The id is still there, and looking at the folder again keeps the settings.
    const std::vector<Entry> again = Update(root, {}, notes);
    CHECK(again.size() == 1 && IsGuid(again[0].guid));
    CHECK(ReadImport<ModelImport>(file).up == UpAxis::Z);
    ChangedAt(root / L"無い.obj", ec);
    CHECK(static_cast<bool>(ec));
    fs::remove_all(root, ec);
}

RM_TEST(Asset_ModelImportApplied) {
    space3d::MeshData mesh;
    mesh.vertices.push_back({{1, 2, 3}, {0, 0, 1}, {0, 0}});
    mesh.vertices.push_back({{-1, 0, 0}, {0, 1, 0}, {0, 0}});
    space3d::ComputeBounds(mesh);
    space3d::ApplyModelImport(mesh, 2.f, true);  // Z up -> Y up, twice as big
    const space3d::MeshVertex& v = mesh.vertices[0];
    CHECK(v.position.x == 2 && v.position.y == 6 && v.position.z == -4);
    CHECK(v.normal.x == 0 && v.normal.y == 1 && v.normal.z == 0);  // what faced up (Z) faces up (Y)
    CHECK(mesh.vertices[1].normal.z == -1);
    CHECK(mesh.max.y == 6 && mesh.min.x == -2 && mesh.min.z == -4);
    space3d::MeshData same = mesh;
    space3d::ApplyModelImport(same, 1.f, false);  // the defaults change nothing
    CHECK(same.vertices[0].position.y == 6);
}

// --- A .meta that cannot be read, and copies made outside the editor ------------------------------

namespace {
// git's conflict marks around two ids (written out here so the marks are not at a line start of this file).
std::string ConflictText() {
    const std::string lt(7, '<'), eq(7, '='), gt(7, '>');
    return lt + " HEAD\n{\"guid\": \"0000000000000abc0000000000000001\"}\n" + eq + "\n{\"guid\": \"0000000000000abc0000000000000002\"}\n" + gt +
           " other\n";
}
}  // namespace

RM_TEST(Asset_ReadMeta_OkDamagedUnreadable) {
    const MetaInfo ok = ReadMeta(R"({"guid": "0000000000000abc0000000000000001", "name": "a.png"})");
    CHECK(ok.state == MetaState::Ok && ok.guid == G(1) && ok.name == "a.png");
    // A comma too many: not JSON, but the one id in it is picked out (and the file is never rewritten).
    const MetaInfo comma = ReadMeta("{\"guid\": \"0000000000000abc0000000000000002\",}");
    CHECK(comma.state == MetaState::Damaged && comma.guid == G(2));
    CHECK(!GuidInMeta("{\"guid\": \"0000000000000abc0000000000000002\",}"));  // (only an Ok one counts there)
    // git's conflict marks with two ids: which one is not clear.
    CHECK(ReadMeta(ConflictText()).state == MetaState::Unreadable);
    CHECK(ReadMeta("garbage").state == MetaState::Unreadable);
    CHECK(ReadMeta("{\"memo\": 1}").state == MetaState::Unreadable);  // JSON, but no id
    // The name is written with the id; other keys stay.
    const std::string t = MetaText(R"({"guid": "0000000000000abc0000000000000001", "memo": 1})", G(1), "b.png");
    CHECK(ReadMeta(t).name == "b.png" && t.find("\"memo\"") != std::string::npos);
    CHECK(Ignored("a.png.meta.tmp") && Ignored("A.PNG.META.TMP") && !Ignored("a.tmp"));
}

RM_TEST(Asset_Reconcile_UnreadableMetaIsNeverReplaced) {
    Counter ids;
    Found locked{"a.png", false, ""};
    locked.meta = MetaState::Unreadable;
    locked.metaProblem = "開けません";
    // Opened at start (no last look): left out until it can be read - no new id, nothing written.
    const Plan p = Reconcile({}, std::vector<Found>{locked}, {}, ids);
    CHECK(p.entries.empty() && p.writes.empty() && p.moves.empty());
    CHECK(!p.notes.empty());
    // During the session: it keeps the id of the last look - still nothing written.
    const std::vector<Entry> before = {{"a.png", G(1), false}};
    const Plan q = Reconcile(before, std::vector<Found>{locked}, {}, ids);
    CHECK(q.entries.size() == 1 && q.entries[0].guid == G(1) && q.writes.empty());
    // A broken .meta whose id could be picked out: used, never written.
    Found broken{"b.png", false, G(2)};
    broken.meta = MetaState::Damaged;
    const Plan r = Reconcile({}, std::vector<Found>{broken}, {}, ids);
    CHECK(r.entries.size() == 1 && r.entries[0].guid == G(2) && r.writes.empty());
}

RM_TEST(Asset_Reconcile_CopyDoesNotTakeTheId) {
    Counter ids;
    // Copied in Explorer while the editor was closed: "X - コピー.png" + a copy of the .meta, listed
    // first (' ' comes before '.'). The .meta says the name it was written for: the original keeps it.
    Found copy{"X - コピー.png", false, G(1)};
    copy.meta = MetaState::Ok;
    copy.metaName = "X.png";
    Found original{"X.png", false, G(1)};
    original.meta = MetaState::Ok;
    original.metaName = "X.png";
    const Plan p = Reconcile({}, std::vector<Found>{copy, original}, {}, ids);
    CHECK(At(p, "X.png")->guid == G(1));
    CHECK(At(p, "X - コピー.png")->guid != G(1));
    CHECK(p.writes.size() == 1 && p.writes[0].path == "X - コピー.png" && p.writes[0].name == "X - コピー.png");
    // A .meta from before names were written: the one made first is the original (a copy is made new).
    copy.metaName = original.metaName = "";
    copy.metaCreated = 2000;
    original.metaCreated = 1000;
    const Plan q = Reconcile({}, std::vector<Found>{copy, original}, {}, ids);
    CHECK(At(q, "X.png")->guid == G(1));
    // Nothing tells them apart: the first, with a note that it is a guess.
    copy.metaCreated = original.metaCreated = 0;
    const Plan r = Reconcile({}, std::vector<Found>{copy, original}, {}, ids);
    CHECK(At(r, "X - コピー.png")->guid == G(1) && !r.notes.empty());
    // A copy whose .meta is broken cannot be given its own id: left out, its .meta untouched.
    copy.meta = MetaState::Damaged;
    copy.metaName = "";
    original.metaName = "X.png";
    const Plan s = Reconcile({}, std::vector<Found>{copy, original}, {}, ids);
    CHECK(At(s, "X.png")->guid == G(1) && !At(s, "X - コピー.png") && s.writes.empty());
}

RM_TEST(Asset_FilesOnDisk_BrokenAndLockedMetaStay) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / L"rm_asset_safety" / L"Assets";
    fs::remove_all(root.parent_path(), ec);
    fs::create_directories(root, ec);
    auto write = [](const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; };
    auto read = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    // A .meta with a comma too many, and one with git's conflict marks.
    const std::string commaText = "{\"guid\": \"0000000000000abc0000000000000007\", \"memo\": 1,}";
    const std::string conflictText = ConflictText();
    write(root / L"a.png", "a");
    write(root / L"a.png.meta", commaText);
    write(root / L"b.png", "b");
    write(root / L"b.png.meta", conflictText);
    write(root / L"c.png.meta.tmp", "left by a failed write");
    std::vector<std::string> notes;
    const std::vector<Entry> first = Update(root, {}, notes);
    CHECK(first.size() == 1 && first[0].path == "a.png" && first[0].guid == G(7));  // b waits; the .tmp is not an asset
    CHECK(read(root / L"a.png.meta") == commaText);  // not written over
    CHECK(read(root / L"b.png.meta") == conflictText);
    CHECK(!notes.empty());

    // Fixed by hand: b gets the id written in it again.
    write(root / L"b.png.meta", "{\"guid\": \"0000000000000abc0000000000000002\"}");
    const std::vector<Entry> second = Update(root, first, notes);
    CHECK(second.size() == 2);

    // A .meta another program holds open (no sharing): the id of the last look stays, and the
    // .meta is not written over.
    const fs::path bMeta = root / L"b.png.meta";
    HANDLE h = CreateFileW(bMeta.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    const std::vector<Entry> third = Update(root, second, notes);
    bool kept = false;
    for (const Entry& e : third) kept |= e.path == "b.png" && e.guid == G(2);
    CHECK(kept);
    // Opened at start while it is held: left out (not given a new id).
    const std::vector<Entry> fresh = Update(root, {}, notes);
    CHECK(fresh.size() == 1);
    CloseHandle(h);
    CHECK(read(bMeta) == "{\"guid\": \"0000000000000abc0000000000000002\"}");
    // Import settings are not written into a broken .meta either.
    CHECK(!WriteImportJson(root / L"a.png", json::Value::MakeObject()));
    CHECK(read(root / L"a.png.meta") == commaText);
    fs::remove_all(root.parent_path(), ec);
}

RM_TEST(Asset_FilesOnDisk_CopiedWhileClosed) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / L"rm_asset_copy" / L"Assets";
    fs::remove_all(root.parent_path(), ec);
    fs::create_directories(root, ec);
    std::ofstream(root / L"X.png", std::ios::binary) << "x";
    std::vector<std::string> notes;
    const std::vector<Entry> first = Update(root, {}, notes);
    CHECK(first.size() == 1);
    const std::string id = first[0].guid;
    // The editor is closed; Explorer copies the file and its .meta.
    fs::copy_file(root / L"X.png", root / L"X - コピー.png", ec);
    fs::copy_file(root / L"X.png.meta", root / L"X - コピー.png.meta", ec);
    const std::vector<Entry> second = Update(root, {}, notes);  // opened again: no last look
    std::string original, copy;
    for (const Entry& e : second) (e.path == "X.png" ? original : copy) = e.guid;
    CHECK(original == id);
    CHECK(IsGuid(copy) && copy != id);
    // Renamed in the editor: the .meta says the new name.
    fs::rename(root / L"X.png", root / L"Y.png", ec);
    fs::rename(root / L"X.png.meta", root / L"Y.png.meta", ec);
    CHECK(RecordName(root / L"Y.png"));
    std::string text;
    {
        std::ifstream m(root / L"Y.png.meta", std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(m), std::istreambuf_iterator<char>());
    }
    CHECK(ReadMeta(text).name == "Y.png" && ReadMeta(text).guid == id);
    fs::remove_all(root.parent_path(), ec);
}

// --- Big folders --------------------------------------------------------------------------------

RM_TEST(Asset_Reconcile_BigFolderIsFast) {
    // 20,000 files in 100 folders: 18,000 with their .meta, 1,000 moved without it (orphans left
    // behind), 1,000 new; and the last look's ids. The time grows with the count, not its square.
    Counter ids;
    std::vector<Found> found;
    std::vector<Orphan> orphans;
    std::vector<Entry> before;
    for (int i = 0; i < 20000; ++i) {
        const std::string folder = std::format("F{}", i % 100);
        Found f{std::format("{}/file{}.png", folder, i), false, ""};
        if (i < 18000) {
            f.metaGuid = G(static_cast<uint64_t>(i + 1));
            f.meta = MetaState::Ok;
            before.push_back({f.path, f.metaGuid, false});
        } else if (i < 19000) {
            // moved here from another folder, its .meta left behind
            const std::string old = std::format("Old{}/file{}.png", i % 100, i);
            orphans.push_back({old, G(static_cast<uint64_t>(i + 1))});
            before.push_back({old, G(static_cast<uint64_t>(i + 1)), false});
        }
        found.push_back(std::move(f));
    }
    const auto start = std::chrono::steady_clock::now();
    const Plan p = Reconcile(before, found, orphans, ids);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 5.0);
    CHECK(p.entries.size() == 20000);
    CHECK(p.moves.size() == 1000);  // every moved one kept its id
    CHECK(At(p, "F50/file18050.png")->guid == G(18051));
    // DisplayOrder of the same: folders first, then by name.
    const auto t2 = std::chrono::steady_clock::now();
    const std::vector<size_t> order = DisplayOrder(p.entries);
    CHECK(order.size() == 20000);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t2).count() < 5.0);
}

RM_TEST(Asset_ScanReadsOnlyChangedMeta) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / L"rm_asset_cache" / L"Assets";
    fs::remove_all(root.parent_path(), ec);
    fs::create_directories(root, ec);
    for (int i = 0; i < 50; ++i) std::ofstream(root / std::format(L"f{}.png", i), std::ios::binary) << "x";
    MetaCache cache;
    std::vector<std::string> notes;
    bool wrote = false;
    const std::vector<Entry> first = Update(root, {}, notes, &cache, &wrote);
    CHECK(first.size() == 50 && wrote);  // the ids were written
    // Looked at again: the .meta files it wrote are read once (they are new to the cache) ...
    const std::vector<Entry> second = Update(root, first, notes, &cache, &wrote);
    CHECK(second == first && !wrote);
    // ... and after that, none - nothing changed.
    const std::vector<Entry> third = Update(root, second, notes, &cache, &wrote);
    CHECK(third == first && cache.reads == 0);
    // One .meta changed (its import settings): only that one is read.
    json::Value imp = json::Value::MakeObject();
    imp.Set("sharp", true);
    CHECK(WriteImportJson(root / L"f7.png", imp));
    Update(root, third, notes, &cache, &wrote);
    CHECK(cache.reads == 1);
    // A file gone: its .meta is no longer kept.
    fs::remove(root / L"f3.png", ec);
    fs::remove(root / L"f3.png.meta", ec);
    const std::vector<Entry> fourth = Update(root, third, notes, &cache, &wrote);
    CHECK(fourth.size() == 49 && cache.byPath.size() == 49);
    fs::remove_all(root.parent_path(), ec);
}

// --- Looking at the folder on a Worker --------------------------------------------------------------

RM_TEST(Asset_LookNumber_OnlyTheNewestCounts) {
    LookNumber looks;
    const uint64_t a = looks.Next();
    CHECK(looks.Current(a));
    const uint64_t b = looks.Next();  // asked again (or the editor renamed something)
    CHECK(!looks.Current(a) && looks.Current(b) && looks.Now() == b);
}

RM_TEST(Asset_Look_WritesNothingAndStops) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / L"rm_asset_look" / L"Assets";
    fs::remove_all(root.parent_path(), ec);
    fs::create_directories(root, ec);
    for (int i = 0; i < 600; ++i) std::ofstream(root / std::format(L"f{}.png", i), std::ios::binary) << "x";
    // A look only works the plan out: the new ids are in it, no .meta is written yet.
    MetaCache cache;
    bool stopped = true;
    const Plan plan = Look(root, {}, &cache, {}, &stopped);
    CHECK(!stopped && plan.entries.size() == 600 && plan.writes.size() == 600);
    CHECK(!fs::exists(root / L"f0.png.meta"));
    // The thread that owns the ids writes them.
    std::vector<std::string> errors;
    ApplyPlan(root, plan, errors);
    CHECK(errors.empty() && fs::exists(root / L"f0.png.meta"));
    // Asked to stop: it ends part way, with nothing to use.
    std::stop_source source;
    source.request_stop();
    const Plan none = Look(root, plan.entries, &cache, source.get_token(), &stopped);
    CHECK(stopped && none.entries.empty() && none.writes.empty());
    CHECK(ScanFolder(root, nullptr, source.get_token()).stopped);
    fs::remove_all(root.parent_path(), ec);
}

RM_TEST(Asset_LookOnWorker_OvertakenResultIsDropped) {
    // As AssetLibrary does: the look runs on a Worker, its plan comes back to the UI thread and is used
    // only while its number is the newest.
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / L"rm_asset_look_worker" / L"Assets";
    fs::remove_all(root.parent_path(), ec);
    fs::create_directories(root, ec);
    std::ofstream(root / L"a.png", std::ios::binary) << "a";
    rm::editor::SetUiThread();
    rm::editor::Worker worker;
    LookNumber looks;
    std::vector<Entry> applied;
    int arrived = 0;
    auto ask = [&] {
        const uint64_t number = looks.Next();
        worker.Run([&, number, root](std::stop_token stop, const rm::editor::Worker::Post& post) {
            bool stopped = false;
            Plan plan = Look(root, {}, nullptr, stop, &stopped);
            if (stopped) return;
            post([&, number, plan = std::move(plan)] {
                ++arrived;
                if (looks.Current(number)) applied = plan.entries;  // (else: overtaken - thrown away)
            });
        });
    };
    auto pump = [&](auto done) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < end && !done()) {
            rm::editor::RunPostedToUi();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return done();
    };
    // The editor changes the folder while a look runs (a new number, no new look yet): the look's
    // result arrives, and is not used.
    ask();
    looks.Next();
    CHECK(pump([&] { return arrived == 1; }));
    CHECK(applied.empty());
    // A look asked after the change is used - and sees the folder as it is now.
    std::ofstream(root / L"b.png", std::ios::binary) << "b";
    ask();
    CHECK(pump([&] { return !applied.empty(); }));
    CHECK(applied.size() == 2);
    // Two asked one after the other: only the newest is used (the Worker drops the first, too).
    applied.clear();
    std::ofstream(root / L"c.png", std::ios::binary) << "c";
    ask();
    ask();
    CHECK(pump([&] { return !applied.empty() && !worker.Busy(); }));
    CHECK(applied.size() == 3);
    worker.Stop();
    fs::remove_all(root.parent_path(), ec);
}
