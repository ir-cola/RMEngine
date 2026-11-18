#include "core/asset/ImportSettings.h"

#include "base/Utf.h"
#include "core/asset/AssetFiles.h"
#include "core/asset/AssetIndex.h"

#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace rm::asset {

const reflect::TypeDesc* ImportTypeOf(Kind kind) {
    switch (kind) {
        case Kind::Model: return &reflect::TypeOf<ModelImport>();
        case Kind::Image: return &reflect::TypeOf<ImageImport>();
        case Kind::Audio: return &reflect::TypeOf<SoundImport>();
        default: return nullptr;
    }
}

void MigrateImport(json::Value& import) {
    if (!import.IsObject() || !import.Get("sharp").IsBool()) return;
    if (import.Get("filter").IsNull()) import.Set("filter", json::Value(std::string(import.Get("sharp").AsBool() ? "Sharp" : "Smooth")));
    import.Erase("sharp");
}

json::Value ImportInMeta(std::string_view metaText) {
    if (auto v = json::Parse(metaText); v && v->IsObject()) {
        json::Value imp = v->Get("import");
        if (imp.IsObject()) {
            MigrateImport(imp);
            return imp;
        }
    }
    return json::Value::MakeObject();
}

std::string MetaWithImport(std::string_view metaText, const json::Value& import) {
    json::Value doc = json::Value::MakeObject();
    if (auto v = json::Parse(metaText); v && v->IsObject()) doc = std::move(*v);
    doc.Set("import", import);
    return json::Write(doc) + "\n";
}

namespace {

fs::path MetaOf(const fs::path& file) {
    fs::path p = file;
    p += PathFromUtf8(kMetaSuffix);
    return p;
}

bool ReadText(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

}  // namespace

json::Value ReadImportJson(const fs::path& file) {
    std::string text;
    if (!ReadText(MetaOf(file), text)) return json::Value::MakeObject();
    return ImportInMeta(text);
}

bool WriteImportJson(const fs::path& file, const json::Value& import, const std::string& guid) {
    const fs::path meta = MetaOf(file);
    std::string old;
    std::error_code ec;
    if (!fs::exists(meta, ec) && !ec && IsGuid(guid)) {
        // Gone (it could not be written when the asset got its id): made again with that id.
        return WriteMetaFile(meta, MetaWithImport(MetaText({}, guid), import));
    }
    // Only into a .meta that is there and can be read whole (with its id): one that is locked or
    // broken is never written over, and one without an id would lose the asset its id.
    if (!ReadText(meta, old) || ReadMeta(old).state != MetaState::Ok) return false;
    return WriteMetaFile(meta, MetaWithImport(old, import));
}

fs::file_time_type ChangedAt(const fs::path& file, std::error_code& ec) {
    const fs::file_time_type t = fs::last_write_time(file, ec);
    if (ec) return {};
    std::error_code metaEc;
    const fs::file_time_type m = fs::last_write_time(MetaOf(file), metaEc);
    return !metaEc && m > t ? m : t;
}

}  // namespace rm::asset
