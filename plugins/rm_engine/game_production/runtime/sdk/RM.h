// RM.h - ゲームの動きを C++ で書く (RM Engine のスクリプト)。
//
// ■ スクリプト
//
//   // Assets/Scripts/Mover.h
//   #pragma once
//   #include "RM.h"
//
//   RM_SCRIPT(Label="動く")
//   struct Mover {
//       RM_PROPERTY(Edit, Label="速さ", Min=0) float speed = 3.0f;   // インスペクターに出て、保存される
//       RM_TRANSIENT()                          float time = 0;       // 出さず、保存もしない
//
//       void BeginPlay(rm::Entity self);           // ゲームが始まるとき (またはエンティティができたとき)
//       void Tick(rm::Entity self, float dt);      // 毎フレーム。dt = 前のフレームからの秒数
//       void EndPlay(rm::Entity self);             // ゲームが止まるとき (または壊した / 外したとき)
//       void OnReloaded(rm::Entity self);          // Play 中にスクリプトを作り直した
//       // 物理 (RMPhysics.h: エンティティの Collider) と一緒に - フレームの固定の刻みの後:
//       void OnHit(rm::Entity self, rm::Entity other);           // `other` に触れ始めた (離れるまで 1 回)
//       void OnTriggerEnter(rm::Entity self, rm::Entity other);  // `other` がトリガーに入った (またはこちらが入った)
//       void OnTriggerExit(rm::Entity self, rm::Entity other);   // ... そして出た
//       // ゲーム UI (RMUI.h: エンティティの ボタン) と一緒に - フレームのシステムの後:
//       void OnClick(rm::Entity self);                           // ボタンが押された (マウス / 決定)
//       // rm::Events (下) - フレームの Tick の後:
//       void OnEvent(rm::Entity self, const rm::Event& e);       // 送られたイベント (またはすべてのエンティティへ)
//   };
//
// ■ ほかにあるもの (それぞれ、さらに下の書いてある所で説明する)
//   rm::Find / FindWithTag / FindAllWithTag、Entity::Tag / Layer / SetParent / Children / FindChild
//   rm::Input (アクション。プレイヤー 0..3 ごと。プレイヤー自身の割り当て)、rm::Mouse、rm::Key、rm::Pad
//   rm::DeltaTime / Time / TimeScale / SetTimeScale / UnscaledDeltaTime / FrameCount
//   rm::Task + rm::StartCoroutine (co_await rm::WaitSeconds ...)、rm::After / rm::Every (タイマー)
//   rm::Events::Send / SendTo / SendDown、rm::Game (シーン、Quit)、Global:: (グローバル変数)、rm::Save
//   RMMath.h (ここに含まれる): Vec2 / Vec3 の + - *、Length、Normalize、Lerp、rm::Math、rm::Quat、rm::Random
//
//   // Assets/Scripts/Mover.cpp
//   #include "Mover.h"
//   #include "RM3D.h"                              // 3D の Transform (RM2D.h: 2D)
//   void Mover::Tick(rm::Entity self, float dt) {
//       rm::Vec3 p = rm::Transform3D(self).Position();
//       p.x += rm::Input::Axis("MoveX") * speed * dt;   // ゲーム設定の 入力 のアクション
//       rm::Transform3D(self).SetPosition(p);
//   }
//
// ■ ルール
//   * スクリプトはただの構造体 (コンストラクターなし、基底クラスなし、private なし)。メンバーは
//     すべて RM_PROPERTY(...) か RM_TRANSIENT() にする。欄の型: bool、int、float、std::string、rm::Vec2、
//     rm::Vec3、rm::Color、rm::EntityRef、rm::PrefabRef / rm::ImageRef / rm::ModelRef / rm::SoundRef (アセット)、
//     RM_ENUM の enum class、RM_STRUCT の構造体 (そのメンバーも RM_PROPERTY)、
//     そしてそれらの std::vector (vector の vector は不可)。
//
//       RM_ENUM() enum class Mode { RM_ITEM(Label="歩く") Walk, Run };
//       RM_STRUCT() struct Wave { RM_PROPERTY(Edit) int count = 3; RM_PROPERTY(Edit) float gap = 1; };
//       RM_SCRIPT() struct Spawner {
//           RM_PROPERTY(Edit) rm::PrefabRef enemy;            // インスペクターで .rmprefab を選ぶ
//           RM_PROPERTY(Edit) std::vector<Wave> waves;
//           RM_PROPERTY(Edit) Mode mode = Mode::Walk;
//       };
//   * RM_PROPERTY: Edit (表示する)、ReadOnly、Label="..."、Tooltip="..."、Min=、Max=、Step=。
//   * フックは省略できる: 要るものだけ書く。状態を `static` 変数に持たないこと。スクリプトを
//     作り直すと捨てられる (欄は名前で値を引き継ぐ)。
//   * 複数のスクリプトは型ごとに動く: 型の順はゲーム設定の スクリプトの実行順 (小さいほうが先。
//     載っていない型は 0、そのあと名前順)。同じ型の中は、エンティティごと、付けた順。フレーム中に
//     作られたものは、最初の Tick の前に BeginPlay を受ける。
//   * 落ちたスクリプト (null ポインターなど) は、それだけが止まり、コンソールに知らせる。
//     エディターとほかのスクリプトは動き続ける。
//   * 作る・壊す: rm::Spawn("Bullet") / rm::Spawn(enemyPrefab) はエンティティをすぐ作る (すぐ
//     設定すること)。そのスクリプトは次のフレームで始まる。self.Destroy() と Remove<T>() は
//     フレームの終わりに効く (先に EndPlay。子も一緒に消える)。壊されたものの Entity は
//     もう Valid() ではない。それに対する呼び出しは何もしない。
//   * 自分のヘッダーには、生成されたものを何も include しない: rmgen がそれを読んで、登録を書く。
//
// エンジンが渡すヘッダーはこれだけ: RM.h (これ)、RmAbi.h (境界。C)、プラグインの部品
// (RM3D.h、RM2D.h)。Visual Studio で名前にカーソルを乗せると、何をするかが読める。
#pragma once

#include "RmAbi.h"

#include <coroutine>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>  // (欄は std::vector でもよい)

// --- アノテーション (rmgen が読む。展開すると何も残らない) ---------------------------------
#ifndef RM_SCRIPT
#define RM_SCRIPT(...)
#endif
#ifndef RM_PROPERTY
#define RM_PROPERTY(...)
#endif
#ifndef RM_TRANSIENT
#define RM_TRANSIENT(...)
#endif
#ifndef RM_STRUCT
#define RM_STRUCT(...)
#endif
#ifndef RM_ENUM
#define RM_ENUM(...)
#endif
#ifndef RM_ITEM
#define RM_ITEM(...)
#endif

namespace rm {

// エンジンの表 (DLL を読み込んだときに設定する。rmgen が登録に書く)。
const RmApi* Api();

struct Vec2 {
    float x = 0, y = 0;
    bool operator==(const Vec2&) const = default;
};
struct Vec3 {
    float x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    bool operator==(const Color&) const = default;
};

}  // namespace rm

#include "RMMath.h"  // Vec2 / Vec3 の + - *、Length、Normalize、Lerp、rm::Math、rm::Quat、rm::Random

namespace rm {
// インスペクターで選んだエンティティ (階層からドラッグする)。使うときは Entity(ref)。
struct EntityRef {
    uint64_t id = 0;
};

// インスペクターで選んだアセット (アセット パネルからドラッグする)。id で持つので、ファイルを
// 動かしても名前を変えても保たれる。ただのバイト列 (エンジンが読み書きする): 自由にコピーしてよい。
struct PrefabRef {  // .rmprefab: rm::Spawn(ref)
    char guid[RM_ASSET_ID_SIZE] = {};
    bool Empty() const { return guid[0] == 0; }
};
struct ImageRef {  // 画像
    char guid[RM_ASSET_ID_SIZE] = {};
    bool Empty() const { return guid[0] == 0; }
};
struct ModelRef {  // 3D のモデル
    char guid[RM_ASSET_ID_SIZE] = {};
    bool Empty() const { return guid[0] == 0; }
};
struct SoundRef {  // 音 (WAV / OGG ...): rm::Audio::Play(ref) - RMAudio.h
    char guid[RM_ASSET_ID_SIZE] = {};
    bool Empty() const { return guid[0] == 0; }
};

struct Entity;
// API の部品を名前で (下で定義する)。
template <class T>
const T* Part(const char* name, uint32_t version);
namespace detail {
// エンジンの部品のハンドル (RM3D.h の Transform3D ...): その型の名前を持ち、Entity から作る。
template <class T>
concept EngineComponent = requires { T::kComponentType; };
template <class T>
std::string TypeName();
template <class T>
const char* ComponentName() {
    if constexpr (EngineComponent<T>) {
        return T::kComponentType;
    } else {
        static const std::string name = TypeName<T>();
        return name.c_str();
    }
}
template <class T>
std::string TypeName() {
    // T の C++ の名前。エンジンが保存するもの ("Mover"、"game::Mover")。
    std::string_view s = __FUNCSIG__;
    const size_t b = s.find("TypeName<") + 9;
    const size_t e = s.rfind(">(void)");
    std::string t(s.substr(b, e - b));
    for (std::string_view p : {"struct ", "class "}) {
        if (t.rfind(p, 0) == 0) t.erase(0, p.size());
    }
    return t;
}
}  // namespace detail

// シーンのエンティティ (その id で)。コピーが軽い。見つけたものを使う前に Valid() を確かめる。
struct Entity {
    RmEntityId id = 0;

    Entity() = default;
    explicit Entity(RmEntityId i) : id(i) {}
    Entity(EntityRef r) : id(r.id) {}

    // そのエンティティがある (消されていない)。
    bool Valid() const { return id != 0 && Api()->core->exists(Api()->ctx, id) != 0; }
    // 有効: そのエンティティも親もすべてオンになっている。
    bool Active() const { return Api()->core->isActive(Api()->ctx, id) != 0; }
    void SetActive(bool on) const { Api()->core->setActive(Api()->ctx, id, on ? 1 : 0); }
    std::string Name() const {
        char buf[256];
        const uint32_t n = Api()->core->getName(Api()->ctx, id, buf, sizeof(buf));
        if (n < sizeof(buf)) return std::string(buf, n);
        std::string s(n + 1, '\0');
        Api()->core->getName(Api()->ctx, id, s.data(), n + 1);
        s.resize(n);
        return s;
    }
    Entity Parent() const { return Entity(Api()->core->parent(Api()->ctx, id)); }
    void SetName(std::string_view name) const;

    // 親が変わる: `parent` の下 (無効な Entity: 一番上)、子の最後に。keepWorld: ワールドでの場所は
    // そのまま (新しい親の分を補うように Transform を変える)。false: Transform はそのまま (新しい親と
    // 一緒に動く)。False が返ったら断られた (自分自身の下には入れない)。
    bool SetParent(Entity parent, bool keepWorld = true) const;
    // その子。階層の順 (無効な Entity: 一番上のエンティティ)。
    std::vector<Entity> Children() const;
    // この名前の最初の子 (または "Arm/Hand" のように、もっと深く)。無ければ無効。
    Entity FindChild(std::string_view path) const;

    // タグとレイヤー (名前。ゲーム設定の タグ / レイヤー に並ぶ)。"" = なし / 既定のレイヤー。
    std::string Tag() const;
    void SetTag(std::string_view tag) const;
    bool CompareTag(std::string_view tag) const { return Tag() == tag; }
    std::string Layer() const;
    void SetLayer(std::string_view layer) const;

    // このフレームの終わりに、その下のものと一緒に消える (先に EndPlay)。
    void Destroy() const { Api()->core->destroy(Api()->ctx, id); }

    // 部品。T がスクリプト (Add / Get は T* を返す。無ければ null) か、RM3D.h / RM2D.h にあるエンジンの
    // 部品 (rm::Transform3D、rm::Transform2D: それ自体を返す。あれば Valid())。
    //   auto* hp = self.Add<Health>();          hp->max = 10;     // スクリプト: BeginPlay は次のフレーム
    //   self.Add<rm::Transform3D>().SetPosition({0, 1, 0});
    //   if (auto* h = other.Get<Health>()) h->hp -= 1;
    //   self.Remove<Health>();                                    // フレームの終わりに (EndPlay)
    template <class T>
    auto Add() const {
        void* p = Api()->core->addComponent(Api()->ctx, id, detail::ComponentName<T>());
        if constexpr (detail::EngineComponent<T>) {
            (void)p;
            return T(*this);
        } else {
            return static_cast<T*>(p);
        }
    }
    template <class T>
    auto Get(uint32_t nth = 0) const {
        if constexpr (detail::EngineComponent<T>) {
            return T(*this);
        } else {
            return static_cast<T*>(Api()->core->getScript(Api()->ctx, id, detail::ComponentName<T>(), nth));
        }
    }
    template <class T>
    bool Remove(uint32_t nth = 0) const {
        return Api()->core->removeComponent(Api()->ctx, id, detail::ComponentName<T>(), nth) != 0;
    }
    template <class T>
    uint32_t Count() const {
        return Api()->core->countComponents(Api()->ctx, id, detail::ComponentName<T>());
    }
    // (スクリプト用の Get の古い名前)
    template <class T>
    T* GetScript(uint32_t nth = 0) const {
        return static_cast<T*>(Api()->core->getScript(Api()->ctx, id, detail::ComponentName<T>(), nth));
    }
    bool operator==(const Entity&) const = default;
};

// `name` という名前の新しいエンティティ (`parent` の下。none: 一番上)。空なので、要るものを足す。
// すぐに存在する。そのスクリプトは次のフレームで始まる。
inline Entity Spawn(std::string_view name, Entity parent = Entity()) {
    const std::string n(name);
    return Entity(Api()->core->spawn(Api()->ctx, n.c_str(), parent.id));
}
// Prefab のコピー (その中に入れ子になったものも)。欄が空か、ファイルが無ければ無効。
inline Entity Spawn(const PrefabRef& prefab, Entity parent = Entity()) {
    if (prefab.Empty()) return Entity();
    return Entity(Api()->core->spawnPrefab(Api()->ctx, prefab.guid, parent.id));
}

// この名前の最初のエンティティ (階層の順)。無ければ無効な Entity。
inline Entity Find(std::string_view name) {
    const std::string n(name);
    return Entity(Api()->core->findByName(Api()->ctx, n.c_str()));
}

// エディターのコンソール (とゲームのログ) へのメッセージ。
namespace Log {
inline void Info(std::string_view text) {
    const std::string t(text);
    Api()->core->log(Api()->ctx, 0, t.c_str());
}
inline void Warn(std::string_view text) {
    const std::string t(text);
    Api()->core->log(Api()->ctx, 1, t.c_str());
}
inline void Error(std::string_view text) {
    const std::string t(text);
    Api()->core->log(Api()->ctx, 2, t.c_str());
}
}  // namespace Log

// このフレームの秒数 (Tick の dt と同じ) と、ゲームが始まってからの秒数。
inline float DeltaTime() { return Api()->core->deltaTime(Api()->ctx); }
inline double Time() { return Api()->core->time(Api()->ctx); }

// キー (Windows の仮想キーコード: どの VK_ の値でも使える)。
namespace Key {
enum : int {
    Backspace = 0x08, Tab = 0x09, Enter = 0x0D, Shift = 0x10, Ctrl = 0x11, Alt = 0x12, Escape = 0x1B, Space = 0x20,
    Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28,
    D0 = '0', D1, D2, D3, D4, D5, D6, D7, D8, D9,
    A = 'A', B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    MouseLeft = 0x01, MouseRight = 0x02, MouseMiddle = 0x04, MouseBack = 0x05, MouseForward = 0x06,
};
}  // namespace Key
// ゲームパッドの操作 (ゲーム設定の 入力 が呼ぶ名前で: Input::SetBinding 用)。
namespace Pad {
enum : int {
    A = 0x100, B, X, Y, LB, RB, Back, Start, LeftThumb, RightThumb, Up, Down, Left, Right, LT, RT,
    LeftStickX, LeftStickY, RightStickX, RightStickY,
};
}  // namespace Pad

// API のうち、プラグインの部品 (RM3D.h が使う)。このエンジンにそのプラグインが無ければ null。
template <class T>
const T* Part(const char* name, uint32_t version) {
    return static_cast<const T*>(Api()->part(Api()->ctx, name, version));
}

// 入力。アクション名で読む: ゲーム設定の 入力 にアクション ("MoveX"、"Jump" ...) と、それぞれを
// 動かすもの (キー、マウスのボタン、ゲームパッド) が並ぶ。だからスクリプトを変えなくてもパッドが
// 使え、ビルドし直さなくてもキーを変えられる。一覧に無い名前は false / 0 (コンソールに
// 1 回だけ知らせる)。
namespace Input {
namespace detail {
inline const RmInputApi* Table() { return Part<RmInputApi>(RM_PART_INPUT, RM_PART_INPUT_VERSION); }
}  // namespace detail
// 今押されている (キー / ボタンのどれか。スティックは半分より倒したとき)。
inline bool Down(const char* action) {
    const RmInputApi* t = detail::Table();
    return t && t->down(Api()->ctx, action) != 0;
}
// このフレームで押された (押すごとに 1 回)。
inline bool Pressed(const char* action) {
    const RmInputApi* t = detail::Table();
    return t && t->pressed(Api()->ctx, action) != 0;
}
// このフレームで離された。
inline bool Released(const char* action) {
    const RmInputApi* t = detail::Table();
    return t && t->released(Api()->ctx, action) != 0;
}
// -1..1: キー / ボタンの 向き (例: ← -1、→ 1) を掛けて足したものと、スティックは倒した分。
inline float Axis(const char* action) {
    const RmInputApi* t = detail::Table();
    return t ? t->axis(Api()->ctx, action) : 0.0f;
}

// キーそのもの (rm::Key::Space、または VK_ の値)。上のアクション名のほうがよい。これらは
// ゲームパッドを見ない。
inline bool Down(int key) { return Api()->core->keyDown(Api()->ctx, key) != 0; }
inline bool Pressed(int key) { return Api()->core->keyPressed(Api()->ctx, key) != 0; }
inline bool Released(int key) { return Api()->core->keyReleased(Api()->ctx, key) != 0; }

// --- プレイヤー (4 人まで): 同じアクション。1 人のプレイヤーだけの ---------------------------
// player 0..3: そのスロットのゲームパッド (Windows はつないだ順にパッドへ番号を付ける)。
// ゲーム設定の キーボードとマウスのプレイヤー がその人なら、キーボードとマウスも (1P: 0)。
//   float x = rm::Input::Axis("MoveX", 1);   // 2P
namespace detail {
inline const RmInputApi2* Table2() { return Part<RmInputApi2>(RM_PART_INPUT, RM_PART_INPUT_VERSION2); }
}  // namespace detail
inline bool Down(const char* action, int player) {
    const RmInputApi2* t = detail::Table2();
    return t && t->down(Api()->ctx, action, player) != 0;
}
inline bool Pressed(const char* action, int player) {
    const RmInputApi2* t = detail::Table2();
    return t && t->pressed(Api()->ctx, action, player) != 0;
}
inline bool Released(const char* action, int player) {
    const RmInputApi2* t = detail::Table2();
    return t && t->released(Api()->ctx, action, player) != 0;
}
inline float Axis(const char* action, int player) {
    const RmInputApi2* t = detail::Table2();
    return t ? t->axis(Api()->ctx, action, player) : 0.0f;
}
// そのスロットにゲームパッドがある。
inline bool PadConnected(int player) {
    const RmInputApi2* t = detail::Table2();
    return t && t->padConnected(Api()->ctx, player) != 0;
}
// プレイヤーのパッドを `seconds` 秒震わせる (low: 左の重いモーター、high: 右の軽いモーター。0..1)。
// ゲームが止まる (または ■) と止まる。
inline void Vibrate(int player, float low, float high, float seconds) {
    if (const RmInputApi2* t = detail::Table2()) t->vibrate(Api()->ctx, player, low, high, seconds);
}

// --- プレイヤーが変えられるもの: 割り当て (キーコンフィグ) -----------------------------------
// ここで変えるまでは、ゲーム設定の 入力。SaveBindings でプレイヤーの PC に残り、ゲームは次の起動で
// 自分で読み戻す。操作は、キー (rm::Key::Space、VK_ の値)、マウスのボタン (rm::Key::MouseLeft)、
// パッドのもの (rm::Pad::A)。scale: 設定の 向き と同じ (-1 / 1)。
//   // 「Jump のキーを押してください」: 1 つ待ち、最初の割り当ての代わりに入れる
//   if (int c = rm::Input::AnyPressed(); c != 0) { rm::Input::SetBinding("Jump", 0, c); rm::Input::SaveBindings(); }
struct Binding {
    int control = 0;
    float scale = 1;
};
inline std::vector<Binding> Bindings(const char* action) {
    std::vector<Binding> out;
    const RmInputApi2* t = detail::Table2();
    if (!t) return out;
    const uint32_t n = t->bindingCount(Api()->ctx, action);
    for (uint32_t i = 0; i < n; ++i) {
        int32_t c = 0;
        float s = 1;
        if (t->binding(Api()->ctx, action, i, &c, &s)) out.push_back({c, s});
    }
    return out;
}
// `index` の割り当てを置き換える (index == 個数: 1 つ足す)。False: そのアクション / index が無い。
inline bool SetBinding(const char* action, int index, int control, float scale = 1) {
    const RmInputApi2* t = detail::Table2();
    return t && index >= 0 && t->setBinding(Api()->ctx, action, static_cast<uint32_t>(index), control, scale) != 0;
}
inline bool AddBinding(const char* action, int control, float scale = 1) {
    const RmInputApi2* t = detail::Table2();
    return t && t->setBinding(Api()->ctx, action, t->bindingCount(Api()->ctx, action), control, scale) != 0;
}
inline bool RemoveBinding(const char* action, int index) {
    const RmInputApi2* t = detail::Table2();
    return t && index >= 0 && t->removeBinding(Api()->ctx, action, static_cast<uint32_t>(index)) != 0;
}
// ゲーム設定のものに戻す (SaveBindings するまで保存しない)。
inline void ResetBindings() {
    if (const RmInputApi2* t = detail::Table2()) t->resetBindings(Api()->ctx);
}
inline bool SaveBindings() {
    const RmInputApi2* t = detail::Table2();
    return t && t->saveBindings(Api()->ctx) != 0;
}
inline bool LoadBindings() {
    const RmInputApi2* t = detail::Table2();
    return t && t->loadBindings(Api()->ctx) != 0;
}
// このフレームで押されたもの (キー、マウスのボタン、パッドのボタン、半分より倒したスティックや
// トリガー)。0: なし。*direction: 1、スティックを左 / 下に倒したときは -1。
inline int AnyPressed(int player = -1, float* direction = nullptr) {
    const RmInputApi2* t = detail::Table2();
    float d = 1;
    const int c = t ? t->anyPressed(Api()->ctx, player, &d) : 0;
    if (direction) *direction = d;
    return c;
}
// 操作の名前 ("Space"、"A"、"PadA"、"MouseLeft" ...): 何が割り当ててあるかを見せるため。
inline std::string ControlName(int control) {
    const RmInputApi2* t = detail::Table2();
    if (!t) return {};
    char buf[64];
    const uint32_t n = t->controlName(Api()->ctx, control, buf, sizeof(buf));
    return std::string(buf, n < sizeof(buf) ? n : sizeof(buf) - 1);
}
}  // namespace Input

// ゲーム画面上のマウス: ゲーム設定の 画面 (DIP、左上が (0, 0)、y は下向き)。
//   if (rm::Mouse::Pressed()) { rm::Vec2 p = rm::Mouse::Position(); ... }
namespace Mouse {
namespace detail {
inline const RmMouseApi* Table() { return Part<RmMouseApi>(RM_PART_MOUSE, RM_PART_MOUSE_VERSION); }
}  // namespace detail
// マウスがゲームの上にある (エディターのゲームビュー、ゲームのウィンドウ)。
inline bool OverGame() {
    float p[2];
    const RmMouseApi* t = detail::Table();
    return t && t->position(Api()->ctx, p) != 0;
}
// その位置 (ゲームの上にないときは、最後にあった位置)。
inline Vec2 Position() {
    float p[2] = {0, 0};
    if (const RmMouseApi* t = detail::Table()) t->position(Api()->ctx, p);
    return {p[0], p[1]};
}
// 前のフレームから動いた。
inline Vec2 Delta() {
    float p[2] = {0, 0};
    if (const RmMouseApi* t = detail::Table()) t->delta(Api()->ctx, p);
    return {p[0], p[1]};
}
// このフレームのホイールの目盛り (+: 自分から向こうへ回した)。
inline float Wheel() {
    const RmMouseApi* t = detail::Table();
    return t ? t->wheel(Api()->ctx) : 0.0f;
}
inline Vec2 ScreenSize() {
    float p[2] = {0, 0};
    if (const RmMouseApi* t = detail::Table()) t->screenSize(Api()->ctx, p);
    return {p[0], p[1]};
}
// 0 から 1 での位置 ((0, 0) が左上、(1, 1) が右下)。
inline Vec2 ViewportPosition() {
    const Vec2 s = ScreenSize(), p = Position();
    return {s.x > 0 ? p.x / s.x : 0.0f, s.y > 0 ? p.y / s.y : 0.0f};
}
// ボタン: 0 左、1 右、2 真ん中 (ゲームの上にあるときだけ)。
inline int ButtonKey(int button) { return button == 1 ? Key::MouseRight : button == 2 ? Key::MouseMiddle : Key::MouseLeft; }
inline bool Down(int button = 0) { return Input::Down(ButtonKey(button)); }
inline bool Pressed(int button = 0) { return Input::Pressed(ButtonKey(button)); }
inline bool Released(int button = 0) { return Input::Released(ButtonKey(button)); }
}  // namespace Mouse

// --- 時間 ---------------------------------------------------------------------------------------
// 時間の倍率: 1 ふつう、0.5 スロー、0 停止 (ポーズ メニュー)。ゲームのすべての dt にこれを掛ける
// (Tick、システム、物理、パーティクル)。0 でも Tick は呼ばれる。
inline float TimeScale() {
    const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION);
    return t ? t->scale(Api()->ctx) : 1.0f;
}
inline void SetTimeScale(float scale) {
    if (const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION)) t->setScale(Api()->ctx, scale);
}
// このフレームの実際の秒数と、ゲームが始まってからの秒数 (時間の倍率は数えない)。
inline float UnscaledDeltaTime() {
    const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION);
    return t ? t->unscaledDeltaTime(Api()->ctx) : 0.0f;
}
inline double UnscaledTime() {
    const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION);
    return t ? t->unscaledTime(Api()->ctx) : 0.0;
}
// これより前に Play したフレーム数 (最初のフレームは 0)。
inline uint64_t FrameCount() {
    const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION);
    return t ? t->frameCount(Api()->ctx) : 0;
}

// --- コルーチンとタイマー -------------------------------------------------------------------
// 途中で待つ関数 (C++20 の co_await): rm::Task を返す関数として書く:
//
//   rm::Task Blink(rm::Entity self) {
//       for (int i = 0; i < 5; ++i) {
//           self.SetActive(false);
//           co_await rm::WaitSeconds(0.2f);
//           self.SetActive(true);
//           co_await rm::WaitSeconds(0.2f);
//       }
//   }
//   void Player::OnHit(rm::Entity self, rm::Entity) { rm::StartCoroutine(Blink(self)); }
//
// 最初の co_await までは今すぐ動き、そのあと待ちが終わったら、このフレームの Tick の後で続きが動く。
// これを始めたスクリプトのものになる: そのスクリプトの EndPlay (エンティティを壊した、切った、
// シーンを切り替えた、■) で止まる。だからメンバー関数の `this` は、動いている間ずっと有効だ。
// Play 中にスクリプトを作り直すと、すべてのコルーチンが止まる (OnReloaded でまた始めてもよい)。
// 待ち: WaitSeconds (ゲーム内の時間)、WaitRealSeconds (実際の時間: 時間の倍率 0 のポーズ メニュー用)、
// NextFrame / WaitFrames、WaitUntil / WaitWhile (条件を 1 フレームに 1 回調べる)、そして
// Coroutine (それが終わるまで)。タイマー: rm::After(2.0f, [] { ... }) と rm::Every(1.0f, [] { ... })。
class Task;
namespace detail {
inline const RmCoroutinesApi* Co() { return Part<RmCoroutinesApi>(RM_PART_COROUTINES, RM_PART_COROUTINES_VERSION); }
}  // namespace detail

// 始めたコルーチン (その id で): Running()、Stop()。co_await すると、終わるまで待つ。
struct Coroutine {
    uint64_t id = 0;
    bool Running() const {
        const RmCoroutinesApi* c = detail::Co();
        return id != 0 && c && c->running(Api()->ctx, id) != 0;
    }
    void Stop() const {
        if (const RmCoroutinesApi* c = detail::Co(); c && id != 0) c->stop(Api()->ctx, id);
    }
    bool await_ready() const noexcept { return !Running(); }
    template <class H>
    void await_suspend(H h) const noexcept {
        h.promise().wait = RmWait{RmWait_Coroutine, 0, 0.0, id};
    }
    void await_resume() const noexcept {}
};

class Task {
public:
    struct promise_type {
        RmWait wait{RmWait_Frames, 1, 0.0, 0};
        std::function<bool()> until;  // WaitUntil: 続きを動かす前に 1 フレームに 1 回調べる
        Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }  // (StartCoroutine が動かす)
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { throw; }  // (エンジンが止め、コンソールに知らせる)
    };
    using Handle = std::coroutine_handle<promise_type>;
    explicit Task(Handle h) : h_(h) {}
    Task(Task&& o) noexcept : h_(o.h_) { o.h_ = {}; }
    Task& operator=(Task&& o) noexcept {
        if (this != &o) {
            if (h_) h_.destroy();
            h_ = o.h_;
            o.h_ = {};
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() {
        if (h_) h_.destroy();  // (一度も始めていない)
    }
    Handle Release() {
        Handle h = h_;
        h_ = {};
        return h;
    }

    // エンジンはこれらを通してコルーチンを動かす (RmCoroutinesApi::start)。
    static int Resume(void* frame, RmWait* next) {
        Handle h = Handle::from_address(frame);
        promise_type& p = h.promise();
        if (p.until) {
            if (!p.until()) {
                *next = RmWait{RmWait_Frames, 1, 0.0, 0};
                return 1;
            }
            p.until = nullptr;
        }
        p.wait = RmWait{RmWait_Frames, 1, 0.0, 0};
        h.resume();
        if (h.done()) return 0;
        *next = p.wait;
        return 1;
    }
    static void Destroy(void* frame) { Handle::from_address(frame).destroy(); }

private:
    Handle h_;
};

namespace detail {
struct WaitFor {
    RmWait wait;
    bool await_ready() const noexcept { return false; }
    void await_suspend(Task::Handle h) const noexcept { h.promise().wait = wait; }
    void await_resume() const noexcept {}
};
struct WaitCondition {
    std::function<bool()> ready;
    bool await_ready() const { return ready(); }
    void await_suspend(Task::Handle h) {
        h.promise().wait = RmWait{RmWait_Frames, 1, 0.0, 0};
        h.promise().until = std::move(ready);
    }
    void await_resume() const noexcept {}
};
}  // namespace detail

// rm::Task の中で、これらのどれかを co_await する。
inline detail::WaitFor WaitSeconds(float seconds) { return {RmWait{RmWait_Seconds, 0, static_cast<double>(seconds), 0}}; }
inline detail::WaitFor WaitRealSeconds(float seconds) { return {RmWait{RmWait_RealSeconds, 0, static_cast<double>(seconds), 0}}; }
inline detail::WaitFor NextFrame() { return {RmWait{RmWait_Frames, 1, 0.0, 0}}; }
inline detail::WaitFor WaitFrames(int frames) { return {RmWait{RmWait_Frames, frames > 1 ? static_cast<uint32_t>(frames) : 1u, 0.0, 0}}; }
template <class F>
detail::WaitCondition WaitUntil(F condition) {
    return {std::function<bool()>(std::move(condition))};
}
template <class F>
detail::WaitCondition WaitWhile(F condition) {
    return {std::function<bool()>([c = std::move(condition)]() mutable { return !c(); })};
}

// 始める (最初の待ちまで今すぐ動く)。返った Coroutine で止めたり、待ったりする。
inline Coroutine StartCoroutine(Task task) {
    Task::Handle h = task.Release();
    if (!h) return {};
    const RmCoroutinesApi* c = detail::Co();
    if (!c) {
        h.destroy();
        return {};
    }
    return {c->start(Api()->ctx, h.address(), &Task::Resume, &Task::Destroy)};
}
// 今動いているスクリプトのコルーチンすべて。
inline void StopAllCoroutines() {
    if (const RmCoroutinesApi* c = detail::Co()) c->stopAll(Api()->ctx);
}

namespace detail {
template <class F>
Task AfterTask(float seconds, bool real, F f) {
    if (real) {
        co_await WaitRealSeconds(seconds);
    } else {
        co_await WaitSeconds(seconds);
    }
    f();
}
template <class F>
Task EveryTask(float seconds, int times, bool real, F f) {
    for (int i = 0; times < 0 || i < times; ++i) {
        if (real) {
            co_await WaitRealSeconds(seconds);
        } else {
            co_await WaitSeconds(seconds);
        }
        f();
    }
}
}  // namespace detail
// タイマー: `seconds` 秒のゲーム内の時間の後に `f` (realTime: 実際の秒数)、または `seconds` ごとに
// (`times` 回。-1: 止めるまで)。始めたスクリプトのものになる。コルーチンと同じ。
//   rm::After(3.0f, [self] { self.Destroy(); });
//   rm::Coroutine spawning = rm::Every(0.5f, [this] { Spawn(); });   ... spawning.Stop();
template <class F>
Coroutine After(float seconds, F f, bool realTime = false) {
    return StartCoroutine(detail::AfterTask(seconds, realTime, std::move(f)));
}
template <class F>
Coroutine Every(float seconds, F f, int times = -1, bool realTime = false) {
    return StartCoroutine(detail::EveryTask(seconds, times, realTime, std::move(f)));
}

// --- イベント -------------------------------------------------------------------------------------
// それを欲しがるスクリプトへ、名前で送るメッセージ。次のようなスクリプトが
//     void OnEvent(rm::Entity self, const rm::Event& e) { if (e.Is("Damage")) hp -= e.number; }
// このフレームの Tick の後に (スクリプトの順で) 受け取る。送り手は、誰が聞いているかを知らない。
//   rm::Events::Send("GameOver");                          // すべてのエンティティへ
//   rm::Events::SendTo(enemy, "Damage", 3);                // そのエンティティのスクリプトだけへ
//   rm::Events::SendDown(ui, "Show");                      // そのエンティティと、その下のすべてへ
struct Event {
    std::string_view name;  // (OnEvent の間だけ有効: 残したいものはコピーする)
    Entity sender;          // どのスクリプトが送ったか (無効: エンジン)
    double number = 0;
    std::string_view text;
    Entity entity;          // 運んでいるエンティティ (無効: なし)
    bool Is(std::string_view n) const { return name == n; }
};
namespace Events {
namespace detail {
inline void Send(uint32_t scope, Entity target, std::string_view name, double number, std::string_view text, Entity entity) {
    const RmEventsApi* t = Part<RmEventsApi>(RM_PART_EVENTS, RM_PART_EVENTS_VERSION);
    if (!t) return;
    const std::string n(name);
    t->send(Api()->ctx, scope, target.id, n.c_str(), number, text.data(), text.size(), entity.id);
}
}  // namespace detail
inline void Send(std::string_view name, double number = 0, std::string_view text = {}, Entity entity = Entity()) {
    detail::Send(0, Entity(), name, number, text, entity);
}
inline void SendTo(Entity target, std::string_view name, double number = 0, std::string_view text = {}, Entity entity = Entity()) {
    detail::Send(1, target, name, number, text, entity);
}
inline void SendDown(Entity target, std::string_view name, double number = 0, std::string_view text = {}, Entity entity = Entity()) {
    detail::Send(2, target, name, number, text, entity);
}
}  // namespace Events

// --- エンティティ: タグ、レイヤー、親 -------------------------------------------------------------
namespace detail {
inline const RmEntityApi* EntityPart() { return Part<RmEntityApi>(RM_PART_ENTITY, RM_PART_ENTITY_VERSION); }
inline std::string Text(uint32_t (*get)(void*, RmEntityId, char*, uint32_t), RmEntityId e) {
    char buf[128];
    const uint32_t n = get(Api()->ctx, e, buf, sizeof(buf));
    if (n < sizeof(buf)) return std::string(buf, n);
    std::string s(n + 1, '\0');
    get(Api()->ctx, e, s.data(), n + 1);
    s.resize(n);
    return s;
}
inline std::vector<Entity> Ids(uint32_t n, const std::vector<RmEntityId>& ids) {
    std::vector<Entity> out;
    for (uint32_t i = 0; i < n && i < ids.size(); ++i) out.push_back(Entity(ids[i]));
    return out;
}
}  // namespace detail

inline void Entity::SetName(std::string_view name) const {
    const std::string n(name);
    if (const RmEntityApi* t = detail::EntityPart()) t->setName(Api()->ctx, id, n.c_str());
}
inline bool Entity::SetParent(Entity parent, bool keepWorld) const {
    const RmEntityApi* t = detail::EntityPart();
    return t && t->setParent(Api()->ctx, id, parent.id, keepWorld ? 1 : 0, -1) != 0;
}
inline std::vector<Entity> Entity::Children() const {
    const RmEntityApi* t = detail::EntityPart();
    if (!t) return {};
    std::vector<RmEntityId> ids(16);
    uint32_t n = t->children(Api()->ctx, id, ids.data(), static_cast<uint32_t>(ids.size()));
    if (n > ids.size()) {
        ids.resize(n);
        n = t->children(Api()->ctx, id, ids.data(), static_cast<uint32_t>(ids.size()));
    }
    return detail::Ids(n, ids);
}
inline Entity Entity::FindChild(std::string_view path) const {
    Entity at = *this;
    while (!path.empty()) {
        const size_t slash = path.find('/');
        const std::string_view name = path.substr(0, slash);
        Entity found;
        for (const Entity& c : at.Children()) {
            if (c.Name() == name) {
                found = c;
                break;
            }
        }
        if (found.id == 0) return Entity();
        at = found;
        path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
    }
    return at.id == id ? Entity() : at;
}
inline std::string Entity::Tag() const {
    const RmEntityApi* t = detail::EntityPart();
    return t ? detail::Text(t->getTag, id) : std::string();
}
inline void Entity::SetTag(std::string_view tag) const {
    const std::string s(tag);
    if (const RmEntityApi* t = detail::EntityPart()) t->setTag(Api()->ctx, id, s.c_str());
}
inline std::string Entity::Layer() const {
    const RmEntityApi* t = detail::EntityPart();
    return t ? detail::Text(t->getLayer, id) : std::string();
}
inline void Entity::SetLayer(std::string_view layer) const {
    const std::string s(layer);
    if (const RmEntityApi* t = detail::EntityPart()) t->setLayer(Api()->ctx, id, s.c_str());
}

// このタグを持つすべてのエンティティ (階層の順) と、その最初の 1 つ (無効: なし)。
inline std::vector<Entity> FindAllWithTag(std::string_view tag) {
    const RmEntityApi* t = detail::EntityPart();
    if (!t) return {};
    const std::string s(tag);
    std::vector<RmEntityId> ids(16);
    uint32_t n = t->findWithTag(Api()->ctx, s.c_str(), ids.data(), static_cast<uint32_t>(ids.size()));
    if (n > ids.size()) {
        ids.resize(n);
        n = t->findWithTag(Api()->ctx, s.c_str(), ids.data(), static_cast<uint32_t>(ids.size()));
    }
    return detail::Ids(n, ids);
}
inline Entity FindWithTag(std::string_view tag) {
    const RmEntityApi* t = detail::EntityPart();
    if (!t) return Entity();
    const std::string s(tag);
    RmEntityId first = 0;
    return t->findWithTag(Api()->ctx, s.c_str(), &first, 1) > 0 ? Entity(first) : Entity();
}
// シーンの一番上のエンティティ。階層の順。
inline std::vector<Entity> Roots() { return Entity().Children(); }
// レイヤーの番号 ("" は 0、ゲーム設定の レイヤー は 1、2 ...。-1: 無い)。マスクには 1 << n。
inline int LayerIndex(std::string_view layer) {
    const RmEntityApi* t = detail::EntityPart();
    const std::string s(layer);
    return t ? t->layerIndex(Api()->ctx, s.c_str()) : -1;
}

namespace Game {
// 今ゲームが見通すカメラと、その切り替え (カメラ部品を持つエンティティ)。
inline Entity Camera() { return Entity(Api()->core->camera(Api()->ctx)); }
inline void SetCamera(Entity camera) { Api()->core->setCamera(Api()->ctx, camera.id); }

// シーン。ゲームのシーンは、エディターの シーン パネルの一覧 (その順)。シーンの名前はファイルの名前
// ("Assets/Scenes/Stage.rmscene" -> "Stage")。LoadScene はこのフレームの終わりに切り替える: このシーンの
// 全スクリプトの EndPlay、新しいシーンを読む、そのスクリプトの BeginPlay の順。
// そんなシーンが無ければ False (コンソールに警告)。
inline bool LoadScene(std::string_view name) {
    const std::string n(name);
    return Api()->core->loadScene(Api()->ctx, n.c_str()) != 0;
}
inline bool LoadScene(int index) { return Api()->core->loadSceneAt(Api()->ctx, index) != 0; }
// このフレームの後にゲームを終える (書き出したゲームはウィンドウが閉じ、エディターの Play は ■ のように止まる)。
inline void Quit() {
    if (const RmGameApi* g = Part<RmGameApi>(RM_PART_GAME, RM_PART_GAME_VERSION)) g->quit(Api()->ctx);
}
// 今 Play しているシーン: その名前、一覧での位置 (0 = 最初。-1: 一覧に無い)、ゲームが持つシーンの数。
inline std::string SceneName() {
    char buf[256];
    const uint32_t n = Api()->core->sceneName(Api()->ctx, buf, sizeof(buf));
    if (n < sizeof(buf)) return std::string(buf, n);
    std::string s(n + 1, '\0');
    Api()->core->sceneName(Api()->ctx, s.data(), n + 1);
    s.resize(n);
    return s;
}
inline int SceneIndex() { return Api()->core->sceneIndex(Api()->ctx); }
inline int SceneCount() { return Api()->core->sceneCount(Api()->ctx); }
}  // namespace Game

// ゲーム設定の グローバル変数 にある変数を、名前で。エンジンが一覧からヘッダー
// (RmGlobals.h。このファイルの最後で include される) を書くので、スクリプトはこう書ける
//
//   Global::score += 10;                 // 一覧の int
//   if (Global::cleared) ...             // bool
//   Global::playerName = "A";            // std::string
//
// (Visual Studio が名前を補完する)。GlobalVar<int>("score") は、自分で付けた名前での同じもの。
// 値はシーンを切り替えても残る。Play のたびに 初めの値 からやり直す。
template <class T>
struct GlobalVar {
    const char* name;
    constexpr explicit GlobalVar(const char* n) : name(n) {}

    T Get() const {
        T v{};
        if (const RmGlobalsApi* t = Table()) {
            if constexpr (std::is_same_v<T, std::string>) {
                char buf[256];
                const uint64_t n = t->getString(Api()->ctx, name, buf, sizeof(buf));
                if (n < sizeof(buf)) return std::string(buf, static_cast<size_t>(n));
                v.assign(static_cast<size_t>(n) + 1, '\0');
                t->getString(Api()->ctx, name, v.data(), n + 1);
                v.resize(static_cast<size_t>(n));
            } else if constexpr (std::is_same_v<T, bool>) {
                int32_t b = 0;
                t->get(Api()->ctx, name, RmField_Bool, &b);
                v = b != 0;
            } else {
                t->get(Api()->ctx, name, Kind(), &v);
            }
        }
        return v;
    }
    void Set(const T& v) const {
        const RmGlobalsApi* t = Table();
        if (!t) return;
        if constexpr (std::is_same_v<T, std::string>) {
            t->setString(Api()->ctx, name, v.data(), v.size());
        } else if constexpr (std::is_same_v<T, bool>) {
            const int32_t b = v ? 1 : 0;
            t->set(Api()->ctx, name, RmField_Bool, &b);
        } else {
            t->set(Api()->ctx, name, Kind(), &v);
        }
    }
    operator T() const { return Get(); }
    const GlobalVar& operator=(const T& v) const {
        Set(v);
        return *this;
    }
    // 数値 (と、文字への +=)。
    const GlobalVar& operator+=(const T& d) const requires(std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
        Set(Get() + d);
        return *this;
    }
    const GlobalVar& operator-=(const T& d) const requires std::is_arithmetic_v<T> {
        Set(Get() - d);
        return *this;
    }
    const GlobalVar& operator*=(const T& d) const requires std::is_arithmetic_v<T> {
        Set(Get() * d);
        return *this;
    }
    const GlobalVar& operator/=(const T& d) const requires std::is_arithmetic_v<T> {
        Set(Get() / d);
        return *this;
    }
    const GlobalVar& operator++() const requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        Set(Get() + 1);
        return *this;
    }
    T operator++(int) const requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        const T old = Get();
        Set(old + 1);
        return old;
    }
    const GlobalVar& operator--() const requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        Set(Get() - 1);
        return *this;
    }
    T operator--(int) const requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        const T old = Get();
        Set(old - 1);
        return old;
    }

private:
    static const RmGlobalsApi* Table() { return Part<RmGlobalsApi>(RM_PART_GLOBALS, RM_PART_GLOBALS_VERSION); }
    static constexpr uint32_t Kind() {
        if constexpr (std::is_same_v<T, int>) return RmField_Int;
        else if constexpr (std::is_same_v<T, float>) return RmField_Float;
        else if constexpr (std::is_same_v<T, Vec2>) return RmField_Vec2;
        else if constexpr (std::is_same_v<T, Vec3>) return RmField_Vec3;
        else static_assert(sizeof(T) == 0, "GlobalVar は bool / int / float / std::string / rm::Vec2 / rm::Vec3 だけです");
    }
};

// ゲームの保存: すべてのグローバル変数を、スロット (番号、または名前: "auto") に、プレイヤーの PC の
// ファイルとして書く (エディターの Play: <project>/Library/Saves、ゲーム: %LOCALAPPDATA%\<exe>\Saves)。
// Read は、保存した値をグローバル変数に戻す。false: できなかった (コンソールに知らせる)。
//
// 世界のセーブ: WriteWorld はグローバル変数に加えて、部品「セーブする」を付けた物の位置・有効・部品の値
// (ゲームの中で Spawn した物も、消した物も) と今のシーンを同じスロットに書く。ReadWorld はグローバル
// 変数をすぐ戻し、世界はこのフレームの後で戻す: セーブしたシーンを読み直して (Game::LoadScene と同じ。
// スクリプトは EndPlay → 読み直した物で BeginPlay)、その上にセーブの状態を当てる。
//   if (rm::Input::Pressed("Save")) rm::Save::WriteWorld(1);
//   if (rm::Input::Pressed("Load") && rm::Save::HasWorld(1)) rm::Save::ReadWorld(1);
// Write (グローバル変数だけ) は、スロットの世界を変えない。
namespace Save {
namespace detail {
inline const RmSaveApi* Table() { return Part<RmSaveApi>(RM_PART_SAVE, RM_PART_SAVE_VERSION); }
inline const RmSaveApi2* Table2() { return Part<RmSaveApi2>(RM_PART_SAVE, RM_PART_SAVE_VERSION2); }
inline std::string Slot(int slot) { return std::to_string(slot); }
}  // namespace detail
inline bool Write(const char* slot) {
    const RmSaveApi* t = detail::Table();
    return t && t->write(Api()->ctx, slot) != 0;
}
inline bool Read(const char* slot) {
    const RmSaveApi* t = detail::Table();
    return t && t->read(Api()->ctx, slot) != 0;
}
inline bool Exists(const char* slot) {
    const RmSaveApi* t = detail::Table();
    return t && t->exists(Api()->ctx, slot) != 0;
}
inline bool Delete(const char* slot) {
    const RmSaveApi* t = detail::Table();
    return t && t->remove(Api()->ctx, slot) != 0;
}
inline bool WriteWorld(const char* slot) {
    const RmSaveApi2* t = detail::Table2();
    return t && t->writeWorld(Api()->ctx, slot) != 0;
}
inline bool ReadWorld(const char* slot) {
    const RmSaveApi2* t = detail::Table2();
    return t && t->readWorld(Api()->ctx, slot) != 0;
}
inline bool HasWorld(const char* slot) {
    const RmSaveApi2* t = detail::Table2();
    return t && t->hasWorld(Api()->ctx, slot) != 0;
}
inline bool Write(int slot = 0) { return Write(detail::Slot(slot).c_str()); }
inline bool Read(int slot = 0) { return Read(detail::Slot(slot).c_str()); }
inline bool Exists(int slot = 0) { return Exists(detail::Slot(slot).c_str()); }
inline bool Delete(int slot = 0) { return Delete(detail::Slot(slot).c_str()); }
inline bool WriteWorld(int slot = 0) { return WriteWorld(detail::Slot(slot).c_str()); }
inline bool ReadWorld(int slot = 0) { return ReadWorld(detail::Slot(slot).c_str()); }
inline bool HasWorld(int slot = 0) { return HasWorld(detail::Slot(slot).c_str()); }
}  // namespace Save

namespace detail {
// rmgen の検査用: 集成体のメンバーの数。
struct AnyField {
    template <class U>
    constexpr operator U&() const noexcept;
};
template <class T, class... A>
constexpr size_t CountFields() {
    if constexpr (requires { T{A{}..., AnyField{}}; }) {
        return CountFields<T, A..., AnyField>();
    } else {
        return sizeof...(A);
    }
}
}  // namespace detail

}  // namespace rm

// ゲームのグローバル変数を Global::<name> として (エンジンが、ゲーム設定の グローバル変数 から
// Library/Scripts/Globals/RmGlobals.h に書く - スクリプトのビルドのたびに先に)。
#if __has_include("RmGlobals.h")
#include "RmGlobals.h"
#endif
