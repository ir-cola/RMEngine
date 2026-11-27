#include "rm_engine/animation3d/runtime/MorphPreview.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace rm::animation3d {

namespace {

struct Store {
    std::unordered_map<std::string, MorphTry> models;
    uint64_t version = 1;
};

Store& TheStore() {
    static Store s;
    return s;
}

}  // namespace

const MorphTry* TriedMorphs(std::string_view model) {
    const Store& s = TheStore();
    const auto it = s.models.find(std::string(model));
    return it == s.models.end() || it->second.empty() ? nullptr : &it->second;
}

void TryMorph(std::string_view model, std::string_view name, float weight) {
    Store& s = TheStore();
    MorphTry& list = s.models[std::string(model)];
    std::erase_if(list, [&](const auto& kv) { return kv.first == name; });
    if (std::isfinite(weight) && weight >= 0) list.emplace_back(std::string(name), weight);
    ++s.version;
}

void ClearTriedMorphs(std::string_view model) {
    Store& s = TheStore();
    s.models.erase(std::string(model));
    ++s.version;
}

uint64_t TriedMorphsVersion() { return TheStore().version; }

}  // namespace rm::animation3d
