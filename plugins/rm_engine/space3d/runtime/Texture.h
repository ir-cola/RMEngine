#pragma once
// 3D 空間 - モデルを塗る絵 (テクスチャ) を GPU に載せる。
//
//   PictureDecoder 絵の重い所をやる専用スレッド。ファイルを読み、ディスクキャッシュ
//                  (TextureFile.h: <project>/Cache/Textures/*.dds) を見て、無ければ
//                  デコードし (WIC)、ミップマップを作り (MakeTexturePixels)、圧縮して
//                  (BlockCompress.h: 使い道と取り込みの設定で BC1 / BC3 / BC4 / BC5 / BC7)
//                  キャッシュファイルを書く。GPU へのコピー (UploadTexture) だけが描画
//                  スレッドなので、大きな絵のモデルでも画面は止まらない
//   TextureCache   絵のファイルをフルパスで持つ。最初に頼まれたときに読んで GPU に載せ、
//                  それを使うモデル全部で共有する。ときどき見直し、ファイル (か .meta) が
//                  変わったら読み直す。新しい絵ができるまで古い絵は残る
//   ModelPaint     1 つのモデルのマテリアルを Renderer::Render が受け取る形 (DrawMaterial)
//                  で持つ。モデルの中に書かれた絵 (.glb / data: URI) はキャッシュの
//                  デコーダーが読む (モデルの取り込みの設定: 中の絵の圧縮 / 大きさの上限)。
//                  横のファイルにある絵はキャッシュから来る
//
// 絵ができるまでは、そのマテリアルは絵なしで描く (TextureCache::Waiting の間、描画側は
// フレームを何度も頼む)。1 フレームの GPU へのコピーは、kUploadSecondsPerFrame かかった
// 所で止める (QueryPerformanceCounter で測る。1 フレームの最初の 1 つは必ず通す)。残りは
// 次のフレームへ回す。別のレンダラーが作った絵 (GPU を作り直した後) は決して使わず、
// 読み直す。読めない絵はコンソールに 1 回だけ (日本語で、理由つきで) 出し、マテリアルは
// 絵なしで描く。kMaxTextureSize (か取り込みの設定) より大きい絵は小さくする。

#include "rm_engine/space3d/runtime/BlockCompress.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/UploadSlice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rm::space3d {

constexpr uint32_t kMaxTextureSize = 4096;
// 1 フレームが GPU への絵のコピーに使ってよい時間 (この数を書く唯一の場所)。
inline constexpr double kUploadSecondsPerFrame = 0.002;

// Render が受け取るマテリアルの数値 (絵はまだ入らない: SetPicture)。
DrawMaterial MaterialNumbers(const MeshMaterial& m);
// その絵。MeshData::images のどれ (-1: なし) をどのスロットに入れるか、色の絵 (sRGB) か、
// 何に使うか。スロット: 0 色、1 金属 / 粗さ、2 法線、3 発光。
struct PictureSlot {
    int image = -1;
    bool srgb = true;
    TextureUse use = TextureUse::Color;
};
constexpr size_t kPictureSlots = 4;
// スロットの読み方 (image は -1): 色の絵か、何に使うか。どの材質でも同じ (ゲーム制作 × 3D の
// マテリアルのファイルも、このスロットの順で絵を持つ)。
PictureSlot SlotKind(size_t slot);
std::array<PictureSlot, kPictureSlots> PictureSlots(const MeshMaterial& m);
void SetPicture(DrawMaterial& d, size_t slot, TextureRef texture);

class PictureDecoder {
public:
    // 何を作るか。絵のファイル、またはバイト列 (モデルの中に書かれた絵。その場合 `file` は
    // モデルで、その .meta が id と取り込みの設定を、`image` がモデルの何番目の絵かを与える)。
    struct Source {
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        std::filesystem::path file;
        int image = -1;
        TextureUse use = TextureUse::Color;
        bool srgb = true;
        std::filesystem::path cacheFolder;  // 空: ディスクには残さない
        bool canBC7 = false;
    };
    // 頼まれた 1 枚の絵。スレッドと共有する。Done() になったら、残りは答え。
    // 始まる前に頼んだ側が手放したら作らない。取られる (Taken: GPU に載せた、または読めないと
    // 分かった) か手放されるまでは待ち (Waiting) に数える。できるまでだけではない。そうしないと
    // フレームの合間にできた絵は、フレームを頼む者がいなくて描かれないままになる。
    class Job {
    public:
        ~Job() { Taken(); }
        bool Done() const { return done_.load(std::memory_order_acquire); }
        void Taken() {
            if (counter_) --*counter_;
            counter_.reset();
        }
        std::optional<TexturePixels> pixels;  // nullopt: `error` に理由 (日本語)
        std::string error;
        TextureSampling sampling;  // 画像の取り込みの設定 (フィルタ・端の扱い・ストリーミング)
        bool fromCache = false;  // キャッシュの .dds から読んだ (デコードも圧縮もやり直していない)
        double seconds = 0;      // スレッドがかかった時間

    private:
        friend class PictureDecoder;
        Source source_;
        std::atomic<bool> done_{false};
        std::shared_ptr<std::atomic<size_t>> counter_;  // デコーダーのもの (ジョブが生かしておく)
    };

    PictureDecoder() = default;
    ~PictureDecoder();  // 作っている絵は終わらせ、残りは捨てる
    PictureDecoder(const PictureDecoder&) = delete;
    PictureDecoder& operator=(const PictureDecoder&) = delete;

    std::shared_ptr<Job> Request(Source source);  // 最初の 1 つでスレッドが始まる
    size_t Waiting() const { return waiting_->load(); }  // 頼まれて、まだ取られていない数

private:
    void Run(std::stop_token stop);
    static void Make(Job& job);

    std::mutex mu_;
    std::condition_variable_any wake_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::shared_ptr<std::atomic<size_t>> waiting_ = std::make_shared<std::atomic<size_t>>(0);
    std::jthread worker_;  // 最後: 最初に止めて待つ
};

class TextureCache {
public:
    static constexpr double kCheckSeconds = 1.0;  // ファイルを見直す間隔

    // 圧縮した絵を置く所 (プロジェクトの Cache/Textures。空: 残さない)。
    void SetCacheFolder(std::filesystem::path folder) { cacheFolder_ = std::move(folder); }
    const std::filesystem::path& CacheFolder() const { return cacheFolder_; }

    // `use` 用の `file` (フルパス) の絵。使い道ごとに別に持つ (色は sRGB で読む)。
    // まだ用意できていない、または読めないときは null (1 回だけ知らせ、ファイルが変わった後にまた)。
    // `now`: 秒。進む時計ならどれでもよく、新しい値は新しいフレーム。絵を使う間は毎フレーム呼ぶ。
    TextureRef Get(Renderer& renderer, const std::filesystem::path& file, TextureUse use, bool srgb, double now);
    // フレーム `now` にまだ時間があれば、できたジョブを GPU に載せる。取った (できた、または
    // 失敗した: `out` は null で *error に理由) なら true。取ったジョブはもう待ちに数えない。
    bool Upload(Renderer& renderer, PictureDecoder::Job& job, double now, TextureRef& out, std::string* error);
    // `seconds` の間だれも頼まなかった絵を GPU から外す。
    void ForgetUnused(double now, double seconds);
    // 全部外す (GPU のデバイスを作り直した: 頼まれたら読み直す)。
    void Clear() { entries_.clear(); }
    size_t Size() const { return entries_.size(); }
    // フレーム `now` で作っている最中か、GPU へのコピーを待っている絵の数 (0 になるまで描き直す)。
    size_t Waiting(double now) const { return decoder_.Waiting() + slice_.Deferred(now); }
    PictureDecoder& Decoder() { return decoder_; }

private:
    struct Entry {
        std::shared_ptr<const GpuTexture> gpu;
        std::shared_ptr<PictureDecoder::Job> job;  // 作っている最中
        std::filesystem::file_time_type time{};
        bool loaded = false;  // `time` の時点の状態で読んだ (か頼んだ)
        bool told = false;    // 無いことをコンソールに書いた
        double checkedAt = 0, usedAt = 0;
    };
    std::unordered_map<std::wstring, Entry> entries_;
    std::filesystem::path cacheFolder_;
    UploadSlice slice_{kUploadSecondsPerFrame};
    PictureDecoder decoder_;  // 最後: スレッドを最初に止める
};

class ModelPaint {
public:
    // モデルのマテリアル。中の絵はデコード待ちとして持つ (キャッシュのデコーダーが、Materials を
    // 最初に呼んだときに読む)。ファイルの絵は `modelFile` の横だけを、`root` (プロジェクトの
    // Assets/) の中だけで探す。使えなかったものはコンソールに書く (`modelName`: メッセージ用)。
    void Load(const MeshData& mesh, const std::filesystem::path& modelFile, const std::filesystem::path& root, const std::string& modelName);
    // Render が受け取る形のマテリアル (MeshPart::material で引く)。絵は用意できたものから入る。
    std::span<const DrawMaterial> Materials(Renderer& renderer, TextureCache& cache, double now);
    size_t Pictures() const;  // マテリアルが使う絵の数 (コンソール用)

private:
    // モデルの中に書かれた絵。読み方ごとに 1 つ (バイト列を持っておく: GPU を作り直したら読み直す)。
    struct Inside {
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        std::string name;
        int image = -1;
        TextureUse use = TextureUse::Color;
        bool srgb = true;
        std::shared_ptr<PictureDecoder::Job> job;
        TextureRef gpu;
        bool failed = false;
    };
    // マテリアルの 1 枚の絵。モデルの中 (inside_ の番号) か、ファイル (キャッシュから)。
    struct Slot {
        int inside = -1;
        std::filesystem::path file;
        TextureUse use = TextureUse::Color;
        bool srgb = true;
        bool Used() const { return inside >= 0 || !file.empty(); }
    };
    std::string name_;
    std::filesystem::path modelFile_;
    std::vector<Inside> inside_;
    std::vector<DrawMaterial> draw_;
    std::vector<std::array<Slot, kPictureSlots>> slots_;  // マテリアルごと
};

}  // namespace rm::space3d
