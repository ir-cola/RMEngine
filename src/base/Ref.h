#pragma once
// References a data type can hold (RM_PROPERTY fields, core/reflect): pure values, saved as IDs.
//
//   RM_PROPERTY(Edit, Label="モデル", AssetKind="model") rm::AssetRef model;   // a file of Assets/
//   RM_PROPERTY(Edit, Label="追いかける相手")              rm::EntityRef target; // an item of the 階層
//
// An asset is named by the id in the .meta file next to it (core/asset), so moving or renaming the
// file never breaks the reference. An entity (an item of a software's hierarchy - a game's entity)
// by its id there. The inspector shows the name, takes a drag from the アセット / 階層 panels,
// offers a list to pick from and a × to clear it.

#include <cstdint>
#include <string>

namespace rm {

struct AssetRef {
    std::string guid;  // 32 lower-case hex digits; empty = none
    bool Empty() const { return guid.empty(); }
    bool operator==(const AssetRef&) const = default;
};

struct EntityRef {
    uint64_t id = 0;  // 0 = none
    bool Empty() const { return id == 0; }
    bool operator==(const EntityRef&) const = default;
};

}  // namespace rm
