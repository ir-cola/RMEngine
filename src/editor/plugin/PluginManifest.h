#pragma once
// plugin.json の読み込みと、どのプラグインが使えるかの判定 (純粋。RMTests でテストする)。
//
//   Plugins/<author>/<name>/plugin.json
//   {
//     "author": "rm_engine", "name": "game_ui", "displayName": "ゲーム UI",
//     "requires": [ { "author": "rm_engine", "name": "game_production" } ]
//   }
//
// ID は意図して 2 か所に書く (設計 RME_PLATFORM 3-b、案 "A")。プラグインのヘッダー (C++ 用) と
// plugin.json (コンパイルせずに読める) だ。読み込み時に 2 つを比べ、ID が違うプラグインは
// 断る。食い違いは必ず見つかり、黙ってどちらかを選ぶことはない。
// 前提が無い、または使えないプラグインは有効にせず、理由を伝える。
//
// コネクター (設計 RME_PLATFORM 4-f、案 C): ゲーム制作は 3D も 2D も知ってはいけないので、
// 知っている部分は両側を前提にするコネクタープラグイン ("ゲーム制作 × 3D") に置く:
//   ゲーム制作        "needsConnector": true          -> コネクターのどれかがある間だけ有効
//   コネクター        "connectorFor": { ゲーム制作 }, "requires": [ ゲーム制作, 3D 空間 ]
// 矢印は一方向だけ (コネクター -> 両側) なので、前提が堂々巡りにならない。
// コネクターは、前提が全部そろうと自動で入る (手では選ばない)。

#include "base/Json.h"
#include "core/PluginId.h"

#include <optional>
#include <string>
#include <vector>

namespace rm::editor {

struct PluginManifest {
    PluginId id;
    std::string displayName;  // UTF-8
    std::vector<PluginId> requirements;
    bool needsConnector = false;            // そのコネクターがある間だけ有効
    std::optional<PluginId> connectorFor;   // このプラグインは、そのプラグインのコネクター (前提にも入る)
    // どのソフトに属すか。どのテンプレートも必要としないとき ("software": ["game"]: テンプレートの
    // 木の一番上のノードの ID。editor/project/Templates.h)。Hub の「ソフトで絞る」だけに使う。省略できる。
    std::vector<std::string> softwares;

    bool operator==(const PluginManifest&) const = default;
};

// ID の一覧: [ { "author": ..., "name": ... }, ... ]。"requires" と、プロジェクトファイルに
// 記録するプラグインに使う。各項目に両方の値が要る (推測はしない)。
std::optional<std::vector<PluginId>> ParsePluginIdList(const json::Value& v, std::string* error);
std::optional<PluginManifest> ParsePluginManifest(const json::Value& v, std::string* error);

enum class PluginState {
    Enabled,
    NotInstalled,         // このエンジンにはあるが、ユーザーが入れていない
    NoManifest,           // plugin.json が見つからない
    BadManifest,          // 読めない、形が違う、ID が不正、同じ ID のプラグインが 2 つある
    IdMismatch,           // plugin.json とコードが食い違う
    MissingRequirement,   // 前提のプラグインがこのエンジンに全く無い
    RequirementDisabled,  // 前提のプラグインはあるが、入っていないか使えない
    Cycle,                // 前提が堂々巡りしている
    NoConnector,          // "needsConnector" なのに、使えるコネクターが 1 つも無い
};

// exe に組み込まれた 1 つのプラグインと、その plugin.json から読んだ内容。
struct PluginCandidate {
    PluginId id;  // コードから
    bool installed = true;
    // 入っている場合: 入っている plugin.json。入っていない場合: エンジンの写し (名前だけに使う)。
    std::optional<PluginManifest> manifest;
    bool manifestFound = false;
    std::string manifestError;  // 見つかったが使えないときの理由
};

struct PluginStatus {
    PluginId id;
    std::string displayName;  // plugin.json から。無いときは ID の文字
    PluginState state = PluginState::Enabled;
    std::string reason;  // 日本語。有効なら空
};

struct PluginResolution {
    std::vector<PluginStatus> statuses;  // candidates と同じ順
    std::vector<size_t> order;           // 有効な candidates。前提が先
};

PluginResolution ResolvePlugins(const std::vector<PluginCandidate>& candidates);

// Hub が「⚠ 前提が無い」と印を付ける状態 (入っているが、前提が無い) のとき true。
inline bool LacksRequirement(PluginState s) {
    return s == PluginState::MissingRequirement || s == PluginState::RequirementDisabled || s == PluginState::Cycle ||
           s == PluginState::NoConnector;
}

// --- 入れる・消す (純粋) ------------------------------------------------------
// 設計 RME_PLATFORM 1-b-2 / 5: 前提はプラグインと一緒に入れる。入っているプラグインは
// どれも単独で消せるが、先に、それを必要とする物を並べた警告を出す。

// `id` を使えるようにするために写すもの。`id` の前提の木にあるプラグインすべて (`id` も含む)
// のうち、`ready` (入っていて読める) に無いもの。前提が先で、それぞれ 1 回だけ。
// ready のプラグインは素通りして調べるので、もっと下で足りない前提も見つかる。空 = 写すものは無い。
// 木のうち `available` (このエンジンにある) に無いプラグインは *unavailable に入る。
// コネクターも一緒に来る: 前提が全部そろったコネクターすべてと、コネクターが必要なのに
// 1 つも無くなるプラグインには、このエンジンが持つ最初のコネクターとその前提
// (ゲーム制作だけで 3D 空間 と ゲーム制作 × 3D が来る)。
std::vector<PluginId> InstallPlan(const PluginId& id, const std::vector<PluginManifest>& available,
                                  const std::vector<PluginId>& ready, std::vector<PluginId>* unavailable);

// 写す必要が無い、入っているプラグイン。読めて、このエンジンの写しと同じもの。違う写し
// (古いエンジンのもの: 前提が違う、コネクターの決まりが無いなど) は、次の入れる計画でもう一度
// 写す。このエンジンに無いプラグインは、そのまま受け取る。
std::vector<PluginId> ReadyPlugins(const std::vector<PluginManifest>& installed, const std::vector<PluginManifest>& available);

// エンジンの最初の起動で入れるもの (自分用の入れたプラグインのフォルダーが新しい): 前は
// 全エンジンで共有していたフォルダーにユーザーが入れていたもの (`legacyInstalled`。あれば)、
// 無ければエンジンが持つ全部。エンジンの順で、エンジンが持つプラグインだけ。
std::vector<PluginId> FirstStartInstalls(const std::vector<PluginId>& available, const std::vector<PluginId>& legacyInstalled);

// このエンジンが持つコネクターのうち、つなぐ物がすべて入っているのに入っていないもの
// (新しいエンジンが足したコネクター - ゲーム制作 × 3D × 物理 - を、前から入っているプラグイン用に):
// 起動時に入れる。プラグインを入れたときに一緒に来たはずのものと同じ扱い。エンジンの順で。
std::vector<PluginId> ConnectorsToBring(const std::vector<PluginManifest>& installed, const std::vector<PluginManifest>& available);

struct Dependent {
    PluginId id;
    std::optional<PluginId> via;  // 空: 直接必要とする。それ以外: このプラグインを通して必要とする
};
// `id` を消すと動かなくなる、入っているプラグイン。直接のものが先で、次にそれらを通して
// 必要とするもの。それぞれ 1 回だけ。最後のコネクターを失うプラグインも入る (コネクター経由)。
std::vector<Dependent> Dependents(const PluginId& id, const std::vector<PluginManifest>& installed);

}  // namespace rm::editor
