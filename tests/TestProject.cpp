// Tests for what renaming a project changes on disk (editor/project/ProjectRename).

#include "Test.h"

#include "editor/project/ProjectRename.h"

using namespace rm;
using namespace rm::editor;
namespace fs = std::filesystem;

RM_TEST(ProjectRename_OwnFolderFollows) {
    // A project this engine made: <parent>/<name>/<name>.rmpf - everything follows.
    const auto plan = PlanProjectRename(L"D:\\Projects\\Game\\Game.rmpf", "Game", "Foo");
    CHECK(plan.renameFolder);
    CHECK(plan.folder == fs::path(L"D:\\Projects\\Foo"));
    CHECK(plan.file == fs::path(L"D:\\Projects\\Foo\\Foo.rmpf"));
    // letter case of the folder ignored (Windows)
    CHECK(PlanProjectRename(L"D:\\Projects\\game\\Game.rmpf", "Game", "Foo").renameFolder);
    // Japanese names
    const auto jp = PlanProjectRename(L"D:\\作品\\冒険\\冒険.rmpf", "冒険", "旅");
    CHECK(jp.renameFolder && jp.file == fs::path(L"D:\\作品\\旅\\旅.rmpf"));
}

RM_TEST(ProjectRename_UsersFolderStays) {
    // "開く…" of D:\Work\game.rmpf named "Game": D:\Work is the user's, it must never become D:\Foo.
    const auto plan = PlanProjectRename(L"D:\\Work\\game.rmpf", "Game", "Foo");
    CHECK(!plan.renameFolder);
    CHECK(plan.folder == fs::path(L"D:\\Work"));
    CHECK(plan.file == fs::path(L"D:\\Work\\Foo.rmpf"));
    // a .rmpf at the root of a drive
    const auto root = PlanProjectRename(L"D:\\Game.rmpf", "Game", "Foo");
    CHECK(!root.renameFolder && root.file == fs::path(L"D:\\Foo.rmpf"));
    // the folder only looks like the name with something added
    CHECK(!PlanProjectRename(L"D:\\Game2\\Game.rmpf", "Game", "Foo").renameFolder);
}

RM_TEST(ProjectRename_OnlyTheCase) {
    // The same name in another letter case: the folder is renamed (to show the new case), it is the same one.
    const auto plan = PlanProjectRename(L"D:\\P\\game\\game.rmpf", "game", "Game");
    CHECK(plan.renameFolder && plan.folder == fs::path(L"D:\\P\\Game"));
    // Already that name: nothing for the folder.
    CHECK(!PlanProjectRename(L"D:\\P\\Game\\Game.rmpf", "Game", "Game").renameFolder);
    CHECK(SameFileName(L"Game", L"gAME") && !SameFileName(L"Game", L"Games"));
}
