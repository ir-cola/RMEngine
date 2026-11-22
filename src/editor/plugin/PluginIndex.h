#pragma once
// プラグインの一覧 index.json を読み、版を比べ、Hub に何を出すかを決める。zip の中のパスが安全かも
// 見る (純粋。RMTests でテストする)。設計は docs/PLUGIN_DISTRIBUTION.md §3〜§5・§7。
//
//   {
//     "kind": "rm_engine.plugin_index", "formatVersion": 1, "updated": "2026-11-21",
//     "plugins": [ { "author": "rm_engine", "name": "theme_paper", "displayName": "紙",
//                    "versions": [ { "version": "0.0.2", "engine": { "min": "0.0.3" }, "code": false,
//                                    "url": "plugins/rm_engine.theme_paper-0.0.2.zip",   (index.json からの相対でもよい)
//                                    "size": 1234, "sha256": "<小文字の 16 進>",
//                                    "requires": [ { "author": ..., "name": ... } ] } ] } ],
//     "engine": { "version": "0.0.3", "url": "...", "size": 0, "sha256": "..." }
//   }
//
// 一覧はエンジンが読むだけで書き戻さないので、知らないキーは読み飛ばすだけ (前のエンジンでも新しい
// 一覧が読める)。形の違う 1 つのプラグイン・1 つの版は理由付きで飛ばし、残りは使う。

#include "base/Json.h"
#include "core/PluginId.h"
#include "editor/plugin/PluginManifest.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor {

// 一覧の場所 (エンジンの 1 か所: 憲法 2)。ユーザーのポートフォリオの専用ページ。試すときは環境変数
// RM_PLUGIN_INDEX に別の URL かローカルの index.json のパスを入れる (HOW_TO_TRY)。
inline constexpr const char* kPluginIndexUrl = "https://ir-cola.github.io/rm_engine/index.json";
inline constexpr const wchar_t* kPluginIndexEnv = L"RM_PLUGIN_INDEX";
// 落とす物の大きさの上限 (一覧・zip・展開したものの合計)。
inline constexpr uint64_t kMaxIndexBytes = 4ull << 20;
inline constexpr uint64_t kMaxPackageBytes = 256ull << 20;

// 版は数 3 つ ("0.0.3")。数字以外・数が 3 つでない物は nullopt。
std::optional<std::array<uint32_t, 3>> ParseVersion(std::string_view text);
// a < b: -1、同じ: 0、a > b: 1。どちらかが版の形でなければ nullopt。
std::optional<int> CompareVersions(std::string_view a, std::string_view b);
// `a` が `than` より新しい。`than` が空 (版を書いていない plugin.json) なら、形の正しい `a` は新しい。
bool IsNewerVersion(std::string_view a, std::string_view than);

struct IndexPackage {
    std::string version;
    std::string engineMin;  // 空: どのエンジンでも
    bool code = false;      // コードがある (段階 1 ではまだ入れられない)
    std::string url;        // 一覧に書いてあるとおり (相対もある: ResolveUrl)
    uint64_t size = 0;
    std::string sha256;     // 64 文字の 16 進
    std::vector<PluginId> requirements;
};
struct IndexPlugin {
    PluginId id;
    std::string displayName;
    std::vector<IndexPackage> versions;
};
struct IndexEngine {
    std::string version, url, sha256;
    uint64_t size = 0;
};
struct PluginIndex {
    std::vector<IndexPlugin> plugins;
    std::optional<IndexEngine> engine;
    std::vector<std::string> skipped;  // 飛ばした物と理由 (ログに出す)
};

// 一覧として読めないとき (形・kind・新しすぎる formatVersion) は nullopt と理由。
std::optional<PluginIndex> ParsePluginIndex(const json::Value& v, std::string* error);

bool EngineFits(const IndexPackage& package, std::string_view engineVersion);
// このエンジンで使える一番新しい版 (無ければ nullptr)。コードのある版は `allowCode` のときだけ。
const IndexPackage* NewestFitting(const IndexPlugin& plugin, std::string_view engineVersion, bool allowCode);

// 一覧の場所 `base` (URL か、ローカルのパス) から見た `ref`。`ref` が http(s):// / file:// / ドライブ付きの
// パスならそのまま。そうでなければ `base` の最後の / か \ までに足す。
std::string ResolveUrl(std::string_view base, std::string_view ref);
// 16 進の SHA-256 が同じか (大文字と小文字は同じに扱う。長さが 64 でなければ違う)。
bool SameSha256(std::string_view a, std::string_view b);
std::string Sha256Hex(const std::array<uint8_t, 32>& digest);

// zip の中の名前の区切りを / にそろえる (Windows の道具は \ で書くことがある)。
std::string NormalizeZipPath(std::string_view name);
// その名前のファイルを展開先のフォルダーの中に置いてよいか。だめなら理由 (TrKey: 画面で Tr する)。
// 空・絶対パス・ドライブ (: を含む)・.. や . の段・Windows の予約名 (con, nul, com1 ...)・
// 末尾が . か空白の段・使えない文字 (< > " | ? * と制御文字)・長すぎる名前。
const wchar_t* ZipPathProblem(std::string_view normalizedName);

// Hub の「ダウンロードできるプラグイン」に出す物: このエンジンで使える一番新しい版が、入っている
// 版 (入っていなければエンジンの写しの版) より新しい物。入っていてもいなくても。コードのある物は出さない。
enum class OfferKind { Install, Update };
struct PluginOffer {
    const IndexPlugin* plugin = nullptr;
    const IndexPackage* package = nullptr;
    OfferKind kind = OfferKind::Install;
    std::string currentVersion;  // 入っている版 (Install のときは空)
};
std::vector<PluginOffer> PluginOffers(const PluginIndex& index, const std::vector<PluginManifest>& installed,
                                      const std::vector<PluginManifest>& available, std::string_view engineVersion);
// 一覧のエンジンの版が今のより新しい (Hub の「新しいエンジンがあります」)。
bool EngineUpdateAvailable(const PluginIndex& index, std::string_view engineVersion);

}  // namespace rm::editor
