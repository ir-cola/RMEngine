#pragma once
// The one undo command for "a field of a reflected object was changed" - whatever the type.
// Every edit UI (the inspector today, gizmos and others later) pushes this, so "edit = undo entry
// = unsaved" holds without each UI writing its own command (rule 7 of CLAUDE.md).

#include "core/UndoHistory.h"
#include "core/reflect/Data.h"

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
    SetFieldCommand(const TypeDesc& type, FieldPath path, json::Value before, json::Value after,
                    std::function<EditTarget()> resolve, std::string label);

    void Do() override { Apply(after_); }
    void Undo() override { Apply(before_); }
    std::string Label() const override { return label_; }

private:
    void Apply(const json::Value& v);

    const TypeDesc& type_;
    FieldPath path_;
    json::Value before_, after_;
    std::function<EditTarget()> resolve_;
    std::string label_;
};

}  // namespace rm::reflect
