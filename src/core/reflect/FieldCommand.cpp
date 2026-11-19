#include "core/reflect/FieldCommand.h"

namespace rm::reflect {

SetFieldCommand::SetFieldCommand(const TypeDesc& type, FieldPath path, json::Value before, json::Value after,
                                 std::function<EditTarget()> resolve, std::string label, uint64_t mergeKey)
    : type_(type),
      path_(std::move(path)),
      before_(std::move(before)),
      after_(std::move(after)),
      resolve_(std::move(resolve)),
      label_(std::move(label)),
      mergeKey_(mergeKey) {}

bool SetFieldCommand::CanMergeWith(const ICommand& next) const {
    const auto* n = dynamic_cast<const SetFieldCommand*>(&next);
    if (!n || mergeKey_ == 0 || n->mergeKey_ != mergeKey_ || &n->type_ != &type_ || n->path_ != path_) return false;
    // The same object: a key that happens to be the same for another thing never joins them.
    const EditTarget a = resolve_ ? resolve_() : EditTarget{};
    const EditTarget b = n->resolve_ ? n->resolve_() : EditTarget{};
    return a.object && a.object == b.object;
}

bool SetFieldCommand::MergeWith(const ICommand& next) {
    if (!CanMergeWith(next)) return false;
    after_ = static_cast<const SetFieldCommand&>(next).after_;  // this entry now ends where the newer one ends
    return true;
}

void SetFieldCommand::Apply(const json::Value& v) {
    const EditTarget target = resolve_ ? resolve_() : EditTarget{};
    if (!target.object) return;
    const PathTarget t = Resolve(type_, target.object, target.unknowns, path_);
    if (!t.field) return;
    // A value the user chose replaces an unknown enum name that was kept (the user's own choice).
    // Undo brings the kept name back, because `before` holds it.
    if (t.unknowns) {
        t.unknowns->ForgetEnum(t.field->name);
        t.unknowns->ForgetKept(t.field->name);  // (a value of another kind kept from the file: the user's choice replaces it)
    }
    LoadField(*t.field, t.owner, v, t.unknowns, nullptr);
}

}  // namespace rm::reflect
