// RM.h - write the moves of your game in C++ (RM Engine scripts).
//
// ■ A script
//
//   // Assets/Scripts/Mover.h
//   #pragma once
//   #include "RM.h"
//
//   RM_SCRIPT(Label="動く")
//   struct Mover {
//       RM_PROPERTY(Edit, Label="速さ", Min=0) float speed = 3.0f;   // shown in the inspector, saved
//       RM_TRANSIENT()                          float time = 0;       // neither shown nor saved
//
//       void BeginPlay(rm::Entity self);           // when the game starts (or the entity is made)
//       void Tick(rm::Entity self, float dt);      // every frame; dt = seconds since the last one
//       void EndPlay(rm::Entity self);             // when the game stops (or it is destroyed / removed)
//       void OnReloaded(rm::Entity self);          // the scripts were built again while playing
//       // with the physics (RMPhysics.h: a Collider on the entity) - after the frame's fixed steps:
//       void OnHit(rm::Entity self, rm::Entity other);           // began touching `other` (once until they part)
//       void OnTriggerEnter(rm::Entity self, rm::Entity other);  // `other` came into a trigger (or this into one)
//       void OnTriggerExit(rm::Entity self, rm::Entity other);   // ... and went out
//       // with the game UI (RMUI.h: a ボタン on the entity) - after the frame's systems:
//       void OnClick(rm::Entity self);                           // the button was pressed (mouse / 決定)
//       // rm::Events (below) - after the frame's Ticks:
//       void OnEvent(rm::Entity self, const rm::Event& e);       // an event sent to it (or to every entity)
//   };
//
// ■ What else is here (each explained where it is written, further down)
//   rm::Find / FindWithTag / FindAllWithTag, Entity::Tag / Layer / SetParent / Children / FindChild
//   rm::Input (actions; by player 0..3; the player's own bindings), rm::Mouse, rm::Key, rm::Pad
//   rm::DeltaTime / Time / TimeScale / SetTimeScale / UnscaledDeltaTime / FrameCount
//   rm::Task + rm::StartCoroutine (co_await rm::WaitSeconds ...), rm::After / rm::Every (timers)
//   rm::Events::Send / SendTo / SendDown, rm::Game (scenes, Quit), Global:: (global variables), rm::Save
//   RMMath.h (included here): Vec2 / Vec3 + - *, Length, Normalize, Lerp, rm::Math, rm::Quat, rm::Random
//
//   // Assets/Scripts/Mover.cpp
//   #include "Mover.h"
//   #include "RM3D.h"                              // Transform in 3D (RM2D.h: in 2D)
//   void Mover::Tick(rm::Entity self, float dt) {
//       rm::Vec3 p = rm::Transform3D(self).Position();
//       p.x += rm::Input::Axis("MoveX") * speed * dt;   // an action of the game settings' 入力
//       rm::Transform3D(self).SetPosition(p);
//   }
//
// ■ Rules
//   * A script is a plain struct (no constructor, no base class, no private). Every member is
//     RM_PROPERTY(...) or RM_TRANSIENT(). Field types: bool, int, float, std::string, rm::Vec2,
//     rm::Vec3, rm::Color, rm::EntityRef, rm::PrefabRef / rm::ImageRef / rm::ModelRef / rm::SoundRef (an asset),
//     an enum class with RM_ENUM, a struct with RM_STRUCT (its members RM_PROPERTY too), and
//     std::vector of any of those (not of a vector).
//
//       RM_ENUM() enum class Mode { RM_ITEM(Label="歩く") Walk, Run };
//       RM_STRUCT() struct Wave { RM_PROPERTY(Edit) int count = 3; RM_PROPERTY(Edit) float gap = 1; };
//       RM_SCRIPT() struct Spawner {
//           RM_PROPERTY(Edit) rm::PrefabRef enemy;            // pick a .rmprefab in the inspector
//           RM_PROPERTY(Edit) std::vector<Wave> waves;
//           RM_PROPERTY(Edit) Mode mode = Mode::Walk;
//       };
//   * RM_PROPERTY: Edit (shown), ReadOnly, Label="...", Tooltip="...", Min=, Max=, Step=.
//   * The hooks are optional: write only the ones you need. Do not keep state in `static` variables:
//     when the scripts are built again they are thrown away (fields keep their values, by name).
//   * Several scripts run type by type: the order of the types is the game settings' スクリプトの
//     実行順 (smaller first; a type not listed is 0, then by name). The same type: by entity, in the
//     order they were attached. Things made during a frame get BeginPlay before their first Tick.
//   * A script that crashes (a null pointer ...) is stopped alone and told on the console; the
//     editor and the other scripts go on.
//   * Making and destroying: rm::Spawn("Bullet") / rm::Spawn(enemyPrefab) make an entity at once
//     (set it up right away); its scripts start at the next frame. self.Destroy() and Remove<T>()
//     take effect at the end of the frame (EndPlay first; the children go too). An Entity of
//     something destroyed is not Valid() any more - every call on it does nothing.
//   * Your headers never include anything generated: rmgen reads them and writes the registration.
//
// The engine gives these headers only: RM.h (this), RmAbi.h (the boundary, C) and the plugins' parts
// (RM3D.h, RM2D.h). Hover a name in Visual Studio to read what it does.
#pragma once

#include "RmAbi.h"

#include <coroutine>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>  // (a field may be a std::vector)

// --- Annotations (read by rmgen; they expand to nothing) ------------------------------------
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

// The engine's table (set when the DLL is loaded; written by rmgen into the registration).
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

#include "RMMath.h"  // + - * on Vec2 / Vec3, Length, Normalize, Lerp, rm::Math, rm::Quat, rm::Random

namespace rm {
// An entity chosen in the inspector (drag it from the 階層). Entity(ref) to use it.
struct EntityRef {
    uint64_t id = 0;
};

// An asset chosen in the inspector (drag it from the アセット panel), by its id - moving or renaming
// the file keeps it. Plain bytes (the engine reads and writes them): copy freely.
struct PrefabRef {  // a .rmprefab: rm::Spawn(ref)
    char guid[RM_ASSET_ID_SIZE] = {};
    bool Empty() const { return guid[0] == 0; }
};
struct ImageRef {  // a picture
    char guid[RM_ASSET_ID_SIZE] = {};
    bool Empty() const { return guid[0] == 0; }
};
struct ModelRef {  // a 3D model
    char guid[RM_ASSET_ID_SIZE] = {};
    bool Empty() const { return guid[0] == 0; }
};
struct SoundRef {  // a sound (WAV / OGG ...): rm::Audio::Play(ref) - RMAudio.h
    char guid[RM_ASSET_ID_SIZE] = {};
    bool Empty() const { return guid[0] == 0; }
};

struct Entity;
// A part of the API by name (defined below).
template <class T>
const T* Part(const char* name, uint32_t version);
namespace detail {
// An engine component's handle (RM3D.h's Transform3D ...): it names its type and is made from an Entity.
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
    // The C++ name of T, as the engine saves it ("Mover", "game::Mover").
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

// An entity of the scene (by its id). Cheap to copy; check Valid() before using one you found.
struct Entity {
    RmEntityId id = 0;

    Entity() = default;
    explicit Entity(RmEntityId i) : id(i) {}
    Entity(EntityRef r) : id(r.id) {}

    // There is such an entity (it was not deleted).
    bool Valid() const { return id != 0 && Api()->core->exists(Api()->ctx, id) != 0; }
    // 有効: it and every parent are switched on.
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

    // Its parent changed: under `parent` (an invalid Entity: the top), last among its children. keepWorld:
    // it stays where it is in the world (its Transform is changed to make up for the new parent's);
    // false: its Transform stays as it is (it moves with the new parent). False: refused (under itself).
    bool SetParent(Entity parent, bool keepWorld = true) const;
    // Its children, in the 階層's order (an invalid Entity: the top-level entities).
    std::vector<Entity> Children() const;
    // The first child (or deeper, with "Arm/Hand") with this name; invalid when none.
    Entity FindChild(std::string_view path) const;

    // Tags and layers (names; the game settings' タグ / レイヤー list them). "" = none / the default layer.
    std::string Tag() const;
    void SetTag(std::string_view tag) const;
    bool CompareTag(std::string_view tag) const { return Tag() == tag; }
    std::string Layer() const;
    void SetLayer(std::string_view layer) const;

    // Gone at the end of this frame, with everything below it (their EndPlay first).
    void Destroy() const { Api()->core->destroy(Api()->ctx, id); }

    // Components. T is a script (Add / Get give T*; null when there is none) or one of the engine's in
    // RM3D.h / RM2D.h (rm::Transform3D, rm::Transform2D: they give that, Valid() when it is there).
    //   auto* hp = self.Add<Health>();          hp->max = 10;     // a script: BeginPlay next frame
    //   self.Add<rm::Transform3D>().SetPosition({0, 1, 0});
    //   if (auto* h = other.Get<Health>()) h->hp -= 1;
    //   self.Remove<Health>();                                    // at the end of the frame (EndPlay)
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
    // (the older name of Get for scripts)
    template <class T>
    T* GetScript(uint32_t nth = 0) const {
        return static_cast<T*>(Api()->core->getScript(Api()->ctx, id, detail::ComponentName<T>(), nth));
    }
    bool operator==(const Entity&) const = default;
};

// A new entity named `name` (under `parent`; none: at the top), empty - add what it needs. It is
// there at once; its scripts start at the next frame.
inline Entity Spawn(std::string_view name, Entity parent = Entity()) {
    const std::string n(name);
    return Entity(Api()->core->spawn(Api()->ctx, n.c_str(), parent.id));
}
// A copy of a prefab (with what is nested in it). Invalid when the field is empty or the file is gone.
inline Entity Spawn(const PrefabRef& prefab, Entity parent = Entity()) {
    if (prefab.Empty()) return Entity();
    return Entity(Api()->core->spawnPrefab(Api()->ctx, prefab.guid, parent.id));
}

// The first entity with this name (in the 階層's order); an invalid Entity when there is none.
inline Entity Find(std::string_view name) {
    const std::string n(name);
    return Entity(Api()->core->findByName(Api()->ctx, n.c_str()));
}

// Messages on the editor's console (and the game's log).
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

// Seconds of this frame (the same as Tick's dt) and seconds since the game started.
inline float DeltaTime() { return Api()->core->deltaTime(Api()->ctx); }
inline double Time() { return Api()->core->time(Api()->ctx); }

// Keys (Windows virtual-key codes: any VK_ value works too).
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
// A game pad's controls (as the game settings' 入力 name them: for Input::SetBinding).
namespace Pad {
enum : int {
    A = 0x100, B, X, Y, LB, RB, Back, Start, LeftThumb, RightThumb, Up, Down, Left, Right, LT, RT,
    LeftStickX, LeftStickY, RightStickX, RightStickY,
};
}  // namespace Pad

// A plugin's part of the API (RM3D.h uses it); null when the plugin is not in this engine.
template <class T>
const T* Part(const char* name, uint32_t version) {
    return static_cast<const T*>(Api()->part(Api()->ctx, name, version));
}

// Input. Read it by ACTION NAME: the game settings' 入力 lists the actions ("MoveX", "Jump" ...) and
// what drives each (keys, mouse buttons, a game pad) - so a pad works without changing a script, and
// the keys can be changed without building again. A name that is not in the list is false / 0 (and
// told once on the console).
namespace Input {
namespace detail {
inline const RmInputApi* Table() { return Part<RmInputApi>(RM_PART_INPUT, RM_PART_INPUT_VERSION); }
}  // namespace detail
// Held down now (one of its keys / buttons; a stick pushed more than half way).
inline bool Down(const char* action) {
    const RmInputApi* t = detail::Table();
    return t && t->down(Api()->ctx, action) != 0;
}
// Went down this frame (once per press).
inline bool Pressed(const char* action) {
    const RmInputApi* t = detail::Table();
    return t && t->pressed(Api()->ctx, action) != 0;
}
// Went up this frame.
inline bool Released(const char* action) {
    const RmInputApi* t = detail::Table();
    return t && t->released(Api()->ctx, action) != 0;
}
// -1..1: its keys / buttons times their 向き (e.g. ← -1, → 1), added, and a stick as far as it is pushed.
inline float Axis(const char* action) {
    const RmInputApi* t = detail::Table();
    return t ? t->axis(Api()->ctx, action) : 0.0f;
}

// A key itself (rm::Key::Space, or any VK_ value). Prefer the action names above: these never see a
// game pad.
inline bool Down(int key) { return Api()->core->keyDown(Api()->ctx, key) != 0; }
inline bool Pressed(int key) { return Api()->core->keyPressed(Api()->ctx, key) != 0; }
inline bool Released(int key) { return Api()->core->keyReleased(Api()->ctx, key) != 0; }

// --- Players (up to 4): the same actions, of one player only -------------------------------------
// player 0..3: the game pad in that slot (Windows numbers the pads as they are connected) and the
// keyboard and the mouse when the game settings' キーボードとマウスのプレイヤー is that one (1P: 0).
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
// A game pad is in that slot.
inline bool PadConnected(int player) {
    const RmInputApi2* t = detail::Table2();
    return t && t->padConnected(Api()->ctx, player) != 0;
}
// Shakes the player's pad for `seconds` (low: the left, heavy motor; high: the right, light one; 0..1).
// The game stopping (or ■) stops it.
inline void Vibrate(int player, float low, float high, float seconds) {
    if (const RmInputApi2* t = detail::Table2()) t->vibrate(Api()->ctx, player, low, high, seconds);
}

// --- What the player may change: the bindings (key config) ---------------------------------------
// The game settings' 入力 until changed here; SaveBindings keeps them on the player's PC and the game
// reads them back by itself the next time it starts. A control is a key (rm::Key::Space, a VK_ value),
// a mouse button (rm::Key::MouseLeft) or a pad's (rm::Pad::A). scale: as the settings' 向き (-1 / 1).
//   // "Press a key for Jump": wait for one, then put it in the place of the first binding
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
// The binding at `index` replaced (index == the count: one added). False: no such action / index.
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
// Back to the game settings' (not saved until SaveBindings).
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
// What went down this frame (a key, a mouse button, a pad's button, a stick or a trigger pushed past
// half way) - 0: nothing. *direction: 1, or -1 for a stick pushed left / down.
inline int AnyPressed(int player = -1, float* direction = nullptr) {
    const RmInputApi2* t = detail::Table2();
    float d = 1;
    const int c = t ? t->anyPressed(Api()->ctx, player, &d) : 0;
    if (direction) *direction = d;
    return c;
}
// A control's name ("Space", "A", "PadA", "MouseLeft" ...): to show what is bound.
inline std::string ControlName(int control) {
    const RmInputApi2* t = detail::Table2();
    if (!t) return {};
    char buf[64];
    const uint32_t n = t->controlName(Api()->ctx, control, buf, sizeof(buf));
    return std::string(buf, n < sizeof(buf) ? n : sizeof(buf) - 1);
}
}  // namespace Input

// The mouse on the game's screen: the game settings' 画面 (DIPs, (0, 0) at the top left, y down).
//   if (rm::Mouse::Pressed()) { rm::Vec2 p = rm::Mouse::Position(); ... }
namespace Mouse {
namespace detail {
inline const RmMouseApi* Table() { return Part<RmMouseApi>(RM_PART_MOUSE, RM_PART_MOUSE_VERSION); }
}  // namespace detail
// The mouse is over the game (the editor's game view; the game's window).
inline bool OverGame() {
    float p[2];
    const RmMouseApi* t = detail::Table();
    return t && t->position(Api()->ctx, p) != 0;
}
// Where it is (where it was last when it is not over the game).
inline Vec2 Position() {
    float p[2] = {0, 0};
    if (const RmMouseApi* t = detail::Table()) t->position(Api()->ctx, p);
    return {p[0], p[1]};
}
// Moved since the last frame.
inline Vec2 Delta() {
    float p[2] = {0, 0};
    if (const RmMouseApi* t = detail::Table()) t->delta(Api()->ctx, p);
    return {p[0], p[1]};
}
// The wheel's notches this frame (+: turned away from you).
inline float Wheel() {
    const RmMouseApi* t = detail::Table();
    return t ? t->wheel(Api()->ctx) : 0.0f;
}
inline Vec2 ScreenSize() {
    float p[2] = {0, 0};
    if (const RmMouseApi* t = detail::Table()) t->screenSize(Api()->ctx, p);
    return {p[0], p[1]};
}
// Where it is from 0 to 1 ((0, 0) top left, (1, 1) bottom right).
inline Vec2 ViewportPosition() {
    const Vec2 s = ScreenSize(), p = Position();
    return {s.x > 0 ? p.x / s.x : 0.0f, s.y > 0 ? p.y / s.y : 0.0f};
}
// The buttons: 0 left, 1 right, 2 middle (only over the game).
inline int ButtonKey(int button) { return button == 1 ? Key::MouseRight : button == 2 ? Key::MouseMiddle : Key::MouseLeft; }
inline bool Down(int button = 0) { return Input::Down(ButtonKey(button)); }
inline bool Pressed(int button = 0) { return Input::Pressed(ButtonKey(button)); }
inline bool Released(int button = 0) { return Input::Released(ButtonKey(button)); }
}  // namespace Mouse

// --- Time --------------------------------------------------------------------------------------------
// The time scale: 1 normal, 0.5 slow motion, 0 stopped (a pause menu). Every dt of the game is
// multiplied by it (Tick's, the systems', the physics', the particles'); Tick is still called at 0.
inline float TimeScale() {
    const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION);
    return t ? t->scale(Api()->ctx) : 1.0f;
}
inline void SetTimeScale(float scale) {
    if (const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION)) t->setScale(Api()->ctx, scale);
}
// The real seconds of this frame and since the game started (the time scale does not count).
inline float UnscaledDeltaTime() {
    const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION);
    return t ? t->unscaledDeltaTime(Api()->ctx) : 0.0f;
}
inline double UnscaledTime() {
    const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION);
    return t ? t->unscaledTime(Api()->ctx) : 0.0;
}
// Frames played before this one (the first frame is 0).
inline uint64_t FrameCount() {
    const RmTimeApi* t = Part<RmTimeApi>(RM_PART_TIME, RM_PART_TIME_VERSION);
    return t ? t->frameCount(Api()->ctx) : 0;
}

// --- Coroutines and timers ------------------------------------------------------------------------
// A function that waits in the middle (C++20 co_await) - written as a function that returns rm::Task:
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
// It runs at once up to its first co_await, then on after this frame's Ticks when its wait is over.
// It belongs to the script that started it: that script's EndPlay (its entity destroyed or switched
// off, the scene switched, ■) stops it - so a member function's `this` stays good while it runs. When
// the scripts are built again while playing, every coroutine stops (OnReloaded may start them again).
// Waits: WaitSeconds (game time), WaitRealSeconds (real time: for a pause menu at time scale 0),
// NextFrame / WaitFrames, WaitUntil / WaitWhile (a condition asked once a frame), and a Coroutine (until
// it has ended). Timers: rm::After(2.0f, [] { ... }) and rm::Every(1.0f, [] { ... }).
class Task;
namespace detail {
inline const RmCoroutinesApi* Co() { return Part<RmCoroutinesApi>(RM_PART_COROUTINES, RM_PART_COROUTINES_VERSION); }
}  // namespace detail

// A coroutine that was started (by its id): Running(), Stop(); co_await it to wait until it ends.
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
        std::function<bool()> until;  // WaitUntil: asked once a frame before it runs on
        Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }  // (StartCoroutine runs it)
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { throw; }  // (the engine stops it and tells the console)
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
        if (h_) h_.destroy();  // (never started)
    }
    Handle Release() {
        Handle h = h_;
        h_ = {};
        return h;
    }

    // The engine runs it through these (RmCoroutinesApi::start).
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

// co_await one of these inside an rm::Task.
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

// Starts it (it runs now up to its first wait). The returned Coroutine stops it or is waited for.
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
// Every coroutine of the script that is running now.
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
// Timers: `f` after `seconds` of game time (realTime: real seconds), or every `seconds` (`times` times;
// -1: until stopped). They belong to the script that starts them, as a coroutine.
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

// --- Events ---------------------------------------------------------------------------------------
// A message by name to the scripts that want it: a script with
//     void OnEvent(rm::Entity self, const rm::Event& e) { if (e.Is("Damage")) hp -= e.number; }
// gets it after this frame's Ticks (in the scripts' order). The sender knows nothing of who listens.
//   rm::Events::Send("GameOver");                          // every entity
//   rm::Events::SendTo(enemy, "Damage", 3);                // that entity's scripts only
//   rm::Events::SendDown(ui, "Show");                      // that entity and everything below it
struct Event {
    std::string_view name;  // (valid during OnEvent: copy what you keep)
    Entity sender;          // whose script sent it (invalid: the engine)
    double number = 0;
    std::string_view text;
    Entity entity;          // an entity it carries (invalid: none)
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

// --- Entities: tags, layers, parents ----------------------------------------------------------------
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

// Every entity with this tag (in the 階層's order), and the first one (invalid: none).
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
// The top-level entities of the scene, in the 階層's order.
inline std::vector<Entity> Roots() { return Entity().Children(); }
// A layer's number ("" 0, the game settings' レイヤー 1, 2 ...; -1: not there) - for a mask: 1 << n.
inline int LayerIndex(std::string_view layer) {
    const RmEntityApi* t = detail::EntityPart();
    const std::string s(layer);
    return t ? t->layerIndex(Api()->ctx, s.c_str()) : -1;
}

namespace Game {
// The camera the game sees through now, and switching it (an entity with a camera component).
inline Entity Camera() { return Entity(Api()->core->camera(Api()->ctx)); }
inline void SetCamera(Entity camera) { Api()->core->setCamera(Api()->ctx, camera.id); }

// Scenes. The game's scenes are the list in the editor's シーン panel (in its order); a scene's name is
// its file's name ("Assets/Scenes/Stage.rmscene" -> "Stage"). LoadScene switches at the end of this
// frame: EndPlay of every script of this scene, the new scene read, then its scripts' BeginPlay.
// False (and a warning on the console) when there is no such scene.
inline bool LoadScene(std::string_view name) {
    const std::string n(name);
    return Api()->core->loadScene(Api()->ctx, n.c_str()) != 0;
}
inline bool LoadScene(int index) { return Api()->core->loadSceneAt(Api()->ctx, index) != 0; }
// Ends the game after this frame (the exported game's window closes; the editor's Play stops, as ■).
inline void Quit() {
    if (const RmGameApi* g = Part<RmGameApi>(RM_PART_GAME, RM_PART_GAME_VERSION)) g->quit(Api()->ctx);
}
// The scene playing now: its name, its place in the list (0 = the first; -1: not in the list) and how
// many scenes the game has.
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

// A global variable of the game settings' グローバル変数, by name. The engine writes a header from the
// list (RmGlobals.h, included at the end of this file), so a script writes
//
//   Global::score += 10;                 // an int of the list
//   if (Global::cleared) ...             // a bool
//   Global::playerName = "A";            // a std::string
//
// (Visual Studio completes the names.) GlobalVar<int>("score") is the same by a name of your own.
// Values stay when the scene is switched; at every Play they start again from 初めの値.
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
    // Numbers (and += on a text).
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

// Saving the game: every global variable to a slot (a number, or a name: "auto"), as a file of the
// player's PC (the editor's Play: <project>/Library/Saves; a game: %LOCALAPPDATA%\<exe>\Saves). Read
// puts the saved values back into the global variables. false: not done (told on the console).
namespace Save {
namespace detail {
inline const RmSaveApi* Table() { return Part<RmSaveApi>(RM_PART_SAVE, RM_PART_SAVE_VERSION); }
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
inline bool Write(int slot = 0) { return Write(detail::Slot(slot).c_str()); }
inline bool Read(int slot = 0) { return Read(detail::Slot(slot).c_str()); }
inline bool Exists(int slot = 0) { return Exists(detail::Slot(slot).c_str()); }
inline bool Delete(int slot = 0) { return Delete(detail::Slot(slot).c_str()); }
}  // namespace Save

namespace detail {
// For rmgen's checks: the number of members of an aggregate.
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

// The game's global variables as Global::<name> (written by the engine from the game settings'
// グローバル変数 into Library/Scripts/Globals/RmGlobals.h - before every build of the scripts).
#if __has_include("RmGlobals.h")
#include "RmGlobals.h"
#endif
