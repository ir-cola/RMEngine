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
    Trim();
    Modified();
    return true;
}

void UndoHistory::Trim() {
    size_t bytes = 0;
    for (const auto& c : undo_) bytes += c->Bytes();
    size_t drop = 0;
    while (undo_.size() - drop > 1 && (undo_.size() - drop > maxDepth_ || bytes > maxBytes_)) {
        bytes -= undo_[drop]->Bytes();
        ++drop;
    }
    if (drop > 0) undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(drop));
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
    ++changes_;
}

}  // namespace rm
