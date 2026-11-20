#pragma once
// シーンのエンティティとその親 (プロジェクトに保存する。書き出したゲームが読む)。
// 純粋: 描かず、ファイルも触らない - tests/TestScene.cpp でテストする。
//
// Entity は 1 か所で定義する (RM_STRUCT): 保存・読み込み・インスペクター・Undo はそこから作る。
// リストの中のエンティティの順番が兄弟 (同じ親の子) の順番で、階層に出す順番でもある。
//
// エンティティは名前、スイッチ、部品 (Components.h) を持つ: 型ごとに 1 つ、または複数を許す型
// (ComponentInfo::multiple) なら複数。部品は、その型と、エンティティの同じ型の部品の中での
// 番号 (0 = 最初) で見つける。
//
// Prefab (Prefab.h): 置いた Prefab のエンティティは PrefabLink を持つ - Prefab ファイルのどの
// エンティティか、置いたルートではそのファイルと、それとの違い。別の Prefab のファイルの中に
// 置いた Prefab (入れ子) は両方を持つ: それ自身が置いた Prefab であり、まわりの Prefab の
// エンティティの 1 つでもある (`outer`)。

#include "base/Json.h"
#include "base/Ref.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Components.h"

#include <algorithm>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>

namespace rm::game_production {

RM_STRUCT(Label="エンティティ")
struct Entity {
    RM_PROPERTY() int id = 0;      // 保存するが見せない: ほかのデータが指すもの (使い回さない)
    RM_PROPERTY() int parent = 0;  // 親の id。0 = 一番上
    RM_PROPERTY(Edit, Label="名前") std::string name = "エンティティ";
    RM_PROPERTY(Edit, Label="有効", Tooltip="外すと、このエンティティと中のもの全部がゲームで動きません") bool active = true;
    // エンティティを探したり並べたりするための名前 (スクリプト: rm::FindWithTag, Entity::Layer)。ゲームの設定が一覧にする。
    RM_PROPERTY(Edit, Label="タグ", Tooltip="スクリプトで探すときの名前 (rm::FindWithTag(\"Enemy\"))。ゲームの設定の「タグ」に一覧を書いておけます。空: なし") std::string tag;
    RM_PROPERTY(Edit, Label="レイヤー", Tooltip="ゲームの設定の「レイヤー」の名前。空: 既定 (0 番)") std::string layer;
};

// 置いた Prefab のエンティティがどこから来たか。エンティティの "prefab" キーとして保存する (設定
// されたときだけ):
//   { "source": 3, "asset": "<.rmprefab の id>", "overrides": {...}, "outer": 2 }
//   asset / overrides: 置いた Prefab のルートだけ。outer: 置いた Prefab のまわりの Prefab の
//   エンティティの 1 つでもあるルート (入れ子) - その Prefab のファイルでの id。
// アセットに id が無かった頃のデータは "asset" の代わりに "path": "Assets/Cube.rmprefab" を持つ:
// そのまま `asset` に読み込み (SoftwareHost::AssetFile がどちらでもファイルを見つける)、
// "asset" として書く。
struct PrefabLink {
    int source = 0;          // エンティティの Prefab ファイル内での id (ルート: ファイルの最初)。0 = Prefab から来ていない
    AssetRef asset;          // 置いた Prefab のルート: Prefab のファイル
    json::Value overrides;   // ルート: Prefab との違い (Prefab.h)。最後に保存した時点のもの
    int outer = 0;           // 入れ子のルート: まわりの Prefab のファイルでの id。0 = 手で置いた
    json::Value extra;       // 読んだままの "prefab" オブジェクト (新しい版のキーは残る)

    bool Linked() const { return source > 0; }
    bool Root() const { return source > 0 && !asset.Empty(); }
};

struct SceneEntity {
    Entity data;
    reflect::Unknowns unknowns;  // 新しい版のキー: そのまま書き戻す
    std::vector<Component> components;  // 足した順
    PrefabLink prefab;

    // 部品の中のすべてのエンティティ参照 (EntityRef): to[id] があればそれに。無ければそのまま、
    // `clearOthers` なら消す (0)。コピーがコピーを指すようにするために使う。
    void MapEntityRefs(const std::unordered_map<int, int>& to, bool clearOthers);

    // `type` の n 番目の部品 (0 = その型の最初)。
    Component* FindComponent(std::string_view type, size_t nth = 0);
    const Component* FindComponent(std::string_view type, size_t nth = 0) const;
    size_t CountComponents(std::string_view type) const;
    // エンティティの同じ型の部品の中での `c` の番号 (0 = 最初)。
    size_t NthOf(const Component& c) const;
};

// エンティティが木のどこにいるか: 親が子より先、兄弟は順番どおり。
struct TreeEntry {
    const SceneEntity* entity = nullptr;
    int depth = 0;
    bool activeInHierarchy = true;  // そのエンティティと全部の親が有効
};

// システムが、動いている間シーンのために持つもの (物理の世界など)。システム自身の名前で持つ。
// Scene オブジェクトと生き死にをともにする: コピーしない (Play のコピーは何も無い状態で始まる -
// ■ でコピーごと全部捨てる)。保存しない。シーンといっしょに動かす。
class PlayStates {
public:
    PlayStates() = default;
    PlayStates(const PlayStates&) {}
    PlayStates& operator=(const PlayStates&) {
        map_.clear();
        return *this;
    }
    PlayStates(PlayStates&&) noexcept = default;
    PlayStates& operator=(PlayStates&&) noexcept = default;
    // スロット `key` (システムが何か入れるまでは空)。
    std::shared_ptr<void>& Slot(const std::string& key) { return map_[key]; }
    void Clear() { map_.clear(); }

private:
    std::unordered_map<std::string, std::shared_ptr<void>> map_;
};

class Scene {
public:
    static constexpr size_t kEnd = static_cast<size_t>(-1);

    // システムが動いている間このシーンのために持つもの (PlayStates): `T` は最初に使うときに作る。
    template <class T>
    T& PlayState(const std::string& key) {
        std::shared_ptr<void>& slot = playStates_.Slot(key);
        if (!slot) slot = std::make_shared<T>();
        return *static_cast<T*>(slot.get());
    }
    PlayStates& PlayStatesOf() { return playStates_; }

    // 今出ている部品の型 (プラグインから)、と、その中でエンティティが複数持てるもの。読み込みは
    // それ以外を未知として読んで持ち続ける (1 つしか許さない型の 2 つ目もそうなる)。Load の前に
    // 決めておく。
    void SetComponentTypes(std::vector<const reflect::TypeDesc*> types, std::vector<const reflect::TypeDesc*> multiple = {}) {
        componentTypes_ = std::move(types);
        multipleTypes_ = std::move(multiple);
    }
    const std::vector<const reflect::TypeDesc*>& ComponentTypes() const { return componentTypes_; }
    const std::vector<const reflect::TypeDesc*>& MultipleTypes() const { return multipleTypes_; }
    bool AllowsMultiple(std::string_view type) const;

    // シーン自身の値 (SceneSettingInfo: 重力, 空 ...): 出ている型ごとに 1 つ、この順番で。
    // "settings": [{ "type", "data" }] として保存する (部品と同じ)。今は出ていない型は、読んだまま
    // 持つ。型を決めると (Load の前でもあとでも)、まだ持っていないものにそれぞれ既定値を与える。
    void SetSettingTypes(std::vector<const reflect::TypeDesc*> types);
    const std::vector<const reflect::TypeDesc*>& SettingTypes() const { return settingTypes_; }
    const std::vector<Component>& Settings() const { return settings_; }
    Component* FindSetting(std::string_view type);
    const Component* FindSetting(std::string_view type) const;
    // 型 T の設定 (null: その型はこのシーンに出ていない)。
    template <class T>
    T* Setting() {
        Component* c = FindSetting(reflect::TypeOf<T>().name);
        return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
    }
    template <class T>
    const T* Setting() const {
        const Component* c = FindSetting(reflect::TypeOf<T>().name);
        return c && c->Known() ? static_cast<const T*>(c->value.Get()) : nullptr;
    }

    bool Empty() const { return entities_.empty(); }
    const std::vector<SceneEntity>& Entities() const { return entities_; }
    SceneEntity* Find(int id);
    const SceneEntity* Find(int id) const;

    std::vector<TreeEntry> TreeOrder() const;
    std::vector<int> Children(int parent) const;  // 順番どおり
    // `id` が `ancestor` 自身か、その下のどこかのときは true。
    bool IsUnder(int id, int ancestor) const;
    // 指定されたエンティティとその下の全部 (シーンを 1 回通す: エンティティごとに IsUnder を
    // 尋ねる代わりに使う)。
    std::unordered_set<int> Subtree(std::span<const int> roots) const;
    // 指定されたエンティティのうち、親 (または祖父母 ...) も指定されているものを除いたもの。木の順。
    std::vector<int> Roots(std::span<const int> ids) const;

    // `parent` の下 (0 や無い id = 一番上) の子の中の `index` の位置に、新しいエンティティ。
    int Create(std::string name, int parent = 0, size_t index = kEnd);
    // 指定されたエンティティとその下の全部のコピー。それぞれもとのすぐあとに置く。
    // ルートのコピーの id を返す。
    std::vector<int> Duplicate(std::span<const int> ids);
    // 指定されたエンティティとその下の全部を、クリップボード用のテキストにする (JSON。ファイルと
    // 同じエンティティの形): { "kind": "rm_engine.game_production.entities", "format": 1,
    // "entities": [...] }。id はもとのまま。Paste が新しい id を振る。
    json::Value Copy(std::span<const int> ids) const;
    // Copy が書いたもののコピーを、`parent` の下 (0 や無い id = 一番上) の子の中の `index` の位置に、
    // 新しい id で作る。今は出ていない型の部品は、そのまま持つ。ルートのコピーの id を返す
    // (空: `clip` はエンティティではない)。
    std::vector<int> Paste(const json::Value& clip, int parent = 0, size_t index = kEnd, reflect::Warnings* warnings = nullptr);
    static bool IsClip(const json::Value& clip);

    // 指定されたエンティティとその下の全部を消す。消した数を返す。
    size_t Delete(std::span<const int> ids);
    // 指定されたエンティティ (その下の全部も) を `parent` の下の、動かすものを数えない子の中の
    // `index` の位置へ動かす。断られたとき (自分自身の中へ、親が無い) や何も変わらないときは false。
    // 置いた Prefab の部分を別の親へ動かすと、その Prefab を抜ける (普通のエンティティになる -
    // Prefab が自分の親を決める)。まわりの Prefab から外へ動かした入れ子の Prefab は、手で置いた
    // ことになる。(これで、部分が別のファイルの id のまま別の置いた Prefab に入ることは無い。)
    bool Move(std::span<const int> ids, int parent, size_t index);

    // `type` の部品を既定値で、エンティティのほかの部品のあとに足す。false: そんなエンティティが無い、
    // または 1 つ持っていてその型は 1 つしか許さない。
    bool AddComponent(int id, const reflect::TypeDesc& type);
    // `type` の n 番目の部品を消す。エンティティにそんなものが無いときは false (知っている型でも未知でも)。
    bool RemoveComponent(int id, std::string_view type, size_t nth = 0);
    // `type` の n 番目の部品を、エンティティの全部品の中で 1 つ上 (by = -1) か下 (+1) へ動かす
    // (インスペクターの順番)。動かせないとき (エンティティや部品が無い、すでに最初 / 最後) は false。
    bool MoveComponent(int id, std::string_view type, int by, size_t nth = 0);

    // --- Prefab (ファイル側は Prefab.h) --------------------------------------------------------
    // `id` が属する置いた Prefab のルート (自分がルートならそれ自身)。0: 無い。リンクされた
    // エンティティだけをたどって上がる: 間に普通のエンティティがあると、そこで終わる。
    int PrefabRootOf(int id) const;
    // ルートと、置いた Prefab のエンティティ。木の順: 入れ子になった Prefab のルートも入る
    // (それはその Prefab のエンティティだから) が、その中身や、足した普通のエンティティは入らない。
    std::vector<int> PrefabMembers(int root) const;
    // 置いた Prefab `root` の中に入れ子になった Prefab のルート (そのメンバーのうちルートのもの)。
    std::vector<int> NestedPrefabs(int root) const;
    // 置いた Prefab のすべてのルート (Prefab ファイル `asset` のもの - AssetRef の guid。空 = どれでも)。
    // 木の順: 外側が、その中に入れ子になったものより先。
    std::vector<int> PrefabRoots(std::string_view asset = {}) const;
    // `root` とその下の全部が、`sources` にあるもので `asset` の置いた Prefab になる: 各エンティティの
    // source は sources[その id]。その中にある置いた Prefab のルートはそのままルートで、入れ子になる
    // (outer = sources[その id])。`sources` に無いエンティティ (入れ子の Prefab の中) はそのまま。
    bool LinkPrefab(int root, const AssetRef& asset, const std::unordered_map<int, int>& sources);
    // 置いた Prefab のエンティティが、また普通のエンティティになる (中に入れ子になった Prefab は
    // 残り、今は手で置いたことになる)。false: `root` がルートではない。
    bool UnlinkPrefab(int root);
    // `prefab` (Prefab ファイルの内容) のコピーを、`parent` の下の子の中の `index` の位置に、
    // `asset` につないで作る。中に入れ子になった Prefab はルートだけで来る: エディターがそれぞれの
    // ファイルから埋める (NestedPrefabs のそれぞれに ApplyPrefab)。新しいルートの id を返す
    // (0: Prefab ではない、またはエンティティが無い)。
    int PlacePrefab(const json::Value& prefab, const AssetRef& asset, int parent = 0, size_t index = kEnd,
                    reflect::Warnings* warnings = nullptr);
    // 置いた Prefab `root` を、`overrides` (Prefab.h) つきで、もう一度 Prefab に合わせる: エンティティは
    // id を保つ (source で合わせる)。Prefab に新しくあるものは新しい id で来る。Prefab から無くなった
    // ものは消える (部品といっしょに。入れ子の Prefab はその中の全部といっしょに)。その下に足した
    // 普通のエンティティは残る (親が消えたときはルートの下へ)。前からあった入れ子の Prefab は
    // 自分の上書き (ここで変えたもの) を保つ。新しいものはファイルのものを取る。入れ子のものの
    // 中身はそのままにする: エディターが、それぞれのファイルからもう一度作る。false: `root` が
    // ルートではない、または `prefab` にエンティティが無い。
    bool ApplyPrefab(int root, const json::Value& prefab, const json::Value& overrides, reflect::Warnings* warnings = nullptr);

    // id は二度と使わない。Undo で古いシーンが戻ってきてもそう: 次の id (新しいエンティティが
    // もらうもの) は Undo のコマンドが覚えていて、KeepNextId で戻す。
    int NextId() const { return nextId_; }
    void KeepNextId(int atLeast) { nextId_ = std::max(nextId_, atLeast); }
    // これより大きい id は壊れているものとして扱う (読み込み時に置き換える): 新しい id の余地が
    // いつもある。
    static constexpr int kMaxId = 1 << 30;

    // { "nextId": 7, "entities": [ {"id": 1, "parent": 0, "name": ..., "active": ...,
    //     "components": [ {"type": "rm::space3d::Transform", "data": {...}} ] }, ... ] }
    // このバージョンが知らないキー (シーンのも、各エンティティのも) は書き戻す。
    json::Value Save() const;
    // 直せないもの (id が無い・重複、親が無い、親の輪) を、警告を出して直す。
    void Load(const json::Value& v, reflect::Warnings* warnings = nullptr);

    // ファイル (クリップボードや Prefab の中でも) にあるままの 1 つのエンティティと、その逆。
    json::Value SaveEntity(const SceneEntity& e) const;

    // --- 一部のエンティティの状態。Undo 用 (SceneRecord.h) ---------------------------------------
    // 1 つのエンティティのあるがまま: あるかないか、リストの中での場所 (リストの順番が兄弟の順番)、
    // それ以外の全部 (SaveEntity: フィールド、親、部品、Prefab のリンク)。
    struct EntityState {
        int id = 0;
        bool present = false;
        size_t index = 0;
        json::Value data;
        bool operator==(const EntityState&) const = default;
    };
    std::vector<EntityState> CaptureStates(std::span<const int> ids) const;
    // エンティティ `ids` が `states` のとおりになる (それぞれをいったん外し、そこにあるものを
    // 順番に元の場所へ戻す)。ほかのエンティティはすべて、states を取ったときのままでなければ
    // ならない (場所が合うように): SceneRecord は新しいものから順に Undo する。
    void RestoreStates(std::span<const int> ids, std::span<const EntityState> states);
    SceneEntity LoadEntity(const json::Value& item, reflect::Warnings* warnings) const;
    // ファイルにあるままの 1 つの部品 ({"type": ..., "data": {...}}) と、その逆 (未知のものは、
    // 読んだままにする)。`allowed`: エンティティがその型をまだ持っていない、またはその型が複数を許す。
    static json::Value SaveComponent(const Component& c);
    Component LoadComponent(const json::Value& cj, bool allowed, const std::string& entityName, reflect::Warnings* warnings) const;
    // SaveComponent が書いたままの部品 (または設定): その型が `types` のどれか (かつ `allowed`)
    // ならその値、そうでなければ読んだまま (raw)。
    static Component ReadComponent(const json::Value& cj, std::span<const reflect::TypeDesc* const> types, bool allowed,
                                   reflect::Warnings* warnings);
    void AddMissingSettings();

private:
    size_t IndexOf(int id) const;
    // entities_ の中の各 id の場所 (Find は 1 回の引きで、リストを歩かない)。エンティティを足す・消す・
    // 並べ替える何か (Touch) のあと、次に要るときにもう一度作る。引いたものが古いと分かったときも
    // 作り直す (安全網: 頭から信じ込まない)。
    void Touch() { indexStale_ = true; }
    void Reindex() const;
    size_t InsertPosition(int parent, size_t index) const;
    // Copy が書いたままのエンティティを、`parent` の下の `index` の位置に、新しい id で。
    // `idMap`: 古い id -> 新しい id。
    std::vector<int> PasteList(const json::Value& list, int parent, size_t index, reflect::Warnings* warnings,
                               std::unordered_map<int, int>* idMap);
    // 置いた Prefab のエンティティを、ルート無しでコピー (貼る・複製) したものは普通のエンティティ。
    // まわりの Prefab を持たずにコピーした入れ子のルートは、手で置いたことになる。
    void DetachCopies(const std::vector<int>& made);
    // `id` が、その一部である置いた Prefab を抜ける: それとその下の部分は普通のエンティティになり、
    // その中に入れ子になった Prefab は手で置いたことになる (入れ子のルート自身: 手で置いたことになる)。
    void DetachFromPrefab(int id);

    std::vector<SceneEntity> entities_;
    mutable std::unordered_map<int, size_t> index_;
    mutable bool indexStale_ = true;
    std::vector<const reflect::TypeDesc*> componentTypes_;
    std::vector<const reflect::TypeDesc*> multipleTypes_;
    std::vector<const reflect::TypeDesc*> settingTypes_;
    std::vector<Component> settings_;
    int nextId_ = 1;
    json::Value extra_ = json::Value::MakeObject();  // 読み込んだままのシーンオブジェクト (知らないキーも残る)
    PlayStates playStates_;
};

// シーンのエンティティの 1 つのフィールド: エンティティ自身のもの (部品 "") か、その部品の 1 つ
// (その型と、同じ型の中での場所)。その中のパスで指す。Play の この値を残す が、遊んでいる
// コピーから読み、Play の前のシーンへ書き込むもの。
struct SceneField {
    int entity = 0;
    std::string component;
    size_t nth = 0;
    reflect::FieldPath path;
};
// 保存されるときの値。nullopt: エンティティ、部品、フィールドのどれかが無い。
std::optional<json::Value> ReadSceneField(Scene& scene, const SceneField& f);
// 値を入れる (読み込みと同じように: 範囲、未知の名前は残す)。無ければ false。
bool WriteSceneField(Scene& scene, const SceneField& f, const json::Value& value, reflect::Warnings* warnings = nullptr);

}  // namespace rm::game_production
