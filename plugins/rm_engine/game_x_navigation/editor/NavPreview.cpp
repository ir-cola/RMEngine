#include "rm_engine/game_x_navigation/editor/NavPreview.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "platform/Shell.h"

#include <map>
#include <system_error>

namespace rm::game_x_navigation {

namespace {

struct Entry {
    std::filesystem::path file;
    std::filesystem::file_time_type time{};
    std::unique_ptr<NavPreview> preview;
};

std::map<std::string, Entry>& Cache() {
    static std::map<std::string, Entry> cache;
    return cache;
}

std::filesystem::file_time_type TimeOf(const std::filesystem::path& file) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(file, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

void Fill(NavPreview& p, const navigation::NavData& data, const navigation::NavFileHead& head) {
    p.head = head;
    p.info = data;
    p.info.tiles.clear();
    std::string err;
    p.ok = p.mesh.Load(data, &err);
    p.error = err;
    p.edges.clear();
    p.polygons.clear();
    p.blocked.reset();
    if (!p.ok) return;
    p.mesh.Edges(p.edges);
    p.mesh.PolygonsOf(p.polygons);
    p.polygonCount = p.mesh.Polygons();
}

}  // namespace

NavPreview* FindPreview(editor::SoftwareHost& host, const AssetRef& ref) {
    if (ref.Empty()) return nullptr;
    const std::filesystem::path file = host.AssetFile(ref);
    if (file.empty()) return nullptr;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return nullptr;
    const auto time = TimeOf(file);
    Entry& e = Cache()[ref.guid];
    if (e.preview && e.file == file && e.time == time) return e.preview.get();
    e.file = file;
    e.time = time;
    e.preview = std::make_unique<NavPreview>();
    std::string why;
    const auto bytes = platform::ReadFileBytes(file, &why);
    if (!bytes) {
        e.preview->error = why;
        return e.preview.get();
    }
    navigation::NavFileHead head;
    const auto data = navigation::ReadNavFile(*bytes, &head, &why);
    if (!data) {
        e.preview->error = why;
        log::Warn("ナビ: 焼いたナビメッシュを読めません ({}): {}", PathToUtf8(file.filename()), why);
        return e.preview.get();
    }
    Fill(*e.preview, *data, head);
    return e.preview.get();
}

void RememberPreview(const AssetRef& ref, const std::filesystem::path& file, const navigation::NavData& data, const navigation::NavFileHead& head) {
    if (ref.Empty()) return;
    Entry& e = Cache()[ref.guid];
    e.file = file;
    e.time = TimeOf(file);
    e.preview = std::make_unique<NavPreview>();
    Fill(*e.preview, data, head);
}

}  // namespace rm::game_x_navigation
