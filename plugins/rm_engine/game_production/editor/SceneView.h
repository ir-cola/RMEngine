#pragma once
// The scene view's frame. Game production shows the panel; what is drawn in it - and how the
// camera moves - comes from a plugin that knows a space (the connector ゲーム制作 × 3D draws it in
// 3D). Game production itself never knows 3D or 2D (design RME_PLATFORM 4-f).
//
//   // in a connector's RegisterEditor:
//   static const game_production::SceneViewInfo kView{&CreateMySceneView};
//   registry.Contribute(game_production::kSceneViewPoint, kView);
//
// Each scene view belongs to a space (`space`: "3d", "2d" - only a name to game production). The game
// chooses one (GameSettings::space - project data: the game view and new entities follow it); when
// more than one is offered, the scene panel has a strip of their labels to switch (an edit: undo, unsaved).
// None chosen yet (or a space whose plugin is gone): the one with the smallest `order`. Without any,
// the panel says which plugins bring one.

#include "core/reflect/FieldCommand.h"
#include "editor/plugin/Software.h"
#include "rm_engine/game_production/runtime/DebugDraw.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rm::game_production {

// PluginRegistry point: items are SceneViewInfo (static).
inline constexpr char kSceneViewPoint[] = "rm_engine.game_production.scene_view";

// What a scene view gets each time it is drawn.
struct SceneViewHost {
    editor::SoftwareHost& host;                     // the GPU, the history ...
    const Scene& scene;
    std::span<const int> selection;                 // the selected entities (the 階層's)
    std::function<void(std::vector<int>)> select;   // select these instead (empty: nothing)
    // Where a component of an entity is (found again each time it is called - undo may rebuild the
    // scene). For edits: change the value through it and push reflect::SetFieldCommand with it on
    // host.History() (edit = undo entry = unsaved).
    std::function<std::function<reflect::EditTarget()>(int id, const std::string& type)> component;
    // Play: `scene` is the copy that plays (it changes every frame, and is not edited - no gizmo).
    bool playing = false;
    // How fast the game's time goes while playing (rm::SetTimeScale; 0 while paused): what moves by itself
    // in a picture (the particles) moves by the picture's time times this.
    float timeScale = 1;
    // The game view: only the picture of the game - no grid, tools or selection, seen through the
    // game's camera (`camera`), the game settings' 背景の色 behind it (unless the camera has its own).
    bool game = false;
    Color background{0, 0, 0, 1};
    // The game settings' 最初のカメラ: the entity the game sees through (0: none chosen). What makes an
    // entity a camera is the view's business (a 3D camera component, ...).
    uint64_t camera = 0;
    // The shape of the game's screen (the game settings' 画面: width / height). The game view is drawn
    // in it (bands around when the panel is wider or taller); the scene view's camera preview too.
    float aspect = 16.f / 9.f;
    // Shows the game settings in the inspector (the game view's way to choose 最初のカメラ).
    std::function<void()> openSettings;
    // Play: the lines the scripts drew with rm::Debug (DebugDraw.h) - each view draws the ones of its
    // space (the scene view only while its toolbar's デバッグ is on). Null when not playing.
    const DebugDraw* debug = nullptr;
    // The game settings (read only: the 画面's size and safe area - the UI view).
    const GameSettings* settings = nullptr;
    // Changes the scene's shape (a view makes entities or moves them: the UI view drops a picture, gives
    // a UI thing another parent): `touched` the entities the change alters (with what is under them;
    // the ones it makes are found by themselves - SceneRecord), one undo entry named `label`. `change`
    // gives the entities to select after it (empty: the selection stays). Nothing while playing.
    std::function<void(std::vector<int> touched, std::string label, std::function<std::vector<int>(Scene&)> change)> editScene;
};

// One per open game and view (the scene view, the game view): keeps its own camera.
class SceneView {
public:
    virtual ~SceneView() = default;
    virtual void Draw(ui::Context& c, const ui::Rect& r, SceneViewHost& host) = 0;
    // The game view of another space (the game's own space drew first): this space's picture laid over
    // it, without filling the background (2D over 3D: a HUD), when the view has something to show
    // that way. `host.game` is true. Default: nothing.
    virtual void DrawLayer(ui::Context& c, const ui::Rect& r, SceneViewHost& host) { (void)c, (void)r, (void)host; }
    // How the user looks (camera, tool ...): kept with the panel layout, not in the project.
    virtual void LoadState(const json::Value& state) { (void)state; }
    virtual json::Value SaveState() const { return {}; }
};

// The game screen's rectangle in a panel: the largest of `aspect` (width / height) in its middle (the game
// view draws the game there; bands around it). One place for every view (rule 2).
inline ui::Rect GameScreenRect(const ui::Rect& panel, float aspect) {
    const float a = aspect > 1e-3f ? aspect : 1e-3f;
    float w = panel.w, h = panel.w / a;
    if (h > panel.h) {
        h = panel.h;
        w = h * a;
    }
    return {panel.x + (panel.w - w) * 0.5f, panel.y + (panel.h - h) * 0.5f, w, h};
}

// PluginRegistry point: items are ViewPanelInfo (static). A panel of the game software that a plugin
// adds (ゲーム UI: the UI view). It gets the same SceneViewHost as the scene view (game = false) and is
// not of a space: shown whatever the game's space is.
inline constexpr char kViewPanelPoint[] = "rm_engine.game_production.view_panel";
struct ViewPanelInfo {
    const char* id = "";             // the panel's id in the layout (unique among the game's panels)
    const wchar_t* title = nullptr;  // a TrKey string
    std::unique_ptr<SceneView> (*create)() = nullptr;
};

// PluginRegistry point: items are CreateItemInfo (static). Rows a plugin adds to the 階層's right-click
// menu 追加 ▸ (ゲーム UI: UI ▸ キャンバス / ボタン ...; ゲーム制作 × 3D: 3D オブジェクト ▸ 箱 / 球 ...),
// each in its folder - the folders in a fixed order (GameSoftware), then the order of the plugins.
// The entity is made in one of two ways:
//   make   makes it (and what is under it) in `scene` under `parent` at `index` and returns it (0:
//          nothing) - it decides its name and its components (a 3D 箱 gets a 3D Transform whatever the
//          game's space is);
//   fill   adds to an entity made like 空のエンティティ (named `label`, with the Transform of the game's
//          space) - for what is at home in any space (パーティクル, 音源).
// It is one undo entry, then selected with its name being typed.
inline constexpr char kCreateItemPoint[] = "rm_engine.game_production.create_item";
struct CreateItemInfo {
    const wchar_t* label = nullptr;   // a TrKey string: what it is (a noun: the undo entry is 「箱」を作成)
    const wchar_t* folder = nullptr;  // a TrKey string ("3D オブジェクト"; "/" between folders inside folders); null: 追加 itself
    int (*make)(Scene& scene, int parent, size_t index) = nullptr;
    void (*fill)(Scene& scene, int entity) = nullptr;
    bool child = false;  // under the one selected entity (else - and with none selected - after the selection)
};

// The component of type T added to entity `id` (or the one it has already, if it may not have two; null:
// the type is not offered now). For CreateItemInfo's make / fill.
template <class T>
T* AddComponentTo(Scene& scene, int id) {
    scene.AddComponent(id, reflect::TypeOf<T>());
    SceneEntity* e = scene.Find(id);
    Component* c = e ? e->FindComponent(reflect::TypeOf<T>().name) : nullptr;
    return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
}

struct SceneViewInfo {
    std::unique_ptr<SceneView> (*create)();
    const char* space = "";              // e.g. "3d" (ComponentInfo::space names the same)
    const wchar_t* label = nullptr;      // e.g. 3D (a TrKey string: translated when shown)
    int order = 0;                       // the smaller is the default
};

// PluginRegistry point: items are SceneLinesInfo (static). Lines a plugin shows over the scene in the
// scene view while editing and playing (物理: the colliders' shapes). Each view takes the lines of its
// own space (DebugSpace) and draws them like the scripts' debug lines (3D: hidden parts faint). Not in
// the game view.
inline constexpr char kSceneLinesPoint[] = "rm_engine.game_production.scene_lines";

struct SceneLinesInfo {
    // Adds the lines of the selected entities (`all`: of every entity) to `out` (seconds: 0).
    // The view's toolbar has the switch 形: 選択 / 全部 (`all`).
    void (*lines)(const Scene& scene, std::span<const int> selection, bool all, DebugDraw& out);
};

}  // namespace rm::game_production
