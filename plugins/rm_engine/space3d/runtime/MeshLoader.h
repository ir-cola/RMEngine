#pragma once
// 3D 空間 - reading model files on a thread of their own, so a big model does not stop the screen.
//
//   MeshLoader loader;                          // the thread starts with the first request
//   auto ticket = loader.Request(file);         // read in the background (in the order asked)
//   ...each frame:
//   if (auto done = loader.Take(ticket)) ...    // nullopt: not yet; then done->mesh or done->error
//   loader.Cancel(ticket);                      // not wanted any more (never read if not started)
//
// Only the reading (file -> MeshData: the slow part) happens on the thread. Putting it on the GPU
// and decoding its pictures stay with the caller (the drawing thread: Renderer::Upload, ModelPaint),
// so nothing of Direct3D or WIC is touched from two threads. The destructor cancels the file being
// read (the reader stops at its next ask of the budget) and drops the rest.
//
// Nothing the reading does can end the editor: what it throws (out of memory, ...) becomes an
// error, and every reader keeps to Mesh.h's ReadBudget (vertices, memory, nodes walked).

#include "rm_engine/space3d/runtime/Mesh.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace rm::space3d {

class MeshLoader {
public:
    using Ticket = uint64_t;  // 0 = none
    // Reads `file`; asks `budget` before making room (it says no when cancelled).
    using LoadFn = std::function<std::optional<MeshData>(const std::filesystem::path& file, std::string* error, ReadBudget& budget)>;

    struct Result {
        std::optional<MeshData> mesh;  // nullopt: `error` says why (Japanese)
        std::string error;
        double seconds = 0;  // how long the reading took
    };

    // `load`: how a file is read (LoadMeshFile; tests give their own).
    explicit MeshLoader(LoadFn load = nullptr);
    ~MeshLoader();
    MeshLoader(const MeshLoader&) = delete;
    MeshLoader& operator=(const MeshLoader&) = delete;

    Ticket Request(std::filesystem::path file);
    std::optional<Result> Take(Ticket ticket);  // once, when it is done
    void Cancel(Ticket ticket);
    size_t Pending() const;  // asked for and not taken yet (cancelled ones not counted)

private:
    void Run(std::stop_token stop);

    LoadFn load_;
    mutable std::mutex mu_;
    std::condition_variable_any wake_;
    std::deque<std::pair<Ticket, std::filesystem::path>> queue_;
    std::unordered_map<Ticket, Result> done_;
    Ticket reading_ = 0;     // the one on the thread now
    bool dropReading_ = false;  // ... cancelled while being read
    std::atomic<bool> stopReading_{false};  // ... tells its reader to stop (its budget's cancel)
    Ticket next_ = 1;
    std::jthread worker_;  // last: stopped and joined first
};

}  // namespace rm::space3d
