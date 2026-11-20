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
    if (!import.IsObject()) return;
    if (import.Get("sharp").IsBool()) {
        if (import.Get("filter").IsNull()) import.Set("filter", json::Value(std::string(import.Get("sharp").AsBool() ? "Sharp" : "Smooth")));
        import.Erase("sharp");
    }
    // モデルの "up": "Y" / "Z" (2026-11-20 まで) は "axes" に: Z は「Z が上・右手系」、Y は自動 (前と同じ読み方)。
    if (import.Get("up").IsString()) {
        if (import.Get("axes").IsNull() && import.Get("up").AsString() == "Z") import.Set("axes", json::Value(std::string("ZUpRight")));
        import.Erase("up");
    }
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
        // なくなっていた (アセットが id をもらったとき書けなかった): その id でまた作る。
        return WriteMetaFile(meta, MetaWithImport(MetaText({}, guid), import));
    }
    // そこにあって全部読める .meta (id がある) にだけ: ロックされた・壊れたものには決して上書きせず、
    // id のないものに書くと、そのアセットの id が失われる。
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
