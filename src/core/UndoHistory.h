#pragma once
// Undo / Redo history (pure).
//
// Two rules carried over from the previous engine (handoff memo 2-1 / 4-6):
//  * "Pushed to the history" == "unsaved". The owner wires SetOnModified to its dirty flag,
//    so an edit that reaches the history can never be lost silently on close.
//  * Take the command out of the stack first, then run it. A command that tries to Push
//    while it is being undone / redone is rejected, so the stacks never get out of order
//    (bug #15 of the previous engine).

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rm {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Do() = 0;
    virtual void Undo() = 0;
    virtual std::string Label() const { return {}; }
    // Typing into a field applies every key at once, but is one entry: the history asks the newest
    // entry to take in the next command (true = taken: it now ends where `next` ends).
    virtual bool MergeWith(const ICommand& next) {
        (void)next;
        return false;
    }
    // After merging, an entry that ends where it began (typed, then Esc) is dropped.
    virtual bool IsNoOp() const { return false; }
};

// Several edits that are one action for the user (a gizmo drag of 3 selected objects): one entry,
// undone in the reverse order. Empty groups should not be pushed.
class CommandGroup final : public ICommand {
public:
    explicit CommandGroup(std::string label) : label_(std::move(label)) {}
    void Add(std::unique_ptr<ICommand> cmd) {
        if (cmd) cmds_.push_back(std::move(cmd));
    }
    bool Empty() const { return cmds_.empty(); }
    void Do() override {
        for (auto& c : cmds_) c->Do();
    }
    void Undo() override {
        for (auto it = cmds_.rbegin(); it != cmds_.rend(); ++it) (*it)->Undo();
    }
    std::string Label() const override { return label_; }

private:
    std::vector<std::unique_ptr<ICommand>> cmds_;
    std::string label_;
};

class UndoHistory {
public:
    void SetOnModified(std::function<void()> fn) { onModified_ = std::move(fn); }
    void SetMaxDepth(size_t n) { maxDepth_ = n; }

    // alreadyDone = true: the edit has been applied by the caller (typical for UI drags).
    // Returns false when called from inside Undo()/Redo() (the command is dropped).
    // When nothing was undone since the newest entry and it takes `cmd` in (ICommand::MergeWith), no
    // new entry is made; if that leaves it changing nothing, it is removed.
    bool Push(std::unique_ptr<ICommand> cmd, bool alreadyDone = true);

    bool CanUndo() const { return !executing_ && !undo_.empty(); }
    bool CanRedo() const { return !executing_ && !redo_.empty(); }
    bool Undo();
    bool Redo();
    void Clear();

    size_t UndoCount() const { return undo_.size(); }
    size_t RedoCount() const { return redo_.size(); }
    bool IsExecuting() const { return executing_; }

private:
    void Modified() {
        if (onModified_) onModified_();
    }

    std::vector<std::unique_ptr<ICommand>> undo_;
    std::vector<std::unique_ptr<ICommand>> redo_;
    std::function<void()> onModified_;
    size_t maxDepth_ = 1000;
    bool executing_ = false;
};

}  // namespace rm
