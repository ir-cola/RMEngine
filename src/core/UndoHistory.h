#pragma once
// Undo / Redo の履歴 (純粋)。
//
// 前のエンジンから引き継いだ 2 つの決まり (引き継ぎメモ 2-1 / 4-6):
//  * 「履歴に積んだ」 == 「未保存」。持ち主が SetOnModified を自分の dirty フラグに繋ぐので、
//    履歴に届いた編集が、閉じるときに黙って失われることはない。
//  * コマンドは先にスタックから取り出してから実行する。Undo / Redo の最中のコマンドが
//    Push しようとすると断られるので、スタックの順番が狂わない
//    (前のエンジンのバグ #15)。

#include <cstdint>
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
    // フィールドに打ち込むとキーごとに一度に反映されるが、1 つのエントリーにする: 履歴は
    // 最新のエントリーに、次のコマンドを取り込むよう頼む (true = 取り込んだ: 今は `next` の終わりで終わる)。
    virtual bool MergeWith(const ICommand& next) {
        (void)next;
        return false;
    }
    // MergeWith(next) が取り込むかどうか - 何も変えずに尋ねる (グループが合流するのは、
    // 全コマンドが合流できるときだけ。答えないコマンドは、グループに合流させない)。
    virtual bool CanMergeWith(const ICommand& next) const {
        (void)next;
        return false;
    }
    // 合流した後、始めた所で終わるエントリー (打ち込んで Esc) は捨てる。
    virtual bool IsNoOp() const { return false; }
    // エントリーが持つメモリのおよその量 (前後のシーン: テキストにした大きさ)。0 = 小さい。
    // 履歴は最大 UndoHistory::kMaxBytes までのエントリーを持つ (古いものから消える)。
    virtual size_t Bytes() const { return 0; }
};

// ユーザーにとって 1 つの操作になる複数の編集 (選んだ 3 つのオブジェクトのギズモのドラッグ):
// 1 つのエントリーにして、逆の順で Undo する。空のグループは積まないこと。mergeKey != 0: 同じキーで
// 同じ数のコマンドを持つ後のグループは、コマンドごとにこれに合流する (選んだ複数のものの
// フィールドへの打ち込み) - だから、1 回の打ち込みのグループは、同じ対象を同じ順で持たせること。
class CommandGroup final : public ICommand {
public:
    explicit CommandGroup(std::string label, uint64_t mergeKey = 0) : label_(std::move(label)), mergeKey_(mergeKey) {}
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
    bool CanMergeWith(const ICommand& next) const override {
        const auto* n = dynamic_cast<const CommandGroup*>(&next);
        if (!n || mergeKey_ == 0 || n->mergeKey_ != mergeKey_ || n->cmds_.size() != cmds_.size()) return false;
        for (size_t i = 0; i < cmds_.size(); ++i) {
            if (!cmds_[i]->CanMergeWith(*n->cmds_[i])) return false;  // 同じ対象を同じ順で
        }
        return true;
    }
    // 全部か何もしないか: 全コマンドが合流できない限り何も変えない (半分だけ合流したグループは作らない)。
    bool MergeWith(const ICommand& next) override {
        if (!CanMergeWith(next)) return false;
        const auto& n = static_cast<const CommandGroup&>(next);
        for (size_t i = 0; i < cmds_.size(); ++i) cmds_[i]->MergeWith(*n.cmds_[i]);
        return true;
    }
    bool IsNoOp() const override {
        for (const auto& c : cmds_) {
            if (!c->IsNoOp()) return false;
        }
        return true;
    }
    size_t Bytes() const override {
        size_t n = 0;
        for (const auto& c : cmds_) n += c->Bytes();
        return n;
    }

private:
    std::vector<std::unique_ptr<ICommand>> cmds_;
    std::string label_;
    uint64_t mergeKey_ = 0;
};

class UndoHistory {
public:
    void SetOnModified(std::function<void()> fn) { onModified_ = std::move(fn); }
    void SetMaxDepth(size_t n) { maxDepth_ = n; }
    // まとめて持つエントリーはこの量まで (ICommand::Bytes): 変更のたびにコピーを 2 つ持つ大きなシーンで
    // メモリがいっぱいにならないよう、古いエントリーから消える (最新の 1 つは必ず残す)。
    // 履歴 (ソフト) ごとに 256 MB。
    static constexpr size_t kMaxBytes = size_t{256} << 20;
    void SetMaxBytes(size_t n) { maxBytes_ = n; }
    // 履歴が変わるたびに 1 ずつ増える (エントリーを積む、合流する、Undo、Redo、空にする):
    // 編集したデータから計算したものを持つキャッシュが、これを比べる (前から何か変わったか?)。
    uint64_t Changes() const { return changes_; }

    // alreadyDone = true: 編集は呼んだ側がすでに適用している (UI のドラッグでよくある)。
    // Undo()/Redo() の中から呼ぶと false を返す (コマンドは捨てる)。
    // 最新のエントリー以降に Undo されておらず、それが `cmd` を取り込む (ICommand::MergeWith) なら、
    // 新しいエントリーは作らない。結果として何も変えないエントリーになったら、それを消す。
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
        ++changes_;
        if (onModified_) onModified_();
    }
    void Trim();  // 多すぎる、または持つ量が多すぎる間、古いエントリーを消す

    std::vector<std::unique_ptr<ICommand>> undo_;
    std::vector<std::unique_ptr<ICommand>> redo_;
    std::function<void()> onModified_;
    size_t maxDepth_ = 1000;
    size_t maxBytes_ = kMaxBytes;
    uint64_t changes_ = 0;
    bool executing_ = false;
};

}  // namespace rm
