#include "core/UndoHistory.h"

namespace rm {

bool UndoHistory::Push(std::unique_ptr<ICommand> cmd, bool alreadyDone) {
    if (!cmd || executing_) return false;
    if (!alreadyDone) {
        executing_ = true;
        cmd->Do();
        executing_ = false;
    }
    if (redo_.empty() && !undo_.empty() && undo_.back()->MergeWith(*cmd)) {
        if (undo_.back()->IsNoOp()) undo_.pop_back();
        Modified();
        return true;
    }
    undo_.push_back(std::move(cmd));
    redo_.clear();
    if (undo_.size() > maxDepth_) undo_.erase(undo_.begin());
    Modified();
    return true;
}

bool UndoHistory::Undo() {
    if (!CanUndo()) return false;
    std::unique_ptr<ICommand> cmd = std::move(undo_.back());
    undo_.pop_back();
    executing_ = true;
    cmd->Undo();
    executing_ = false;
    redo_.push_back(std::move(cmd));
    Modified();
    return true;
}

bool UndoHistory::Redo() {
    if (!CanRedo()) return false;
    std::unique_ptr<ICommand> cmd = std::move(redo_.back());
    redo_.pop_back();
    executing_ = true;
    cmd->Do();
    executing_ = false;
    undo_.push_back(std::move(cmd));
    Modified();
    return true;
}

void UndoHistory::Clear() {
    undo_.clear();
    redo_.clear();
}

}  // namespace rm
