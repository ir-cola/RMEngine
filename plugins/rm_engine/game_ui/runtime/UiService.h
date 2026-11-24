#pragma once
// ゲームが動く間の ゲーム UI (ゲーム制作の PlayService: エディターの Play と書き出したゲームは同じように
// 動かす)。毎フレーム、スクリプトとシステムの後に:
//
//   マウス         ボタンの上: 乗った。左ボタンを押す: 押した。同じ所で離す: OnClick
//   キー / パッド  どのゲームでも標準で、設定は要らない: キーとパッドで選ぶ を持つ一番手前の
//                  ウィンドウ (キャンバス) には、いつも選ばれたボタンがある (最初に選ぶボタン、
//                  なければ左上のもの) - 最初から表示され、マウスを使う間は隠れる。UIUp / UIDown /
//                  UILeft / UIRight (矢印キー、パッドの十字、左スティック) はその向きの一番近い
//                  ボタンへ 1 歩動き (NextButton)、押しっぱなしなら動き続ける。UISubmit は選ばれた
//                  ボタンを押す。UICancel はウィンドウの 戻るで押すボタン を押す。なければ最後に
//                  開いたウィンドウを閉じ、隠していたものを再び表示する (スクリプトの Show /
//                  SwitchTo、またはオンにしたキャンバス)。
//   押した         その 押したときの音、次に 押したときの動作 を順に (RunActions: ウィンドウ、
//                  シーン、変数、音、ゲームの終了)、次にスクリプトの OnClick
//   そのあと       すべてのボタンの状態 (UiButton::state / focused: 保存しない) と、その見た目を状態の
//                  ものへなめらかに近づけたもの (UiButton::now: 変わる速さ)、描画用。ボタンの音
//                  (乗った / 押した: 音を鳴らすサービスが何であれ - PlayServices::Sounds)。そして
//                  押されたボタンのエンティティのスクリプト用の OnClick フック (PlayFrame::hooks)
//
// キーとパッドはゲーム設定の 入力 を通してだけ読む (ゲーム制作のアクション。ない場合はその標準の
// もの) - キーやパッドそのものは決して読まない: 割り当てを変えれば (または別の種類のコントローラー
// でも) UI のも変わる。

#include "rm_engine/game_production/runtime/Globals.h"
#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_ui/runtime/UiControls.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"
#include "rm_engine/game_ui/runtime/UiMotion.h"

#include <unordered_map>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rm::game_ui {

inline constexpr char kUiServiceName[] = "rm_engine.game_ui";
inline constexpr float kRepeatDelay = 0.4f;  // この時間押しっぱなしの向きは ...
inline constexpr float kRepeatEvery = 0.12f;  // ... この間隔でまた 1 歩進む

// このフレームのアクションが言っていること (純粋: テストは自分のフレームを渡す)。
struct UiKeys {
    int dx = 0, dy = 0;          // このフレームに押された向き (画面 Y は下向き。強いほうの向きだけ)
    int holdDx = 0, holdDy = 0;  // 今押しっぱなしの向き
    bool submit = false;         // UISubmit が押された
    bool cancel = false;         // UICancel が押された
};
UiKeys ReadUiKeys(std::span<const game_production::InputAction> actions, const game_production::InputFrame& now,
                  const game_production::InputFrame& before);

// ほかのものの上に (または代わりに) 表示されたウィンドウ: 戻る が閉じるもの、そして再び表示するもの。
struct UiVisit {
    int window = 0;
    std::vector<int> hid;
};

// 入力の部品の値が変わった (スクリプトの OnEvent に、そのエンティティへ届く。名前は RMUI.h の
// rm::UI::kChanged / kSubmitted と同じ)。
inline constexpr char kChangedEvent[] = "UIChanged";
inline constexpr char kSubmittedEvent[] = "UISubmitted";
struct UiChange {
    int entity = 0;
    bool submitted = false;  // 入力欄で Enter (でなければ値が変わった)
    double number = 0;       // スライダーの値・トグルの 1 / 0
    std::string text;        // 入力欄の文字
};

// 1 回の Play のフォーカス、マウス、ウィンドウ (フレーム間で保つ)。
struct UiFocus {
    int typing = 0;              // 文字を打っている入力欄 (0: なし。その間キーとパッドで選ぶのは止まる)
    std::string typedBefore;     // 打ち始めたときの文字 (Esc で戻す)
    int scrolling = 0;           // マウスで動かしているスクロール (0: なし)
    bool thumb = false;          // ... そのスクロールバーのつまみを (縦なら thumbVertical)
    bool thumbVertical = true;
    Vec2 dragFrom{0, 0}, offsetFrom{0, 0};  // つかんだ所 (スクロールの単位) と、そのときの offset
    int shownFocus = 0;          // スクロールに見せた選んだボタン (変わったら見せ直す)
    PointerState pointer;
    int window = 0;          // 前のフレームでキーが効いたウィンドウ
    int focus = 0;           // その選ばれたボタン
    bool keyboard = true;    // 選択を表示している (マウスを使う間は隠す)
    bool scriptChose = false;  // 前のフレームから、スクリプトが `focus` を選んだ (そのウィンドウが手前に来ても保つ)
    std::optional<Vec2> lastPointer;
    int heldDx = 0, heldDy = 0;  // 押しっぱなしの向き: 繰り返す
    float heldFor = 0, repeatAt = 0;
    bool started = false;        // 最初に表示されたウィンドウは分かっている (どれも 戻る では閉じない)
    std::vector<int> shown;      // 前のフレームに表示されていたウィンドウ
    std::vector<UiVisit> visits; // それ以降に開いたウィンドウ、古いものが先
};

// 1 フレームがしたこと。
struct UiStep {
    std::vector<int> clicked;  // 押されたボタンを順に (OnClick)
    std::vector<int> hovered;  // このフレームに 乗った (または押された、選ばれた) ボタン: その 乗ったときの音
    int closed = 0;            // 戻る で閉じたウィンドウ (0: なし)
    std::vector<UiChange> changes;  // 値が変わった入力の部品 (順に)
    int typing = 0;            // このフレームの後に文字を打っている入力欄 (ホストは IME をつなぐ)
    UiBox typingBox;           // その欄の画面の箱 (DIP。IME の候補の窓をその下に出す)
};

// 1 フレーム (純粋: シーンのボタンに状態が付き、戻る で閉じたウィンドウはオフになり、それが
// 隠していたものは再びオンになる)。入力の部品: トグルは押すと入れ替わり、スライダーはドラッグとキー、
// スクロールはホイール (now->wheel)・ドラッグ・選んだ物を見せる、入力欄は押すと打ち始め、`typed` を
// 打つ (`clip`: Ctrl+C / V)。打っている間は、キーとパッドでボタンを選ばない (戻るは打つのをやめる)。
UiStep StepUi(game_production::Scene& scene, const UiTree& tree, std::optional<Vec2> pointer, bool down, bool wentDown, bool wentUp,
              std::span<const game_production::InputAction> actions, const game_production::InputFrame* now,
              const game_production::InputFrame* before, float dt, UiFocus& state, const ui::TypedText* typed = nullptr,
              const UiClipboard* clip = nullptr);

// 入力欄 `entity` に打ち始める (カーソルは最後。前に打っていた欄はやめる)。False: 押せる入力欄ではない。
bool StartTyping(game_production::Scene& scene, UiFocus& state, int entity);
// 打つのをやめる (`restore`: 打ち始める前の文字に戻す - 戻したら *change に入れる)。
void StopTyping(game_production::Scene& scene, UiFocus& state, bool restore, std::vector<UiChange>* changes = nullptr);

// --- 動き (UI-4): 出る・消える動き、道、スクリプトのトゥイーン -----------------------------------
// スクリプトのトゥイーン (rm::UI::MoveTo ...): 頼まれたときの所から行く先まで、`seconds` かけて。
struct UiTween {
    bool active = false;
    Vec2 from{0, 0}, to{0, 0};  // (値 1 つ: x)
    float time = 0, seconds = 0;
    UiEase ease = UiEase::Out;
};
struct UiTweens {
    UiTween move, scale, turn, fade;  // 位置、大きさ、回転、透明度
    float scaleNow = 1, alphaNow = 1;  // ScaleTo / FadeTo が残したもの (終わった後も保つ)
};
// ゲームが動く間に動くもので、フレーム間で保つ (UI サービスのもの。シーンの切り替えで新しく作る)。
struct UiMotionWorld {
    bool started = false;  // 最初のフレームを進めた (ゲームの始めにも)
    std::unordered_map<int, UiTweens> tweens;  // エンティティごと
};
// `scene` のすべての動きの 1 フレーム: 各 出る・消える動き はエンティティのオン・オフに合わせて
// 始まり / 進み / 終わり、各 道 は歩き、各トゥイーンは進む - そのあと UiLive (UI の配置の、
// ウィンドウの UiCanvas の) をレイアウト用に書く。
void StepMotion(game_production::Scene& scene, float dt, UiMotionWorld& world);
// 頼まれたトゥイーン (seconds <= 0: すぐに)。False: エンティティに UI の配置がない。
enum class UiTweenKind { Move, Scale, Turn, Fade };
bool StartTween(game_production::Scene& scene, UiMotionWorld& world, int entity, UiTweenKind kind, Vec2 to, float seconds, UiEase ease);
// 道: 1 は今の所から続けて再生、0 は止める、2 は最初から再生。False: 道に沿って動く がない。
bool PlayPath(game_production::Scene& scene, int entity, int how);

// --- ウィンドウ (キャンバス): ボタンのアクションとスクリプトの rm::UI が共有する ---------------
// ウィンドウ: 別のキャンバスの中にないキャンバス (中にあるものはそのウィンドウの一部)。
bool IsWindow(const game_production::Scene& scene, int entity);
// ウィンドウのうち `window` だけを表示する (ほかはすべて隠す。その中身はそのまま)。
void SwitchToWindow(game_production::Scene& scene, int window);
// 戻る: `window` を隠し、それが隠していたものを再び表示する。それがほかのものの上に開かれて
// いたとき (state.visits)。そうでなければ false (何も変わらない) - 最初のウィンドウはこれでは閉じない。
bool CloseWindow(game_production::Scene& scene, UiFocus& state, int window);

// --- 押したときの動作 -------------------------------------------------------------------------
// アクションが届くもの: ゲームのもの (サービスが PlayFrame::world とホストから渡す)、テストなら
// その専用のもの。空のままなら、その種類のアクションは何もしない (知らせる)。
struct UiActionHost {
    game_production::GlobalStore* globals = nullptr;
    std::function<bool(const AssetRef& scene)> loadScene;           // 頼まれる。フレームの間に切り替える (false: ゲームのシーンではない)
    std::function<void()> quit;                                    // 頼まれる。このフレームの後でゲームが終わる
    std::function<void(const AssetRef& sound, float volume)> playSound;
    std::function<void(int level, std::string_view text)> log;
};
// `a` の言うとおりに変えたグローバル変数 (変え方、数、下限 / 上限)。そうできないときは false (*why)
// (テキスト、Vec2 - または ON / OFF への 足す)。
bool ChangeVariable(game_production::GlobalValue& v, const UiAction& a, std::string* why);
// ボタン `button` (ウィンドウ `window` の) のアクションを順に。
void RunActions(game_production::Scene& scene, int button, int window, const UiButton& b, UiFocus& state, const UiActionHost& host);

class UiService final : public game_production::PlayService {
public:
    explicit UiService(const game_production::PlayHost& host) : host_(host) {}
    void Frame(const game_production::PlayFrame& f) override;
    void SceneSwitched() override {
        state_ = {};
        motion_ = {};
    }
    UiMotionWorld& Motion() { return motion_; }
    // いまのフォーカス、ウィンドウ、マウス (エディターの デバッグ パネル)。
    const UiFocus& State() const { return state_; }
    UiFocus& MutableState() { return state_; }  // (スクリプトの rm::UI::StartTyping)
    // スクリプトがボタンを選んだ (rm::UI::Focus): マウスが動くまで選ばれたものとして表示する。
    void Focus(int entity) {
        state_.focus = entity;
        state_.keyboard = true;
        state_.scriptChose = true;
    }

private:
    const game_production::PlayHost& host_;
    UiFocus state_;
    UiMotionWorld motion_;
};

std::unique_ptr<game_production::PlayService> CreateUiService(const game_production::PlayHost& host);

// ゲームの画面とそのセーフエリア、ゲーム設定から。
UiScreen ScreenOf(const game_production::GameSettings& settings, Vec2 size);

}  // namespace rm::game_ui
