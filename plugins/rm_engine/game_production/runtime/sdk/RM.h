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
//       void EndPlay(rm::Entity self);             // when the game stops
//   };
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
//     rm::Vec3, rm::Color, rm::EntityRef. (Lists, enums and nested structs: later.)
//   * RM_PROPERTY: Edit (shown), ReadOnly, Label="...", Tooltip="...", Min=, Max=, Step=.
//   * The hooks are optional: write only the ones you need. Do not keep state in `static` variables:
//     when the scripts are built again they are thrown away (fields keep their values, by name).
//   * Several scripts run type by type: the order of the types is the game settings' スクリプトの
//     実行順 (smaller first; a type not listed is 0, then by name). The same type: by entity, in the
//     order they were attached. Things made during a frame get BeginPlay before their first Tick.
//   * A script that crashes (a null pointer ...) is stopped alone and told on the console; the
//     editor and the other scripts go on.
//   * Your headers never include anything generated: rmgen reads them and writes the registration.
//
// The engine gives these headers only: RM.h (this), RmAbi.h (the boundary, C) and the plugins' parts
// (RM3D.h, RM2D.h). Hover a name in Visual Studio to read what it does.
#pragma once

#include "RmAbi.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

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

namespace rm {

// The engine's table (set when the DLL is loaded; written by rmgen into the registration).
const RmApi* Api();

struct Vec2 {
    float x = 0, y = 0;
};
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};
// An entity chosen in the inspector (drag it from the 階層). Entity(ref) to use it.
struct EntityRef {
    uint64_t id = 0;
};

namespace detail {
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
    // A script of this entity (the nth of that type); nullptr when it has none.
    template <class T>
    T* GetScript(uint32_t nth = 0) const {
        static const std::string name = detail::TypeName<T>();
        return static_cast<T*>(Api()->core->getScript(Api()->ctx, id, name.c_str(), nth));
    }
    bool operator==(const Entity&) const = default;
};

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
};
}  // namespace Key

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
}  // namespace Input

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
