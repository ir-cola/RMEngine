#pragma once
// アセットパネル (と参照欄の一覧) 用の、アセットの小さな絵:
//
//   画像ファイル (PNG / JPEG ...)      その画像そのものを縮めた物 (WIC: platform/Image.h)
//   プラグインが描く種類 (3D モデル)    そのプラグインが小さく描く - 核は 3D について何も知らない。
//                                       プラグインが kThumbnailPoint に ThumbnailMaker を出す
//   それ以外                           なし (パネルは前と同じく、その種類の絵を出す)
//
// 開いているプロジェクトにつき 1 つ (どのソフトのパネルも共有する)。アセットの id で保つ。重い部分は
// 専用のスレッドで行う (worker は 1 つ、一番新しく頼まれた物が先 - 今画面にある物):
// 画像はそこで丸ごとデコードする。プラグインの種類はそこで読んで準備し (ThumbnailMaker::
// prepare: ファイル、その絵)、描くのは UI スレッド (draw: GPU。1 フレームがそれに使うのは
// kFrameBudgetMs まで)。何かを作っている間は、パネルはもっとフレームを頼むので、各サムネイルは
// 出来しだい現れる。ファイル (または .meta) は毎秒見直し、変わっていたらサムネイルを作り直す。
// 頼まれたより少し大きく (32 / 64 / 128 / 256 ピクセル) 作るので、パネルのサムネイルを大きくしても、
// 1 歩ごとに全部を作り直しはしない。1 分間出さなかった物は忘れる。

#include "core/asset/AssetIndex.h"
#include "platform/Gfx.h"
#include "platform/Image.h"
#include "ui/Context.h"

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rm::editor {

class PluginRegistry;

// プラグインがある種類のアセットを小さく描くと申し出る所: registry.Contribute(kThumbnailPoint, maker)。
inline constexpr char kThumbnailPoint[] = "core.thumbnail";

// アセットの id -> 今のファイル (フルパス。無ければ空)。サムネイルを頼んだ時点の写しで、どのスレッドでも使える。
using AssetFinder = std::function<std::filesystem::path(std::string_view guid)>;

struct ThumbnailMaker {
    asset::Kind kind = asset::Kind::Model;
    // worker スレッドで: `file` (とそれが指す物 - モデルの画像 - ただし `root`、プロジェクトの
    // Assets/ の中だけ) を、`draw` が要る形に読み込む。null: *error が理由を言う (日本語)。
    // GPU、UI、UI スレッドの物には触れてはいけない。WIC は platform::ThreadImagingFactory() を通す。
    std::shared_ptr<void> (*prepare)(const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                                     std::string* error) = nullptr;
    // UI スレッドで、Direct2D のここまでの描画をすでに GPU へ送った状態で (Direct3D を使ってもよい):
    // prepare が作った物を `size` x `size` ピクセルに描く - platform::PixelOrder::PremultipliedBgra、
    // 何もない所は透ける。false: *error が理由を言う。
    bool (*draw)(platform::Gfx& gfx, const void* prepared, uint32_t size, platform::Image& out, std::string* error) = nullptr;
    // 拡張子で選ぶ (".rmmat": プラグインが作るファイル。`kind` は見ない)。null: `kind` で選ぶ。
    const char* extension = nullptr;
    // `prepare` の代わり (あれば): ファイルが名指す別のアセット (マテリアルの絵) を `find` で引ける。
    std::shared_ptr<void> (*prepareWith)(const std::filesystem::path& file, const AssetFinder& find, uint32_t size, std::string* error) = nullptr;
};

class Thumbnails {
public:
    static constexpr double kFrameBudgetMs = 30.0;

    Thumbnails(platform::Gfx& gfx, const PluginRegistry& registry, std::filesystem::path root);
    ~Thumbnails();  // worker を止める (作っているサムネイルは終わらせて、捨てる)
    Thumbnails(const Thumbnails&) = delete;
    Thumbnails& operator=(const Thumbnails&) = delete;

    // この種類のファイル (`fileName`: 拡張子で選ぶ描き方のため) がサムネイルを持つか (画像と、プラグインが描く種類)。
    bool Has(asset::Kind kind, std::string_view fileName = {}) const;
    // 今のアセットの id -> ファイルの表 (ThumbnailMaker::prepareWith の `find`)。`version` (AssetLibrary::Version)
    // が前と違うときだけ `make` で作り直す。
    void KeepAssets(uint64_t version, const std::function<std::unordered_map<std::string, std::filesystem::path>()>& make);
    // `file` (アセット `guid`) のサムネイル。少なくとも `pixels` の大きさ。まだ作っていないとき
    // (別のフレームを頼む) と、作れないとき (理由は 1 度だけコンソールに出た) は null。
    ID2D1Bitmap* Get(ui::Context& c, const std::string& guid, asset::Kind kind, const std::filesystem::path& file, uint32_t pixels);

private:
    struct Entry {
        platform::Image image;  // 取っておく: 別のウィンドウへ動かしたパネルには、その分のビットマップが要る
        uint32_t size = 0;      // この大きさで作った (0: まだ)
        uint32_t asked = 0;     // worker がこの大きさで作っている (0: 何も頼んでいない)
        bool failed = false;
        std::filesystem::file_time_type time{};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        ID2D1DeviceContext* dc = nullptr;  // ビットマップはこのウィンドウの描画に属する
        double checkedAt = -1e9, usedAt = 0;
    };

    // worker に頼む物と、worker が返す物。
    struct Job {
        std::string guid;
        asset::Kind kind;
        std::filesystem::path file;
        uint32_t size = 0;
        std::filesystem::file_time_type time{};
        std::shared_ptr<const std::unordered_map<std::string, std::filesystem::path>> assets;  // (頼んだ時点の写し)
    };
    struct Done {
        Job job;
        bool ok = false;
        platform::Image image;          // 画像: 済み
        std::shared_ptr<void> prepared;  // プラグインの種類: まだ描く
        std::string error;
    };

    const ThumbnailMaker* MakerFor(asset::Kind kind, std::string_view fileName) const;
    static Done Failed(Job job, std::string why);
    void Work(std::stop_token stop);         // worker スレッド
    void Collect(ui::Context& c);            // worker の結果を entries に入れる (UI スレッド)
    void Finish(Done& d, ui::Context& c);    // 結果 1 つ: 描く必要があれば描いて、取っておく

    platform::Gfx& gfx_;
    const PluginRegistry& registry_;
    std::filesystem::path root_;
    std::unordered_map<std::string, Entry> entries_;
    double frame_ = -1;    // 描いているフレームの時刻 (新しいフレームは予算をリセットする)
    double spentMs_ = 0;   // このフレームでサムネイル作りに使った分
    double sweptAt_ = 0;
    uint64_t generation_ = 0;  // ビットマップを作った GPU のデバイス (platform::Gfx::Generation)
    std::shared_ptr<const std::unordered_map<std::string, std::filesystem::path>> assets_;  // KeepAssets
    uint64_t assetsVersion_ = ~uint64_t{0};

    // worker と共有する (mu_ のもとで)。
    std::mutex mu_;
    std::condition_variable_any wake_;
    std::vector<Job> jobs_;    // 一番新しい物が最後 (それが先に取られる)
    std::vector<Done> done_;
    size_t working_ = 0;       // 渡したがまだ集めていない仕事 (UI スレッドだけ)
    std::vector<Done> toDraw_;  // 準備が済んで、次のフレームの予算を待っている (UI スレッドだけ)
    std::jthread worker_;      // 最後に置く: 最初に止めて join する
};

}  // namespace rm::editor
