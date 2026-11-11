#pragma once
// The one undo command for "a field of a reflected object was changed" - whatever the type.
// Every edit UI (the inspector today, gizmos and others later) pushes this, so "edit = undo entry
// = unsaved" holds without each UI writing its own command (rule 7 of CLAUDE.md).

#include "core/UndoHistory.h"
#include "core/reflect/Data.h"

#include <cstdint>
#include <functional>
#include <string>

namespace rm::reflect {

// Where the edited object is at the moment the command runs (it may have been reloaded since).
struct EditTarget {
    void* object = nullptr;
    Unknowns* unknowns = nullptr;
};

class SetFieldCommand final : public ICommand {
public:
    // mergeKey != 0: later commands with the same key, the same field and the same object (where
    // `resolve` points now) join this one - the keys typed into one field while it has the focus are
    // one undo entry.
    SetFieldCommand(const TypeDesc& type, FieldPath path, json::Value before, json::Value after,
                    std::function<EditTarget()> resolve, std::string label, uint64_t mergeKey = 0);

    void Do() override { Apply(after_); }
    void Undo() override { Apply(before_); }
    std::string Label() const override { return label_; }
    bool MergeWith(const ICommand& next) override;
    bool CanMergeWith(const ICommand& next) const override;
    bool IsNoOp() const override { return before_ == after_; }

private:
    void Apply(const json::Value& v);

    const TypeDesc& type_;
    FieldPath path_;
    json::Value before_, after_;
    std::function<EditTarget()> resolve_;
    std::string label_;
    uint64_t mergeKey_ = 0;
};

}  // namespace rm::reflect
