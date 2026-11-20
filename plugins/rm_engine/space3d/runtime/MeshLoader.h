#pragma once
// 3D 空間 - モデルファイルを専用のスレッドで読む。大きなモデルで画面が止まらないようにする。
//
//   MeshLoader loader;                          // スレッドは最初の要求で始まる
//   auto ticket = loader.Request(file);         // 裏で読む (頼んだ順)
//   ...毎フレーム:
//   if (auto done = loader.Take(ticket)) ...    // nullopt: まだ。そうでなければ done->mesh か done->error
//   loader.Cancel(ticket);                      // もう要らない (始まっていなければ読まない)
//
// スレッドでやるのは読み込み (file -> MeshData: 遅い部分) だけ。GPU に載せることと絵のデコードは
// 呼び出し側 (描画スレッド: Renderer::Upload、ModelPaint) に残すので、Direct3D も WIC も 2 つの
// スレッドから触られない。デストラクターは読んでいるファイルを取り消し (リーダーは、budget に
// 次に確かめるときに止まる)、残りは捨てる。
//
// 読み込みで起きることでエディターが終わることはない: 投げられるもの (メモリ不足など) はエラーに
// なり、どのリーダーも Mesh.h の ReadBudget (頂点、メモリ、歩いたノード) を守る。

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
    using Ticket = uint64_t;  // 0 = なし
    // `file` を読む。場所を作る前に `budget` に確かめる (取り消されたときは、これが「だめ」と言う)。
    using LoadFn = std::function<std::optional<MeshData>(const std::filesystem::path& file, std::string* error, ReadBudget& budget)>;

    struct Result {
        std::optional<MeshData> mesh;  // nullopt: `error` が理由を言う (日本語)
        std::string error;
        double seconds = 0;  // 読むのにかかった時間
    };

    // `load`: ファイルの読み方 (LoadMeshFile。テストは自前のものを渡す)。
    explicit MeshLoader(LoadFn load = nullptr);
    ~MeshLoader();
    MeshLoader(const MeshLoader&) = delete;
    MeshLoader& operator=(const MeshLoader&) = delete;

    Ticket Request(std::filesystem::path file);
    std::optional<Result> Take(Ticket ticket);  // 終わったとき 1 回だけ
    void Cancel(Ticket ticket);
    size_t Pending() const;  // 頼まれて、まだ受け取られていない数 (取り消されたものは数えない)

private:
    void Run(std::stop_token stop);

    LoadFn load_;
    mutable std::mutex mu_;
    std::condition_variable_any wake_;
    std::deque<std::pair<Ticket, std::filesystem::path>> queue_;
    std::unordered_map<Ticket, Result> done_;
    Ticket reading_ = 0;     // 今スレッドにあるもの
    bool dropReading_ = false;  // ... 読んでいる間に取り消された
    std::atomic<bool> stopReading_{false};  // ... リーダーに止まるよう伝える (その budget の cancel)
    Ticket next_ = 1;
    std::jthread worker_;  // 最後: 最初に止めて join される
};

}  // namespace rm::space3d
