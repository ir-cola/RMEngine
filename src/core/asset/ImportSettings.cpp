#include "core/asset/ImportSettings.h"

#include "base/Utf.h"

#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace rm::asset {

const reflect::TypeDesc* ImportTypeOf(Kind kind) {
    switch (kind) {
        case Kind::Model: return &reflect::TypeOf<ModelImport>();
        case Kind::Image: return &reflect::TypeOf<ImageImport>();
        default: return nullptr;
    }
}

json::Value ImportInMeta(std::string_view metaText) {
    if (auto v = json::Parse(metaText); v && v->IsObject()) {
        const json::Value& imp = v->Get("import");
        if (imp.IsObject()) return imp;
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

bool WriteImportJson(const fs::path& file, const json::Value& import) {
    const fs::path meta = MetaOf(file);
    std::string old;
    // Only into a .meta that is there and can be read whole (with its id): one that is locked or
    // broken is never written over, and one without an id would lose the asset its id.
    if (!ReadText(meta, old) || ReadMeta(old).state != MetaState::Ok) return false;
    const std::string text = MetaWithImport(old, import);
    // Next to it first, then in place (a crash never leaves half a .meta).
    fs::path tmp = meta;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, meta, ec);
    if (ec) fs::remove(tmp, ec);
    return !ec;
}

fs::file_time_type ChangedAt(const fs::path& file, std::error_code& ec) {
    const fs::file_time_type t = fs::last_write_time(file, ec);
    if (ec) return {};
    std::error_code metaEc;
    const fs::file_time_type m = fs::last_write_time(MetaOf(file), metaEc);
    return !metaEc && m > t ? m : t;
}

}  // namespace rm::asset
