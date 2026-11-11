#include "rm_engine/space3d/runtime/MeshLoader.h"

#include "rm_engine/space3d/runtime/MeshFile.h"

#include <algorithm>
#include <chrono>

namespace rm::space3d {

MeshLoader::MeshLoader(LoadFn load)
    : load_(load ? std::move(load)
                 : LoadFn([](const std::filesystem::path& file, std::string* error, ReadBudget& budget) { return LoadMeshFile(file, error, &budget); })) {}

MeshLoader::~MeshLoader() {
    if (worker_.joinable()) {
        worker_.request_stop();
        stopReading_ = true;  // the file being read stops at its next ask (after request_stop: see Run)
        wake_.notify_all();
        worker_.join();  // waits for the file being read now, if any
    }
}

MeshLoader::Ticket MeshLoader::Request(std::filesystem::path file) {
    std::lock_guard lock(mu_);
    const Ticket t = next_++;
    queue_.emplace_back(t, std::move(file));
    if (!worker_.joinable()) worker_ = std::jthread([this](std::stop_token stop) { Run(stop); });
    wake_.notify_one();
    return t;
}

std::optional<MeshLoader::Result> MeshLoader::Take(Ticket ticket) {
    std::lock_guard lock(mu_);
    const auto it = done_.find(ticket);
    if (it == done_.end()) return std::nullopt;
    Result r = std::move(it->second);
    done_.erase(it);
    return r;
}

void MeshLoader::Cancel(Ticket ticket) {
    if (ticket == 0) return;
    std::lock_guard lock(mu_);
    std::erase_if(queue_, [&](const auto& q) { return q.first == ticket; });
    done_.erase(ticket);
    if (reading_ == ticket) {
        dropReading_ = true;
        stopReading_ = true;
    }
}

size_t MeshLoader::Pending() const {
    std::lock_guard lock(mu_);
    return queue_.size() + done_.size() + (reading_ != 0 && !dropReading_ ? 1 : 0);
}

void MeshLoader::Run(std::stop_token stop) {
    std::unique_lock lock(mu_);
    for (;;) {
        wake_.wait(lock, stop, [&] { return !queue_.empty(); });
        if (stop.stop_requested()) return;
        auto [ticket, file] = std::move(queue_.front());
        queue_.pop_front();
        reading_ = ticket;
        dropReading_ = false;
        stopReading_ = stop.stop_requested();  // closing already (the destructor sets it after asking to stop)
        lock.unlock();

        Result r;
        const auto start = std::chrono::steady_clock::now();
        // Whatever goes wrong while reading becomes "not readable" (an exception leaving a thread's
        // function would end the whole program).
        try {
            ReadBudget budget(&stopReading_);
            r.mesh = load_(file, &r.error, budget);
        } catch (const std::bad_alloc&) {
            r.mesh.reset();
            r.error = "メモリーが足りないので読めません";
        } catch (const std::exception& e) {
            r.mesh.reset();
            r.error = std::string("読み込み中に問題が起きました (") + e.what() + ")";
        } catch (...) {
            r.mesh.reset();
            r.error = "読み込み中に問題が起きました";
        }
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        lock.lock();
        if (!dropReading_) done_[ticket] = std::move(r);
        reading_ = 0;
        dropReading_ = false;
    }
}

}  // namespace rm::space3d
