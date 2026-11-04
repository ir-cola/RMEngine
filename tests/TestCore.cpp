// Tests for the pure parts of core and ui: undo history and text editing.

#include "Test.h"

#include "core/UndoHistory.h"
#include "ui/TextEdit.h"

#include <memory>
#include <string>

using namespace rm;

namespace {

struct SetInt final : ICommand {
    int& target;
    int before, after;
    SetInt(int& t, int b, int a) : target(t), before(b), after(a) {}
    void Do() override { target = after; }
    void Undo() override { target = before; }
};

// A command that (wrongly) pushes another command while it runs.
struct Reentrant final : ICommand {
    UndoHistory& h;
    int& value;
    bool pushedOk = true;
    Reentrant(UndoHistory& hist, int& v) : h(hist), value(v) {}
    void Do() override { pushedOk = h.Push(std::make_unique<SetInt>(value, 0, 99)); }
    void Undo() override { pushedOk = h.Push(std::make_unique<SetInt>(value, 0, 99)); }
};

}  // namespace

RM_TEST(Undo_PushMarksModified) {
    UndoHistory h;
    int modified = 0;
    h.SetOnModified([&] { ++modified; });
    int v = 0;
    v = 5;
    CHECK(h.Push(std::make_unique<SetInt>(v, 0, 5)));
    CHECK(modified == 1);
    CHECK(h.CanUndo() && !h.CanRedo());
    CHECK(h.Undo() && v == 0);
    CHECK(modified == 2);
    CHECK(h.Redo() && v == 5);
    CHECK(modified == 3);
}

RM_TEST(Undo_PushClearsRedo) {
    UndoHistory h;
    int v = 0;
    h.Push(std::make_unique<SetInt>(v, 0, 1), false);
    CHECK(v == 1);
    h.Push(std::make_unique<SetInt>(v, 1, 2), false);
    h.Undo();
    CHECK(v == 1 && h.CanRedo());
    h.Push(std::make_unique<SetInt>(v, 1, 3), false);
    CHECK(!h.CanRedo());
    CHECK(h.UndoCount() == 2);
}

RM_TEST(Undo_ReentrantPushIsRejected) {
    UndoHistory h;
    int v = 0;
    auto cmd = std::make_unique<Reentrant>(h, v);
    Reentrant* raw = cmd.get();
    CHECK(h.Push(std::move(cmd), false));
    CHECK(!raw->pushedOk);  // nested Push during Do was refused
    CHECK(h.UndoCount() == 1);
    CHECK(h.Undo());
    CHECK(!raw->pushedOk);  // ... and during Undo
    CHECK(h.UndoCount() == 0 && h.RedoCount() == 1);
    CHECK(v == 0);
}

RM_TEST(Undo_MaxDepth) {
    UndoHistory h;
    h.SetMaxDepth(3);
    int v = 0;
    for (int i = 0; i < 5; ++i) h.Push(std::make_unique<SetInt>(v, i, i + 1), false);
    CHECK(h.UndoCount() == 3);
}

RM_TEST(TextEdit_InsertAndMove) {
    ui::TextEdit e;
    e.SetText(L"abc");
    CHECK(e.Caret() == 3);
    e.MoveLeft(false, false);
    e.Insert(L"X");
    CHECK(e.Text() == L"abXc");
    e.Home(false);
    e.Insert(L">");
    CHECK(e.Text() == L">abXc");
    e.End(false);
    CHECK(e.Backspace(false));
    CHECK(e.Text() == L">abX");
    CHECK(!e.Delete(false));
}

RM_TEST(TextEdit_Selection) {
    ui::TextEdit e;
    e.SetText(L"hello world");
    e.SelectAll();
    CHECK(e.SelectedText() == L"hello world");
    e.Insert(L"hi");
    CHECK(e.Text() == L"hi" && !e.HasSelection());
    e.SetText(L"abcdef");
    e.SetCaret(1, false);
    e.SetCaret(4, true);
    CHECK(e.SelectedText() == L"bcd");
    e.MoveLeft(false, false);  // collapses to the start
    CHECK(e.Caret() == 1 && !e.HasSelection());
    e.SetCaret(4, true);
    CHECK(e.Backspace(false));
    CHECK(e.Text() == L"aef");
}

RM_TEST(TextEdit_SurrogatePairs) {
    ui::TextEdit e;
    e.SetText(L"a\xD83D\xDE00z");  // a 😀 z
    e.MoveLeft(false, false);       // before z
    e.MoveLeft(false, false);       // before 😀 (skips both halves)
    CHECK(e.Caret() == 1);
    e.SetCaret(2, false);  // inside the pair -> snapped
    CHECK(e.Caret() == 1);
    e.SetCaret(3, false);
    CHECK(e.Backspace(false));
    CHECK(e.Text() == L"az");
}

RM_TEST(TextEdit_WordsAndFiltering) {
    ui::TextEdit e;
    e.SetText(L"one two  three");
    e.MoveLeft(false, true);
    CHECK(e.Caret() == 9);
    e.Backspace(true);
    CHECK(e.Text() == L"one three");
    e.SetText(L"");
    e.Insert(L"a\r\nb\tc\x01");
    CHECK(e.Text() == L"a b c");
}

RM_TEST(TextEdit_MaxLength) {
    ui::TextEdit e;
    e.SetMaxLength(4);
    e.SetText(L"");
    e.Insert(L"abcdef");
    CHECK(e.Text() == L"abcd");
    e.SetText(L"abc");
    e.Insert(L"\xD83D\xDE00");  // would split the pair -> dropped
    CHECK(e.Text() == L"abc");
}
