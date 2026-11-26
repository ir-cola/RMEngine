#pragma once
// シーンビューの枠。ゲーム制作がパネルを出す。中に描くもの、そしてカメラの動かし方は、空間を
// 知るプラグインが持つ (コネクター ゲーム制作 × 3D が 3D で描く)。ゲーム制作自身は 3D も 2D も
// 知らない (設計 RME_PLATFORM 4-f)。
//
//   // コネクターの RegisterEditor で:
//   static const game_production::SceneViewInfo kView{&CreateMySceneView};
//   registry.Contribute(game_production::kSceneViewPoint, kView);
//
// シーンビューはそれぞれ 1 つの空間 (`space`: "3d"、"2d" - ゲーム制作にとってはただの名前) に属す。
// ゲームは 1 つを選ぶ (GameSettings::space - プロジェクトのデータ: ゲームビューと新しいエンティティが
// それに従う)。2 つ以上あるときは、ゲームの設定 ▸ ゲーム設定 に「ゲームの空間」の選択が出る (編集: Undo に
// 積み、未保存になる)。シーンパネルの上の帯はビューが自分で描く (3D: 3D / 2D の見え方と、ギズモ・影 ...)。まだ選んでいない (または選んだ空間のプラグインがない) ときは、`order` が一番小さいもの。
// 1 つもなければ、パネルがどのプラグインが持ってくるかを表示する。

#include "core/reflect/FieldCommand.h"
#include "editor/plugin/Software.h"
#include "rm_engine/game_production/runtime/DebugDraw.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace rm::game_production {

// PluginRegistry の口: 中身は SceneViewInfo (static)。
inline constexpr char kSceneViewPoint[] = "rm_engine.game_production.scene_view";

// シーンビューが描かれるたびに受け取るもの。
struct SceneViewHost {
    editor::SoftwareHost& host;                     // GPU、履歴など
    const Scene& scene;
    std::span<const int> selection;                 // 選択中のエンティティ (階層のもの)
    std::function<void(std::vector<int>)> select;   // 代わりにこれらを選ぶ (空: 何も選ばない)
    // エンティティの部品がある場所 (呼ぶたびに探し直す - Undo でシーンが作り直されることがあるため)。
    // 編集するときは、これを通して値を変え、これを使った reflect::SetFieldCommand を
    // host.History() に積む (編集 = Undo の 1 件 = 未保存)。
    std::function<std::function<reflect::EditTarget()>(int id, const std::string& type)> component;
    // Play 中: `scene` は Play 中のコピー (毎フレーム変わり、編集しない - ギズモなし)。
    bool playing = false;
    // Play 中のゲームの時間の進む速さ (rm::SetTimeScale。一時停止中は 0): 絵の中で自分で動くもの
    // (パーティクル) は、絵の時間にこれをかけた分だけ動く。
    float timeScale = 1;
    // ゲームビュー: ゲームの絵だけ - グリッド・ツール・選択なし。ゲームのカメラ (`camera`) を通して見え、
    // その後ろにはゲーム設定の 背景の色 が出る (カメラが自分の色を持つときは除く)。
    bool game = false;
    Color background{0, 0, 0, 1};
    // ゲーム設定の 最初のカメラ: ゲームがそれを通して見るエンティティ (0: 未選択)。何がエンティティを
    // カメラにするかはビューごとの事情 (3D のカメラ部品など)。
    uint64_t camera = 0;
    // ゲームの画面の形 (ゲーム設定の 画面: 幅 / 高さ)。ゲームビューはこの中に描く (パネルがそれより
    // 広い・高いときは周りに帯が出る)。シーンビューのカメラのプレビューも同じ。
    float aspect = 16.f / 9.f;
    // ゲーム設定をインスペクターに出す (ゲームビューが 最初のカメラ を選ぶための手段)。
    std::function<void()> openSettings;
    // Play 中: スクリプトが rm::Debug (DebugDraw.h) で描いた線。各ビューは自分の空間の線を描く
    // (シーンビューはツールバーの デバッグ がオンのときだけ)。Play 中でないときは null。
    const DebugDraw* debug = nullptr;
    // ゲーム設定 (読み取り専用: 画面のサイズとセーフエリア - UI ビュー用)。
    const GameSettings* settings = nullptr;
    // シーンの形を変える (ビューがエンティティを作ったり動かしたりする: UI ビューは絵を置き、UI の
    // ものに別の親をつける): `touched` は変更で変わるエンティティ (その下にあるものも含む。作られる
    // ものは自動で見つかる - SceneRecord)。Undo は `label` という名前の 1 件。`change` は、変更の後に
    // 選ぶエンティティを返す (空: 選択はそのまま)。Play 中は何もしない。
    std::function<void(std::vector<int> touched, std::string label, std::function<std::vector<int>(Scene&)> change)> editScene;
    // シーン自身の値 (SceneSettingInfo: 型の TypeDesc::name) がある場所 (呼ぶたびに探し直す)。いつも
    // 編集するシーン (Play 中でも Play 前のもの)。編集するときは `component` と同じく、これを使った
    // reflect::SetFieldCommand を host.History() に積む。Play 中は編集しない (`scene` は Play 中のコピー)。
    std::function<std::function<reflect::EditTarget()>(const std::string& type)> setting;
    // ゲームの空間 (SceneViewInfo::space: "3d"、"2d"。空間のプラグインが無ければ空)。
    std::string space;

    // 真上から見た地図 (気候のタブ): あると、3D のビューはカメラの代わりに、シーンを真上からまっすぐ
    // 見下ろした絵 (正射影: 遠くても小さくならない) を、ゲームの見た目 (グリッド・ツール・ギズモ・印・
    // 選択の枠なし) で描く。空・天候の粒は描かない。3D では (x, z) が地図の (横, 縦): 画面の右が +X、
    // 画面の上が +Z (左手系で上から見るので、+Z を下にすると鏡に映った絵になる)。`center` が r の
    // 真ん中に、r の幅がちょうど 2 x `halfWidth` m に映る。
    struct Overhead {
        Vec2 center{0, 0};
        float halfWidth = 50;
    };
    std::optional<Overhead> overhead;
};

// 開いているゲームとビュー (シーンビュー、ゲームビュー) ごとに 1 つ: 自分のカメラを持つ。
class SceneView {
public:
    virtual ~SceneView() = default;
    virtual void Draw(ui::Context& c, const ui::Rect& r, SceneViewHost& host) = 0;
    // 別の空間のゲームビュー (ゲーム自身の空間が先に描く): この空間の絵を、背景を塗らずにその上に
    // 重ねる (2D を 3D の上に: HUD)。ビューに、そのように出せるものがあるとき。`host.game` は true。
    // 既定: 何もしない。
    virtual void DrawLayer(ui::Context& c, const ui::Rect& r, SceneViewHost& host) { (void)c, (void)r, (void)host; }
    // ユーザーの見方 (カメラ、ツールなど): パネルのレイアウトと一緒に保存し、プロジェクトには入れない。
    virtual void LoadState(const json::Value& state) { (void)state; }
    virtual json::Value SaveState() const { return {}; }
};

// パネルの中のゲーム画面の長方形: `aspect` (幅 / 高さ) の最大のものを、真ん中に取る (ゲームビューは
// そこにゲームを描き、周りに帯が出る)。全ビューで 1 か所 (憲法 2)。
inline ui::Rect GameScreenRect(const ui::Rect& panel, float aspect) {
    const float a = aspect > 1e-3f ? aspect : 1e-3f;
    float w = panel.w, h = panel.w / a;
    if (h > panel.h) {
        h = panel.h;
        w = h * a;
    }
    return {panel.x + (panel.w - w) * 0.5f, panel.y + (panel.h - h) * 0.5f, w, h};
}

// PluginRegistry の口: 中身は ViewPanelInfo (static)。プラグインが足す、ゲームソフトのパネル
// (ゲーム UI: UI ビュー)。シーンビューと同じ SceneViewHost を受け取る (game = false)。空間には
// 属さず、ゲームの空間が何であっても出る。
inline constexpr char kViewPanelPoint[] = "rm_engine.game_production.view_panel";
struct ViewPanelInfo {
    const char* id = "";             // レイアウトでのパネルの id (ゲームのパネルの中で一意)
    const wchar_t* title = nullptr;  // TrKey の文字列
    std::unique_ptr<SceneView> (*create)() = nullptr;
};

// PluginRegistry の口: 中身は CreateItemInfo (static)。プラグインが階層の右クリックメニュー
// 追加 ▸ に足す行 (ゲーム UI: UI ▸ キャンバス / ボタン ...、ゲーム制作 × 3D: 3D オブジェクト ▸ 箱 / 球 ...)。
// それぞれ自分のフォルダに入り、フォルダは決まった順 (GameSoftware)、その中はプラグインの順。
// エンティティの作り方は 2 通り:
//   make   `scene` の `parent` の下の `index` に、エンティティ (とその下のもの) を作って返す (0:
//          何も作らない) - 名前と部品はこれが決める (3D の 箱 は、ゲームの空間が何でも
//          3D の Transform を持つ);
//   fill   空のエンティティ (`label` という名前、ゲームの空間の Transform つき) と同じように作った
//          エンティティに足す - どの空間でも使えるもの (パーティクル、音源) 用。
// Undo の 1 件になり、その後に選択されて、名前を入力できる状態になる。
inline constexpr char kCreateItemPoint[] = "rm_engine.game_production.create_item";
struct CreateItemInfo {
    const wchar_t* label = nullptr;   // TrKey の文字列: それが何か (名詞: Undo の名前は 「箱」を作成)
    const wchar_t* folder = nullptr;  // TrKey の文字列 ("3D オブジェクト"。フォルダの中のフォルダは "/" で区切る)。null: 追加 そのもの
    int (*make)(Scene& scene, int parent, size_t index) = nullptr;
    void (*fill)(Scene& scene, int entity) = nullptr;
    bool child = false;  // 選択中のエンティティの下に作る (そうでなければ - 何も選んでいなくても - 選択の後ろに)
    // 何を作るかを、足すプラグイン (kCreatedPoint) 向けに平たく言ったもの: "box"、"sphere"、
    // "cylinder"、"capsule"、"plane"、"cone"、"pyramid" (1 ユニットの 3D の形。plane は 10 x 10) - null: 言うことなし。
    const char* made = nullptr;
};

// PluginRegistry の口: 中身は CreatedInfo (static)。追加 ▸ がエンティティを作った後、同じ Undo の
// 1 件の中で、それぞれが足してよい (物理: 3D オブジェクトの形のコライダー - 両方を知るプラグイン)。
inline constexpr char kCreatedPoint[] = "rm_engine.game_production.created";
struct CreatedInfo {
    void (*added)(Scene& scene, int entity, std::string_view made) = nullptr;  // `made`: CreateItemInfo::made (なければ "")
};

// エンティティ `id` に足した型 T の部品 (2 つ持てない型で、すでにあるときはそれ。null: 今はその型を
// 出していない)。CreateItemInfo の make / fill 用。
template <class T>
T* AddComponentTo(Scene& scene, int id) {
    scene.AddComponent(id, reflect::TypeOf<T>());
    SceneEntity* e = scene.Find(id);
    Component* c = e ? e->FindComponent(reflect::TypeOf<T>().name) : nullptr;
    return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
}

struct SceneViewInfo {
    std::unique_ptr<SceneView> (*create)();
    const char* space = "";              // 例: "3d" (ComponentInfo::space も同じものを指す)
    const wchar_t* label = nullptr;      // 例: 3D (TrKey の文字列: 表示するときに翻訳する)
    int order = 0;                       // 小さいほうが既定
};

// PluginRegistry の口: 中身は SceneLinesInfo (static)。プラグインが、編集中も Play 中も、シーンビューの
// シーンの上に出す線 (物理: コライダーの形)。各ビューは自分の空間 (DebugSpace) の線を取り、スクリプトの
// デバッグ線と同じように描く (3D: 隠れた部分は薄く)。ゲームビューには出ない。
inline constexpr char kSceneLinesPoint[] = "rm_engine.game_production.scene_lines";

struct SceneLinesInfo {
    // 選択中のエンティティ (`all` のときは全エンティティ) の線を `out` に足す (seconds: 0)。
    // ビューのツールバーに 形: 選択 / 全部 (`all`) のスイッチがある。
    void (*lines)(const Scene& scene, std::span<const int> selection, bool all, DebugDraw& out);
};

}  // namespace rm::game_production
