#include "core/reflect/FieldCommand.h"

namespace rm::reflect {

SetFieldCommand::SetFieldCommand(const TypeDesc& type, FieldPath path, json::Value before, json::Value after,
                                 std::function<EditTarget()> resolve, std::string label)
    : type_(type),
      path_(std::move(path)),
      before_(std::move(before)),
      after_(std::move(after)),
      resolve_(std::move(resolve)),
      label_(std::move(label)) {}

void SetFieldCommand::Apply(const json::Value& v) {
    const EditTarget target = resolve_ ? resolve_() : EditTarget{};
    if (!target.object) return;
    const PathTarget t = Resolve(type_, target.object, target.unknowns, path_);
    if (!t.field) return;
    // A value the user chose replaces an unknown enum name that was kept (the user's own choice).
    // Undo brings the kept name back, because `before` holds it.
    if (t.unknowns) t.unknowns->ForgetEnum(t.field->name);
    LoadField(*t.field, t.owner, v, t.unknowns, nullptr);
}

}  // namespace rm::reflect
