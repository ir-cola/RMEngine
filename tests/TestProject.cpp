// プロジェクトの名前を変えたときにディスク上で変わること (editor/project/ProjectRename) のテスト。

#include "Test.h"

#include "editor/project/ProjectRename.h"

using namespace rm;
using namespace rm::editor;
namespace fs = std::filesystem;

RM_TEST(ProjectRename_OwnFolderFollows) {
    // このエンジンが作ったプロジェクト: <parent>/<name>/<name>.rmpf - すべてがついてくる。
    const auto plan = PlanProjectRename(L"D:\\Projects\\Game\\Game.rmpf", "Game", "Foo");
    CHECK(plan.renameFolder);
    CHECK(plan.folder == fs::path(L"D:\\Projects\\Foo"));
    CHECK(plan.file == fs::path(L"D:\\Projects\\Foo\\Foo.rmpf"));
    // フォルダーの大文字小文字は無視 (Windows)
    CHECK(PlanProjectRename(L"D:\\Projects\\game\\Game.rmpf", "Game", "Foo").renameFolder);
    // 日本語の名前
    const auto jp = PlanProjectRename(L"D:\\作品\\冒険\\冒険.rmpf", "冒険", "旅");
    CHECK(jp.renameFolder && jp.file == fs::path(L"D:\\作品\\旅\\旅.rmpf"));
}

RM_TEST(ProjectRename_UsersFolderStays) {
    // D:\Work\game.rmpf の "開く…" で名前が "Game": D:\Work はユーザーのもので、D:\Foo にしてはいけない。
    const auto plan = PlanProjectRename(L"D:\\Work\\game.rmpf", "Game", "Foo");
    CHECK(!plan.renameFolder);
    CHECK(plan.folder == fs::path(L"D:\\Work"));
    CHECK(plan.file == fs::path(L"D:\\Work\\Foo.rmpf"));
    // ドライブの直下の .rmpf
    const auto root = PlanProjectRename(L"D:\\Game.rmpf", "Game", "Foo");
    CHECK(!root.renameFolder && root.file == fs::path(L"D:\\Foo.rmpf"));
    // フォルダーは、名前に何かを足したものに見えるだけ
    CHECK(!PlanProjectRename(L"D:\\Game2\\Game.rmpf", "Game", "Foo").renameFolder);
}

RM_TEST(ProjectRename_OnlyTheCase) {
    // 同じ名前で大文字小文字だけ違う: フォルダーの名前は変わる (新しい大文字小文字を見せるため) が、同じフォルダー。
    const auto plan = PlanProjectRename(L"D:\\P\\game\\game.rmpf", "game", "Game");
    CHECK(plan.renameFolder && plan.folder == fs::path(L"D:\\P\\Game"));
    // もうその名前: フォルダーは何もしない。
    CHECK(!PlanProjectRename(L"D:\\P\\Game\\Game.rmpf", "Game", "Game").renameFolder);
    CHECK(SameFileName(L"Game", L"gAME") && !SameFileName(L"Game", L"Games"));
}
