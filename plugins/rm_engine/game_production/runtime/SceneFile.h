#pragma once
// ゲームのシーンを、シーンごとに 1 つのファイルに残す (.rmpf の中ではなく: .rmpf には
// platform::kMaxFileBytes の上限があり、保存では変わったシーンだけを書く)。純粋: ここではファイルを
// 触らない - tests/TestSceneFile.cpp でテストする。エディター (GameSoftware) と、あとで書き出した
// ゲームがそのファイルを読む。
//
//   Assets/Scenes/<名前>.rmscene      ほかと同じアセット: id を持つ .meta が隣にある
//   { "kind": "rm_engine.scene", "format": 1, "nextId": 7, "entities": [ ... ] }    (Scene::Save + kind / format)
//
//   .rmpf  "softwareData": [ { ... "software": "game", "data": {
//              "settings":  { ... ゲームの設定 ... },
//              "sceneList": { "scenes": ["<アセット id>", ...], "open": "<アセット id>" } } } ]
//
// 前のプロジェクト (0.0.1: .rmpf の中に "scene" として 1 つのシーン) も、まだ読める。次の保存で
// シーンを自分のファイルに書き、.rmpf から "scene" を落とし、.rmpf はもとのまま 1 回だけ、その
// 隣に残す (<名前>.rmpf.before-scenes.bak)。
//
// 新しい版のキーは残る: シーンのファイル (シーン自身の知らないキー。kind / format も含む)、
// "sceneList" (reflect::Unknowns)、ゲームのデータ (エディターは読んだままのデータに保存する)。

#include "base/Json.h"
#include "base/Ref.h"
#include "core/RuntimeRegistry.h"
#include "core/reflect/Reflect.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::game_production {

inline constexpr char kSceneKind[] = "rm_engine.scene";
// このバージョンが書く "format"。2 (2026-11-20): 左手系 (右・上・奥が +)。1 は右手系で、読むときに
// Z で写す (MirrorZ: 見た目は前と同じ)。
inline constexpr int64_t kSceneFormat = 2;
inline constexpr char kSceneExtension[] = ".rmscene";
inline constexpr char kScenesFolder[] = "Assets/Scenes";  // 新しいシーンのファイルの置き場所 (プロジェクトのフォルダから)
inline constexpr char kOldSceneBackupSuffix[] = ".before-scenes.bak";  // <名前>.rmpf + this

// ゲームのシーン。.rmpf ("sceneList") の中: アセット id をゲームの順番で (書き出したゲームに
// 入るもの。Game::LoadScene(number) はこの中で数える)、ゲームが始まるシーン、エディターが
// 見せているシーン。
RM_STRUCT()
struct SceneList {
    RM_PROPERTY() std::vector<AssetRef> scenes;
    RM_PROPERTY() AssetRef open;   // 空: 最初のもの
    RM_PROPERTY() AssetRef first;  // ゲームはここから始まる (空、またはリストに無いとき: リストの最初)
    // 書き出しから外すリストのシーン (ゲームの設定 ▸ ビルド)。ゲームが始まるシーンは必ず入る。
    // エディターの Play もこれらを外す (PlayScenes: ゲームと同じ番号)。
    RM_PROPERTY() std::vector<AssetRef> notBuilt;
};
// そのシーンが書き出しに入る (外されていない - または、ゲームが始まるシーン)。
bool SceneBuilt(const SceneList& list, const AssetRef& scene);

// 見せるシーン: リストにあれば `open`、なければ最初のもの。リストが空なら空。
AssetRef OpenScene(const SceneList& list);
// ゲームが始まるシーン: リストにあれば `first`、なければリストの最初。リストが空なら空。
AssetRef FirstScene(const SceneList& list);
// ゲームのシーンを順番に: リストのもの、そのあとにエディターで開いているシーンがリストに
// 無いとき (まだ保存していない新しいシーン: 空の ref) - Play が切り替えられるものと、
// 書き出しに入れるもの。
std::vector<AssetRef> GameScenes(const SceneList& list, const AssetRef& open);
// エディターの Play が切り替えられるシーン。書き出したゲームと同じ番号を付ける: GameScenes から
// ビルドで外したものを除き、開いているものが外されているときは、それを最後に置く (遊べるが、
// そのあとのシーンは書き出したゲームの番号を保つ)。
std::vector<AssetRef> PlayScenes(const SceneList& list, const AssetRef& open);
// シーンの名前。Game::LoadScene("…") が受け取る形: ファイル名から .rmscene を除いたもの
// ("Assets/Scenes/Title.rmscene" -> "Title")。
std::string SceneNameOf(std::string_view path);
// その名前の最初のシーンの場所 (nullopt: 無い)。
std::optional<size_t> FindSceneByName(const std::vector<std::string>& names, std::string_view name);

// シーンのファイル: kind と format が先、そのあとにシーンのキー (Scene::Save) - 読んだファイル
// から持ってきた "kind" / "format" は、このバージョンのものに置き換える。
std::string SceneFileText(const json::Value& scene);
// シーンファイルのテキストの中のシーン (Scene::Load に渡すオブジェクト)。断るときは、理由
// (日本語) を *error に入れる: JSON ではない、シーンファイルではない (kind)、新しいバージョンが
// 書いた (format) - 読まず、したがって上書きもしない。
// `registry` があれば、形式 1 (右手系) のシーンを左手系に写す (MirrorZ)。
std::optional<json::Value> ReadSceneFile(std::string_view text, std::string* error, const RuntimeRegistry* registry = nullptr);

// PluginRegistry の口: 中身は MirrorZInfo (static)。左手系 (2026-11-20) より前のファイル (シーンの
// 形式 1・Prefab の形式 1) を読むとき、部品とシーンの値の "data" を Z で写す (位置の Z・オイラー角の
// X と Y を反転、など): 見た目を前と同じに保つ。その型を出すプラグインが、自分の値の写し方を言う。
inline constexpr char kMirrorZPoint[] = "rm_engine.game_production.mirror_z";
struct MirrorZInfo {
    const char* type = nullptr;                // TypeDesc::name ("rm::space3d::Transform")
    void (*mirror)(json::Value& data) = nullptr;  // その部品の "data" を書き換える
};
// JSON の数の並び (Vec3 の [x, y, z]) の `index` 番目の符号を変える (無い・数でないときは何もしない)。
void NegateJsonNumber(json::Value& data, std::string_view key, size_t index);
// シーンや Prefab の JSON 全体 (entities[].components[] と settings[]) を写す。
void MirrorZ(json::Value& doc, const RuntimeRegistry& registry);

// ファイルのテキストの短い印 (FNV-1a, 64 ビット): シーンは、そのテキストの印が、ディスクの
// ファイル (読んだとき、または最後に書いたとき) の印と違うときだけ書く。0 は決して出ない。
uint64_t TextSign(std::string_view text);

// 前のプロジェクトのデータ: そのシーン ("scene")。`data` から取り出す (`data` はほかのキーを
// そのまま持つ)。無いとき、またはすでに "sceneList" があるときは何もしない (`data` はそのまま)。
std::optional<json::Value> TakeOldScene(json::Value& data);

// 新しいシーンのファイルの置き場所: Assets/Scenes/<name>.rmscene。`taken` がそれは有ると
// 言うときは "<name> 2"、"<name> 3" ...。`name` は正しいファイル名 (base/FileName) でなければならない。
template <class Taken>
std::string NewScenePath(std::string_view name, Taken taken) {
    for (int i = 1;; ++i) {
        std::string p = std::string(kScenesFolder) + "/" + std::string(name) + (i == 1 ? "" : " " + std::to_string(i)) + kSceneExtension;
        if (!taken(p) || i > 10000) return p;
    }
}

// 1 つのシーンの保存分: 何を書くか。
struct SceneSlot {
    std::string guid;     // そのアセット id ("": まだ無い - 前のシーン、またはまだ保存していない)
    std::string path;     // そのファイル。プロジェクトのフォルダから ("Assets/Scenes/Main.rmscene")
    uint64_t onDisk = 0;  // 読んだとき、または最後に書いたときのファイルの TextSign (0: まだファイルが無い)
};
struct FileWrite {
    std::string path;  // プロジェクトのフォルダから
    std::string text;
};
struct ScenePlan {
    std::vector<FileWrite> files;  // この順で: 新しいシーンの .meta が先、そのあとシーン
    SceneSlot after;               // ファイルを書いたあとのスロット (その id、ファイルの印)
};
// `scene` (Scene::Save) を `slot` に保存すると何を書くか: テキストがディスクにあるものと同じなら
// 何も書かない。変わったらシーンのファイル。id の無いシーンには、`newGuid` を与える .meta も
// (シーンより先に書く: フォルダを見ただけで別の id を与えてしまうことが無いように)。
ScenePlan PlanSceneSave(const json::Value& scene, const SceneSlot& slot, const std::string& newGuid);

// `tried` (その `after`) を計画した保存が途中で失敗したあとに保存するスロット - ファイルの
// いくつかは書け、いくつかは書けていないが、どれかは分からない: ディスクのファイルはもう
// 分からない (onDisk 0: シーンはまた書く)。.meta が書けた新しいシーン (`newMetaThere`) は、その
// id とファイルを持ち続ける (そうでなければ id 無しのまま: .meta を先に書いたので、そのファイルも無い)。
SceneSlot SlotAfterFailedSave(const SceneSlot& slot, const SceneSlot& tried, bool newMetaThere);

}  // namespace rm::game_production
