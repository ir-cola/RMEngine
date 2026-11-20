#pragma once
// データの型が持てる参照 (RM_PROPERTY のフィールド、core/reflect): 純粋な値で、ID として保存する。
//
//   RM_PROPERTY(Edit, Label="モデル", AssetKind="model") rm::AssetRef model;   // Assets/ のファイル
//   RM_PROPERTY(Edit, Label="追いかける相手")              rm::EntityRef target; // 階層の項目
//
// アセットは、隣にある .meta ファイルの id で名前を指す (core/asset)。だからファイルを動かしても
// 名前を変えても参照は壊れない。エンティティ (ソフトの階層の項目 - ゲームのエンティティ) は
// そこにある id で指す。インスペクターは名前を表示し、アセット / 階層パネルからのドラッグを受け、
// 選ぶための一覧を出し、× で空にできる。

#include <cstdint>
#include <string>

namespace rm {

struct AssetRef {
    std::string guid;  // 小文字の 16 進 32 桁。空 = なし
    bool Empty() const { return guid.empty(); }
    bool operator==(const AssetRef&) const = default;
};

struct EntityRef {
    uint64_t id = 0;  // 0 = なし
    bool Empty() const { return id == 0; }
    bool operator==(const EntityRef&) const = default;
};

}  // namespace rm
