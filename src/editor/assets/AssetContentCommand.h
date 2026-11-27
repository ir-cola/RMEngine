#pragma once
// アセットの中身の 1 回の編集の Undo (前と後の中身を丸ごと持つ): プラグインが自分のパネルで、自分の種類の
// ファイル (.rmtileset など) を編集するとき。Do / Undo は SoftwareHost::SetAssetContent を通す (読む側は
// すぐに見て、編集が終わったときにファイルへ書く)。`merge` != 0: 同じ欄に続けて打つ間は 1 件 (同じ
// ファイル・同じキー)。
//
//   host.History().Push(std::make_unique<editor::AssetContentCommand>(host, ref, before, after, "名前を変更", key), false);

#include "core/UndoHistory.h"
#include "editor/plugin/Software.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace rm::editor {

class AssetContentCommand final : public ICommand {
public:
    AssetContentCommand(SoftwareHost& host, AssetRef ref, std::string before, std::string after, std::string label, uint64_t merge = 0)
        : host_(host), ref_(std::move(ref)), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)), merge_(merge) {}
    void Do() override { host_.SetAssetContent(ref_, after_); }
    void Undo() override { host_.SetAssetContent(ref_, before_); }
    std::string Label() const override { return label_; }
    bool IsNoOp() const override { return before_ == after_; }
    bool CanMergeWith(const ICommand& next) const override {
        const auto* n = dynamic_cast<const AssetContentCommand*>(&next);
        return n && merge_ != 0 && n->merge_ == merge_ && n->ref_ == ref_;
    }
    bool MergeWith(const ICommand& next) override {
        if (!CanMergeWith(next)) return false;
        after_ = static_cast<const AssetContentCommand&>(next).after_;
        return true;
    }
    size_t Bytes() const override { return before_.size() + after_.size(); }

    // 欄ごとの「続けて打つ間は 1 件」の番号 (名前から。0 は使わない)。
    static uint64_t MergeKey(std::string_view key) {
        uint64_t h = 1469598103934665603ull;
        for (const char ch : key) h = (h ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
        return h == 0 ? 1 : h;
    }

private:
    SoftwareHost& host_;
    AssetRef ref_;
    std::string before_, after_, label_;
    uint64_t merge_;
};

}  // namespace rm::editor
