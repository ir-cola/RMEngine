#pragma once
// プラグインのパッケージ (zip) を落として、確かめて、一時フォルダーに展開する
// (docs/PLUGIN_DISTRIBUTION.md §5 の 1〜7。置くのは Plugins::InstallDownloaded)。
//
//   1 落とす (platform::Fetch: https か、試すときはローカルのファイル)
//   2 一覧の大きさと SHA-256 と比べる (違えば捨てる)
//   3 zip の中のすべての名前を先に確かめる (ZipPathProblem。1 つでもだめなら何も書かない)
//   4 合計の大きさの上限 (kMaxPackageBytes) を確かめ、空にした `into` に全部を書き出す
//   5 一番上に plugin.json があること
//
// Worker のスレッドで呼ぶ: ログも翻訳もしない。結果の言葉は UI スレッドで Describe が作る。

#include "base/Zip.h"
#include "platform/Http.h"

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>

namespace rm::editor {

struct PackageFetch {
    enum class Fail { None, Fetch, Size, Hash, Zip, Path, TooBig, NoManifest, Disk };
    Fail fail = Fail::None;
    platform::FetchResult fetch;             // Fail::Fetch のとき
    zip::Problem zipProblem = zip::Problem::None;
    const wchar_t* pathProblem = nullptr;    // Fail::Path のとき (TrKey)
    std::string name;                        // だめだった zip の中の名前 (Path / Zip / Disk)
    unsigned long code = 0;                  // Fail::Disk の Windows のエラー番号
    uint64_t gotSize = 0;                    // Fail::Size のとき、落とした大きさ
};

PackageFetch FetchPackage(const std::string& url, uint64_t size, const std::string& sha256, const std::filesystem::path& into,
                          std::stop_token stop = {});
// zip のバイト列を `into` に展開する (FetchPackage の 3〜5。テストでも使う)。
PackageFetch ExtractPackage(const std::string& zipBytes, const std::filesystem::path& into);

// UI スレッドで: 画面に出す理由。
std::wstring DescribeFetch(const platform::FetchResult& r);
std::wstring DescribePackageFetch(const PackageFetch& r);

}  // namespace rm::editor
