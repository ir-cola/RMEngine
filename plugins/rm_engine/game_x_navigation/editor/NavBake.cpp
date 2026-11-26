#include "rm_engine/game_x_navigation/editor/NavBake.h"

#include "platform/Shell.h"
#include "rm_engine/navigation/runtime/Bake.h"

#include <chrono>
#include <system_error>

namespace rm::game_x_navigation {

std::string FileNameOf(std::string_view name) {
    std::string out;
    for (const char ch : name) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (u < 0x20 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
            out += '_';
        } else {
            out += ch;
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("NavMesh") : out;
}

BakeOutcome RunBake(const BakeJob& job, std::stop_token stop, const std::function<void(int, int)>& progress) {
    BakeOutcome r;
    r.entity = job.volume.entity;
    r.file = job.out;
    const auto start = std::chrono::steady_clock::now();
    const auto files = job.files;
    game_production::AssetAccess assets;
    assets.file = [files](const AssetRef& ref) {
        const auto it = files.find(ref.guid);
        return it != files.end() ? it->second : std::filesystem::path{};
    };
    const Worlds worlds = WorldsOf(job.scene);
    navigation::NavInput input;
    if (!CollectInput(job.scene, worlds, job.volume, job.sources, &assets, stop, input)) {
        r.stopped = true;
        return r;
    }
    r.triangles = input.Triangles();
    navigation::BakeOptions opt;
    opt.agent = AgentOf(job.type);
    opt.bounds = job.volume.bounds;
    std::string err;
    if (!navigation::Bake(input, opt, stop, progress, r.data, &err)) {
        r.stopped = err.empty();
        r.error = err;
        return r;
    }
    // 前のファイルの、このバージョンが知らないキーは残す (憲法 6)。
    r.head = job.head;
    std::error_code ec;
    if (std::filesystem::exists(job.out, ec)) {
        std::string why;
        if (const auto bytes = platform::ReadFileBytes(job.out, &why)) {
            navigation::NavFileHead old;
            if (navigation::ReadNavFile(*bytes, &old, &why)) r.head.extra = old.extra;
        }
    }
    if (stop.stop_requested()) {
        r.stopped = true;
        return r;
    }
    std::filesystem::create_directories(job.out.parent_path(), ec);
    if (!platform::WriteFileAtomic(job.out, navigation::WriteNavFile(r.data, r.head), &err)) {
        r.error = err.empty() ? std::string("ナビメッシュのファイルを書けません") : err;
        return r;
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    r.ok = true;
    return r;
}

}  // namespace rm::game_x_navigation
