#pragma once
// 「リフレクションのオブジェクトのフィールドが変わった」ための Undo コマンドは、これ 1 つ - 型は問わない。
// 編集 UI (今はインスペクター、後でギズモなど) はどれもこれを積むので、各 UI が自前のコマンドを
// 書かなくても、「編集 = Undo のエントリー = 未保存」が成り立つ (CLAUDE.md の憲法 7)。

#include "core/UndoHistory.h"
#include "core/reflect/Data.h"

#include <cstdint>
#include <functional>
#include <string>

namespace rm::reflect {

// コマンドが動く時点で、編集するオブジェクトがどこにあるか (その間に読み込み直されたかもしれない)。
struct EditTarget {
    void* object = nullptr;
    Unknowns* unknowns = nullptr;
};

class SetFieldCommand final : public ICommand {
public:
    // mergeKey != 0: 同じキー、同じフィールド、同じオブジェクト (`resolve` が今指す先) を持つ後のコマンドは
    // これに合流する - フォーカスがある間に 1 つのフィールドへ打ったキーは、Undo の 1 エントリーになる。
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
