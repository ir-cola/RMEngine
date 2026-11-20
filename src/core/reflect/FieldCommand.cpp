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
    // 同じオブジェクト: 別のものでたまたま同じになったキーでは、決して合流しない。
    const EditTarget a = resolve_ ? resolve_() : EditTarget{};
    const EditTarget b = n->resolve_ ? n->resolve_() : EditTarget{};
    return a.object && a.object == b.object;
}

bool SetFieldCommand::MergeWith(const ICommand& next) {
    if (!CanMergeWith(next)) return false;
    after_ = static_cast<const SetFieldCommand&>(next).after_;  // このエントリーは、新しいほうが終わる所で終わるようになる
    return true;
}

void SetFieldCommand::Apply(const json::Value& v) {
    const EditTarget target = resolve_ ? resolve_() : EditTarget{};
    if (!target.object) return;
    const PathTarget t = Resolve(type_, target.object, target.unknowns, path_);
    if (!t.field) return;
    // ユーザーが選んだ値は、残してあった知らない enum の名前を置き換える (ユーザー自身の選択)。
    // `before` が持っているので、Undo すると残してあった名前が戻る。
    if (t.unknowns) {
        t.unknowns->ForgetEnum(t.field->name);
        t.unknowns->ForgetKept(t.field->name);  // (ファイルから残してあった別の種類の値: ユーザーの選択がそれを置き換える)
    }
    LoadField(*t.field, t.owner, v, t.unknowns, nullptr);
}

}  // namespace rm::reflect
