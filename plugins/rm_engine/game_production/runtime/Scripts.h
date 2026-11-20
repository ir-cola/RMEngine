#pragma once
// C++ スクリプト (ゲームの動き): DLL から読み込み、エンジンからは reflect の型として見え、ゲームが
// Play される間に動く。ランタイムの部分: 書き出したゲームも、エディターの Play と同じコードを使う
// (設計 TestEngine NewEngineBluePrint 5 / 6 / 6-b / 20-c、RME_PLATFORM 4-b)。
//
//   ホスト (エディター / ゲーム)                このファイル
//     ScriptModule::Load(dll, api)          ─▶  LoadLibrary、RmModuleInit、ABI の確認、どの
//                                                スクリプトも reflect::TypeDesc にする (MakeScriptTypes)
//     scene.SetComponentTypes(... + Types())     スクリプトはエンティティの部品: インスペクター、
//                                                保存、Undo、コピーが、ほかの部品と同じに働く
//     ScriptRunner::Frame(scene, world, dt)  ─▶  新しいものの BeginPlay、そのあと型ごとに Tick
//     ScriptRunner::Stop(scene, world)       ─▶  EndPlay
//
// どの DLL を使うかはホストの仕事: エディターは <project>/Library/Scripts/ の中で一番新しくビルド
// されたもの (LatestScriptDll) を読み、書き出したゲームは exe の隣の Scripts.dll を読む。
//
// どのフックも __try / __except の中で動く (ScriptGuard.cpp): 落ちたスクリプトだけが止まり、コンソールに
// 1 回知らせる。ホストは動き続ける。

#include "base/Json.h"
#include "core/RuntimeRegistry.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/DebugDraw.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Globals.h"
#include "rm_engine/game_production/runtime/Input.h"
#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_production/runtime/sdk/RmAbi.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rm::game_production {

// --- API のうち、プラグインの部品 (RmApi::part) ------------------------------------------------
// RuntimeRegistry の口: 項目は ScriptApiPart (static)。つなぎ役が、空間を知っているものを渡す:
// 「ゲーム制作 × 3D」は 3D の Transform (RM3D.h)、「ゲーム制作 × 2D」は 2D の Transform (RM2D.h)。
inline constexpr char kScriptApiPoint[] = "rm_engine.game_production.script_api";
struct ScriptApiPart {
    const char* name;  // 例: "rm_engine.game_x_3d.transform"
    uint32_t version;
    const void* table;  // その部品の関数の C 構造体。RmApi::ctx = ScriptWorld* を受け取る
};

// スクリプトのコルーチン (rm::Task: フレームは DLL のもの) と、いつ続きを動かすか (RmCoroutinesApi)。
struct CoroutineSlot {
    uint64_t id = 0;
    void* frame = nullptr;
    int (*resume)(void* frame, RmWait* next) = nullptr;
    void (*destroy)(void* frame) = nullptr;
    const void* owner = nullptr;  // 始めたスクリプト (null: なし。ゲームの終わりにだけ止まる)
    int ownerEntity = 0;
    RmWait wait{};
    uint64_t waitFrom = 0;  // 待ちが始まったフレーム (待ちは早くても後のフレームで終わる)
    double until = 0;       // RmWait_Seconds: ゲーム内の時間。RmWait_RealSeconds: 実際の時間
    bool dead = false;      // 終わった、または止めた (パスの後で取り除く)
    bool running = false;   // 今 resume の中にいる (その間に止めると、戻った後で解放する)
};

// スクリプトが送ったイベント (rm::Events)。フレームの Tick の後に届ける (ScriptRunner::DeliverEvents)。
struct QueuedEvent {
    std::string name;
    int sender = 0;
    double number = 0;
    std::string text;
    int entity = 0;
    uint32_t scope = 0;  // 0 すべてのエンティティ、1 対象だけ、2 対象とその下
    int target = 0;
};

// スクリプトが動いている間に見えるもの: RmApi::ctx はこれを指す。
struct ScriptWorld {
    Scene* scene = nullptr;  // Play しているシーン (エディターではコピー)
    float dt = 0;            // このフレームの秒数。時間の倍率を掛けた後 (Tick の dt)
    double time = 0;         // このフレームより前のゲーム内の秒数 (時間の倍率を掛けた後)
    // 時間の倍率 (rm::SetTimeScale): ホストはフレームの実際の秒数にこれを掛けて、スクリプト、システム、
    // 描くもの (パーティクル) に渡す。実際の値は unscaledDt / unscaledTime。frame: このフレームより
    // 前のフレーム数。
    float timeScale = 1;
    float unscaledDt = 0;
    double unscaledTime = 0;
    uint64_t frame = 0;
    // ゲーム画面上のマウス (ゲーム設定の 画面、DIP): ホストがスクリプトの実行前に毎フレーム設定する
    // (SetPointer)。nullopt: ゲームの上にない。
    std::optional<Vec2> pointer;
    Vec2 lastPointer{0, 0};
    Vec2 pointerDelta{0, 0};
    Vec2 screen{1280, 720};
    // プラグイン (世界での位置を保つ Entity::SetParent が空間に尋ねる: PlaceInfo::keepWorld)。null: なし。
    const RuntimeRegistry* registry = nullptr;
    // ゲーム設定のレイヤー名 (rm::LayerIndex: 1、2 ... はこの順)。null: なし。
    const std::vector<std::string>* layers = nullptr;
    uint64_t camera = 0;  // SystemContext::camera: ゲームのカメラ。スクリプトでも切り替えられる
    // ホストの入力。1 フレームに 1 回読む (どちらのホストも InputReader: ウィンドウのどれかが前にある間だけ。
    // テストは自分のものを渡す)。空: 何も押されていない。
    std::function<InputFrame()> readInput;
    // ゲーム設定の 入力 (ホストの GameSettings::input。ワールドが使われる間は生きている)。
    const std::vector<InputAction>* actions = nullptr;
    // ... プレイヤーが変えたもの (rm::Input::SetBinding、input.json から読む)。nullopt: 設定のもの。
    std::optional<std::vector<InputAction>> playerActions;
    // 誰がキーボードとマウスを持つか (0 = 1P: ゲーム設定の キーボードとマウスのプレイヤー - 1)。
    int keyboardPlayer = 0;
    // プレイヤーのパッドを震わせる (ホストの InputReader::Vibrate)。空: 震えない。
    std::function<void(int player, float low, float high, float seconds)> vibrate;
    // スクリプトのメッセージ (レベル 0 情報 / 1 警告 / 2 エラー)。
    std::function<void(int level, std::string_view text)> log;
    // API のうち、プラグインの部品 (kScriptApiPoint で渡されたもの: ScriptApiParts)。
    std::vector<ScriptApiPart> parts;
    // rm::Debug の行の行き先: Play 中はエディターのバッファー (DebugDraw.h)。null (書き出したゲーム) の
    // ときは、デバッグの部品は何もしない。
    DebugDraw* debug = nullptr;
    // ゲームのシーン (Game::LoadScene): 一覧の順の名前、Play 中のもの (-1: 一覧にない) と、このフレームに
    // 頼まれたもの (-1: なし。フレームの間に切り替える: SwitchAskedScene)。
    std::vector<std::string> sceneNames;
    // それらのアセット id。同じ順 (わからないものは "")。アセットとして選んだシーン (ゲーム UI のボタンの
    // シーンを切り替える) は id で探す。別のフォルダに同じ名前のシーンが 2 つあれば、2 つのシーンになる。
    std::vector<std::string> sceneIds;
    int sceneIndex = -1;
    int sceneAsked = -1;
    // ゲームが終了を頼んだ (ゲーム UI のボタンの ゲームを終わる): ホストはこのフレームの後で終える
    // (書き出したゲームはウィンドウを閉じ、エディターの Play は ■ のように止まる)。
    bool quitAsked = false;
    InputFrame inputNow, inputBefore;
    std::unordered_set<std::string> told;  // 頼まれたが無い名前 (それぞれ 1 回だけ知らせる)
    // ゲームのグローバル変数 (ゲーム全体で 1 つの置き場: シーンを切り替えても残る。ホストは Play / Stop /
    // ゲームの開始時に、設定からこれを Reset する) と、rm::Save がそれを書く場所
    // (空: rm::Save は何もしない)。
    GlobalStore globals;
    std::filesystem::path saveFolder;
    // rm::Spawn(prefab): Prefab のファイル (ホストのもの: エディターは Assets、ゲームは .rmpak)。空:
    // Prefab を出しても何も出ない。
    PrefabReader prefab;
    // ゲームが Play される間にプラグインが持つもの (音 ...: PlayServices.h)。ホストのもの。無いとき
    // (テスト) は null: それを使う部品は何もしない。
    PlayServices* services = nullptr;
    // フレーム中に頼まれ、フレームの終わりに行う (ScriptRunner::Frame): まず EndPlay、そのあと消す。
    std::vector<int> destroyAsked;                            // エンティティ (その下のものも)
    std::vector<std::pair<int, const void*>> removeAsked;     // 部品: そのエンティティとオブジェクト
    // コルーチン (rm::StartCoroutine) と、このフレームに送られたイベント (rm::Events)。
    std::vector<CoroutineSlot> coroutines;
    uint64_t nextCoroutine = 1;
    bool inCoroutinePass = false;
    std::vector<QueuedEvent> events;
    // 今フックを動かしている (またはコルーチンが動いている) スクリプトと、そのエンティティ (null / 0: なし)。
    // 今始めたコルーチンの持ち主と、今送ったイベントの送り主になる。
    const void* currentScript = nullptr;
    int currentEntity = 0;

    // 入力を 1 フレームに 1 回読む (押した / 離したは前のフレームと比べる)。
    void ReadInput();
    // このフレームのマウス (ホストのもの: ゲーム画面の上にあるかどうか) と画面の大きさ。
    void SetPointer(std::optional<Vec2> at, Vec2 screenSize);
    // 使っている入力アクション: プレイヤーのもの (playerActions) か、ゲーム設定のもの。
    const std::vector<InputAction>& Actions() const;
    // プレイヤーの割り当てを <saveFolder>/input.json から読む (無ければ設定のもの)。ファイルがあるのに
    // 読めないときは false と *why。Save がそれを書く。
    bool LoadBindings(std::string* why = nullptr);
    bool SaveBindings(std::string* why = nullptr);
    std::filesystem::path BindingsFile() const;
    // コルーチン: 1 つ始める (最初の待ちまで今すぐ動かす。id を返す。すぐ終わったら 0)、1 つ止める /
    // スクリプトのすべてを止める / 全部止める (スクリプトの DLL を手放す前)。ResumeCoroutines: Tick の後に、
    // 待ちが終わったコルーチンすべてを続きへ進める。
    uint64_t StartCoroutine(void* frame, int (*resume)(void*, RmWait*), void (*destroy)(void*));
    void StopCoroutine(uint64_t id);
    void StopCoroutinesOf(const void* owner);
    void ClearCoroutines();
    void ResumeCoroutines();
    bool CoroutineRunning(uint64_t id) const;
    // ゲーム設定のアクションを名前で。無ければ null (1 回だけ知らせる)。
    const InputAction* Action(const char* name);
    // グローバル変数を名前で。無ければ null (1 回だけ知らせる)。
    GlobalValue* Global(const char* name);
    // スクリプトの間違いを 1 回だけ知らせる (コンソールに警告): `key` でどれかを区別する。
    void TellOnce(const std::string& key, std::string_view text);
};

// ワールド用のエンジンの表: RmApi::ctx がワールド (表が使われる間は生きていること)。
RmApi MakeScriptApi(ScriptWorld& world);
const void* FindScriptApiPart(std::span<const ScriptApiPart> parts, const char* name, uint32_t version);
// プラグインが渡したもの (RuntimeRegistry: エディターの PluginRegistry、またはゲームのもの)。
std::vector<ScriptApiPart> ScriptApiParts(const RuntimeRegistry& registry);

// --- リフレクトされた型としてのモジュールのスクリプト (純粋: テストは手で作ったモジュールを渡す) ----
struct ScriptType {
    reflect::TypeDesc desc{};
    std::vector<reflect::FieldDesc> fields;
    const RmScriptDesc* script = nullptr;  // そのフック (DLL の中)
    reflect::StringAccess strings{};        // その std::string の欄: DLL を通して (RmModule::stringGet / stringSet)
    reflect::StringAccess assets{};         // そのアセットの欄 (char guid[RM_ASSET_ID_SIZE]: ただのバイト列)
    // 欄に必要なもの。ここが持つ (アドレスは変わらない): RM_STRUCT、RM_ENUM、リスト (DLL の関数で扱う)。
    struct Nested {
        reflect::TypeDesc desc{};
        std::vector<reflect::FieldDesc> fields;
    };
    struct EnumType {
        reflect::EnumDesc desc{};
        std::vector<reflect::EnumItem> items;
    };
    std::unordered_map<const RmStructDesc*, std::unique_ptr<Nested>> structs;
    std::unordered_map<const RmEnumDesc*, std::unique_ptr<EnumType>> enums;
    std::vector<std::unique_ptr<reflect::ArrayOps>> lists;
    // その TypeDesc が DLL の構築 / 破棄 / コピーのために使う、ガード付きの代役 (そこで落ちても止まるのは
    // そのオブジェクトだけで、エディターやゲームではない)。消えるときに返す。
    std::vector<size_t> guards;
    ScriptType() = default;
    ScriptType(const ScriptType&) = delete;
    ScriptType& operator=(const ScriptType&) = delete;
    ~ScriptType();
};
// このエンジンは Debug ビルド (スクリプトも Debug でなければならない: RmModuleFlag_Debug)。
#ifdef _DEBUG
inline constexpr bool kEngineDebug = true;
#else
inline constexpr bool kEngineDebug = false;
#endif
// モジュールが使えない理由 (空 = 問題なし): ABI が違う、Release のエンジンに Debug ビルド (その逆も)、
// 文字列の関数が無い、アラインメントが厳しすぎる型、このバージョンが知らない種類の欄 (または説明の無い
// enum / 構造体 / リスト)、同じ名前のスクリプトが 2 つ。
std::string CheckModule(const RmModule* module, uint32_t engineAbi = RM_ABI_VERSION, bool engineDebug = kEngineDebug);
std::vector<std::unique_ptr<ScriptType>> MakeScriptTypes(const RmModule& module);

// --- 読み込んだスクリプトの DLL -----------------------------------------------------------------
class ScriptModule {
public:
    ScriptModule() = default;
    ScriptModule(const ScriptModule&) = delete;
    ScriptModule& operator=(const ScriptModule&) = delete;
    ~ScriptModule() { Unload(); }

    // `dll` を読み込み、そのスクリプトを読む。使えないときは理由 (日本語) を付けて false。
    bool Load(const std::filesystem::path& dll, const RmApi& api, std::string* why);
    // まず、これらの型のオブジェクトをすべて消しておくこと (そのコードは DLL の中にある)。
    void Unload();
    bool Loaded() const { return module_ != nullptr; }
    const std::filesystem::path& Path() const { return path_; }
    std::vector<const reflect::TypeDesc*> Types() const;
    const ScriptType* Find(std::string_view name) const;

    // テスト用: DLL ではないモジュール (手で作ったもの)。
    void Adopt(const RmModule& module);

private:
    void* library_ = nullptr;  // HMODULE
    const RmModule* module_ = nullptr;
    std::filesystem::path path_;
    std::vector<std::unique_ptr<ScriptType>> types_;
};

// エディターが `folder` (<project>/Library/Scripts) に作った一番新しい DLL: 番号が最大の
// "Scripts_<number>.dll"。無ければ空。
std::filesystem::path LatestScriptDll(const std::filesystem::path& folder);
// "Scripts_0007.dll" -> 7 (-1: この形ではない)。
int ScriptDllNumber(std::wstring_view fileName);

// --- スクリプトを動かす順 ----------------------------------------------------------------------
// ゲーム設定の 実行順 での型の番号 (一覧に無ければ 0)。
int ScriptOrderOf(std::string_view type, std::span<const ScriptOrderEntry> order);
// 型を (番号, 名前) で並べる: 一覧にあるものを、一覧にないものの真ん中のまとまり (0、名前順) の前 /
// 後に置く。
std::vector<std::string> SortScriptTypes(std::vector<std::string> types, std::span<const ScriptOrderEntry> order);

// --- 動かす -------------------------------------------------------------------------------------
class ScriptRunner {
public:
    // 1 フレーム: 入力を読む。その後に切られた (そのエンティティか親が 有効 でなくなった) 始まった
    // スクリプトの EndPlay。まだ始まっていない動作中のスクリプト (新しい、または入れ直した) の
    // BeginPlay。そのあと動いているすべての Tick。型ごとに実行順で、型の中ではエンティティ id 順、
    // そのあとは付けた順。BeginPlay と EndPlay は必ず対で呼ぶ (エンティティごと消えたスクリプトは
    // 一緒に消える)。落ちたスクリプトは二度と動かさない。
    void Frame(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order, float dt);
    // フレーム中に頼まれたこと (Entity::Destroy、Remove<T>): 去るスクリプトの EndPlay (実行順で。始まった
    // スクリプトだけ)、そのあと外した部品と消したエンティティ。その EndPlay の中でまた頼まれたことも
    // 行う (多くても数回)。Frame は終わりにこれを行う。
    void ApplyAsked(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order);
    // このフレームにシステムが頼んだフック (SystemRunner::Hooks: OnHit、OnTriggerEnter / Exit ...)。
    // 頼まれた順に、それぞれについて、そのエンティティの有効で始まったスクリプトすべてを、スクリプトの
    // 順 (Tick と同じ) で呼ぶ。そのあと、それらが壊した / 外したものを片付ける (ApplyAsked)。
    void Deliver(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order,
                 std::span<const HookCall> calls);
    // 送られたイベント (rm::Events): 届く OnEvent を持つ、始まって有効なスクリプトそれぞれに、スクリプトの
    // 順で。その間に送られたものは続けて (多くても数回)。そのあと、それらが壊したものを片付ける。
    // Frame と Deliver は、それぞれの終わりにこれを行う。
    void DeliverEvents(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order);
    // ゲームが Play される間にスクリプトを作り直す (設計 BluePrint 20-c): 前は、シーンのどのスクリプトが
    // 始まった / 落ちたか (エンティティ、型、その型の中での位置で。オブジェクトは作り直される)。
    // 新しい型でシーンを読み直した後は、同じものをもう一度取り、始まった各スクリプトに OnReloaded を
    // 呼ぶ (実行順で。BeginPlay はもう一度呼ばない)。
    struct RunState {
        int entity = 0;
        std::string type;
        size_t nth = 0;
        bool started = false, crashed = false;
    };
    std::vector<RunState> Remember(const Scene& scene) const;
    void Resume(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order,
                std::span<const RunState> before);
    // 始まった全スクリプトの EndPlay (Stop)、そのあと全部忘れる。ワールドもシーンを指さなくなる
    // (シーンはこれから消える)。
    void Stop(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order);
    void Reset();
    size_t Crashed() const { return crashed_.size(); }

    // 最後の Frame (とその後の Deliver) でスクリプトにかかった時間。型ごとに (多い順)、Tick の後に
    // 続きを動かしたコルーチンも。頼まれている間だけ測る (デバッグ パネル)。
    struct ScriptTime {
        const ScriptType* type = nullptr;  // null: コルーチン
        double ms = 0;
        int calls = 0;
    };
    void SetTiming(bool on) { timing_ = on; }
    std::vector<ScriptTime> Times() const;

private:
    struct Instance {
        int entity;
        const ScriptType* type;
        void* data;
        bool active;  // それも親もすべて 有効
    };
    // シーンの全スクリプト (切られたものも)。実行順で。
    std::vector<Instance> Collect(Scene& scene, const ScriptModule& module, std::span<const ScriptOrderEntry> order) const;
    // フックをガード付きで動かす。落ちたら false (スクリプトを止め、1 回だけ知らせる)。EndPlay (と落ちたとき)
    // はスクリプトのコルーチンを止める。
    bool Call(ScriptWorld& world, const Instance& in, int hook, float dt, int other = 0, const RmEvent* event = nullptr);

    std::unordered_set<void*> started_;
    std::unordered_set<void*> crashed_;
    bool timing_ = false;
    std::unordered_map<const ScriptType*, ScriptTime> times_;  // (null のキー: コルーチン)
};

// --- シーンの切り替え (Game::LoadScene) ---------------------------------------------------------
enum class SceneSwitch { None, Switched, Failed };
// フレームの間に: 今終えたフレームでスクリプトが頼んだシーン。`load` がそれ (world.sceneNames での
// 位置) を渡されたシーン (部品の型は設定済み) に読み込む。そのあと `playing` の始まった全スクリプトの
// EndPlay を呼び、`playing` を新しいシーンにする。そのスクリプトは次の Frame で BeginPlay を受ける。
// None: 何も頼まれていない。失敗 (*error に理由): `playing` はそのまま続く。
SceneSwitch SwitchAskedScene(Scene& playing, ScriptWorld& world, ScriptRunner& runner, const ScriptModule& module,
                             std::span<const ScriptOrderEntry> order, const std::function<bool(size_t index, Scene& into, std::string* error)>& load,
                             std::string* error);

// フック。__try / __except の中で呼ぶ (ScriptGuard.cpp)。0 = 問題なし、それ以外は例外コード。
// hook: 0 = BeginPlay、1 = Tick、2 = EndPlay。
unsigned long CallScriptHook(const RmScriptDesc& script, int hook, void* self, RmEntityId entity, float dt, RmEntityId other = 0,
                             const RmEvent* event = nullptr);
// コルーチンの resume / destroy。同じようにガード付き (0 = 問題なし)。*alive: resume の答え。
unsigned long CallCoroutineResume(int (*resume)(void*, RmWait*), void* frame, RmWait* next, int* alive);
unsigned long CallCoroutineDestroy(void (*destroy)(void*), void* frame);
// スクリプト (またはその RM_STRUCT) のオブジェクトを、DLL のコードで作る、壊す、コピーする。同じようにガード付き。
unsigned long CallScriptConstruct(void (*construct)(void*), void* p);
unsigned long CallScriptDestruct(void (*destruct)(void*), void* p);
unsigned long CallScriptCopy(void (*copy)(void*, const void*), void* dst, const void* src);

}  // namespace rm::game_production
