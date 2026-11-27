#pragma once
// ゲーム制作 × タイルマップ (エディター) - タイルを塗った 1 回 (1 回のドラッグ・クリック) の Undo (憲法 7)。
// 設計: docs/TILEMAP.md 7-4。
//
// 層のエンティティの部品の場所 (`resolve`: 呼ぶたびに ID と型で探し直す - SetFieldCommand と同じ考え) と、
// 触った区画ごとの (区画の番号, 前の文字, 後の文字) だけを持つ (空の文字 = その区画が無い)。だから大きな
// マップでも軽い (SceneRecord のようにエンティティ全体の JSON を取らない)。

#include "core/UndoHistory.h"
#include "core/reflect/FieldCommand.h"
#include "rm_engine/tilemap/runtime/TileStore.h"

#include <functional>
#include <string>
#include <vector>

namespace rm::game_x_tilemap {

struct ChunkChange {
    int x = 0, y = 0;
    std::string before, after;  // 区画のランレングスの文字 (空: 無い区画)
};

class TileEditCommand final : public ICommand {
public:
    TileEditCommand(std::function<reflect::EditTarget()> resolve, std::vector<ChunkChange> changes, std::string label);
    void Do() override { Apply(true); }
    void Undo() override { Apply(false); }
    std::string Label() const override { return label_; }
    bool IsNoOp() const override;
    size_t Bytes() const override;

private:
    void Apply(bool after);
    std::function<reflect::EditTarget()> resolve_;
    std::vector<ChunkChange> changes_;
    std::string label_;
};

}  // namespace rm::game_x_tilemap
