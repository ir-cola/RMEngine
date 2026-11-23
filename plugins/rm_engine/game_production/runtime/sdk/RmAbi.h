/* RM Engine - エンジンとゲームのスクリプト (DLL) の境目。素の C: 越えるのは数、素の struct、
 * UTF-8 の `const char*`、関数ポインターだけ - std::string も仮想関数も越えない
 * (設計: TestEngine docs/NewEngineBluePrint.md 6 / 6-b)。
 *
 *   エンジン (エディターか書き出したゲーム)                   スクリプトの DLL (Scripts_0007.dll)
 *     LoadLibrary -> GetProcAddress("RmModuleInit")
 *     RmModuleInit(&api, RM_ABI_VERSION)  ───────────────▶  版を確かめ、&api を持っておく
 *                                         ◀───────────────  持っているスクリプト (RmModule) を返す
 *     毎フレーム: 全スクリプトのフック (BeginPlay / Tick / EndPlay) を関数ポインターで呼ぶ
 *     スクリプトは api->core (とプラグインの部品: api->part(...)) を通して呼び返す
 *
 * エンジンはスクリプトの std::string に直接は触らない: DLL は自分の C ランタイムを持つ (DLL は /MD、
 * エンジンは /MT のことがある) ので、文字列は DLL 自身の 2 つの関数 (RmModule::stringGet /
 * stringSet) を通して読み書きする。片側で確保して反対側で解放するものは、ほかにない。
 *
 * このファイルは自分では取り込まない: RM.h が取り込む。ここを変えたら RM_ABI_VERSION を上げる
 * (ABI 7: RmScriptDesc::onEvent。下の部品 - 時間・コルーチン・イベント・エンティティ・マウス・入力 2)。 */
#pragma once

#include <stdint.h>

#define RM_ABI_VERSION 7u

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t RmEntityId; /* 0 = なし */

/* スクリプトが持てるフィールドの種類 (RM_PROPERTY)。 */
enum RmFieldType {
    RmField_Bool = 0,
    RmField_Int = 1,
    RmField_Float = 2,
    RmField_String = 3, /* std::string */
    RmField_Vec2 = 4,
    RmField_Vec3 = 5,
    RmField_Color = 6,
    RmField_EntityRef = 7,
    /* ABI 4 */
    RmField_Enum = 8,   /* RM_ENUM の enum class (RmFieldDesc::enumDesc) */
    RmField_Struct = 9, /* RM_STRUCT の struct (RmFieldDesc::structDesc) */
    RmField_List = 10,  /* 上のどれかの std::vector (RmFieldDesc::list。要素の enumDesc /
                           structDesc / assetKinds がフィールドのもの) */
    RmField_Asset = 11  /* rm::PrefabRef / rm::ImageRef / rm::ModelRef / rm::SoundRef: char guid[RM_ASSET_ID_SIZE]
                           (16 進 32 桁。"" = なし) - ただのバイト列で、エンジンが読み書きする */
};

#define RM_ASSET_ID_SIZE 40

enum RmFieldFlags {
    RmFieldFlag_Edit = 1,      /* インスペクターに出す */
    RmFieldFlag_ReadOnly = 2,
    RmFieldFlag_Transient = 4, /* RM_TRANSIENT: 保存しない・出さない */
    RmFieldFlag_HasMin = 8,
    RmFieldFlag_HasMax = 16,
    RmFieldFlag_HasStep = 32
};

typedef struct RmFieldDesc {
    const char* name; /* C++ の名前 = シーンファイルのキー */
    uint32_t type;    /* RmFieldType */
    uint32_t flags;   /* RmFieldFlags */
    void* (*access)(void* self);
    const char* label; /* UTF-8。null もある */
    const char* tooltip;
    double min, max, step;
    int32_t line; /* ヘッダーにある */
    /* ABI 4: 上の種類が必要とするもの (ほかは null) */
    const struct RmEnumDesc* enumDesc;
    const struct RmStructDesc* structDesc;
    const struct RmListDesc* list;
    const char* assetKinds; /* RmField_Asset: "image" / "model" / "audio" / ".rmprefab" (core/asset の種類、または拡張子) */
} RmFieldDesc;

typedef struct RmEnumItem {
    const char* name;  /* ファイルに保存する */
    const char* label; /* 画面に出す。null: 名前 */
    int64_t value;
} RmEnumItem;

typedef struct RmEnumDesc {
    const char* name; /* C++ の名前 */
    const RmEnumItem* items;
    uint32_t itemCount;
    int64_t (*get)(const void* p);
    void (*set)(void* p, int64_t v);
} RmEnumDesc;

/* スクリプトの中の RM_STRUCT (値: フックはない)。 */
typedef struct RmStructDesc {
    const char* name;
    const char* label;
    uint64_t size, align;
    const struct RmFieldDesc* fields;
    uint32_t fieldCount;
    void (*construct)(void* p);
    void (*destruct)(void* p);
    void (*copy)(void* dst, const void* src);
} RmStructDesc;

/* std::vector のフィールド。DLL が扱う (メモリは DLL の C ランタイムのもの)。 */
typedef struct RmListDesc {
    uint32_t element; /* 要素の RmFieldType (RmField_List にはならない) */
    uint32_t reserved;
    uint64_t (*size)(const void* vec);
    void (*resize)(void* vec, uint64_t n);
    void* (*at)(void* vec, uint64_t i);
} RmListDesc;

typedef struct RmScriptDesc {
    const char* name;   /* C++ の名前 ("Mover"、"game::Mover") = シーンが保存するもの */
    const char* label;  /* RM_SCRIPT(Label="...") か名前 */
    const char* header; /* 宣言されているヘッダー (メッセージ用) */
    uint64_t size, align;
    const RmFieldDesc* fields;
    uint32_t fieldCount;
    void (*construct)(void* p); /* メンバーの既定値で placement new する */
    void (*destruct)(void* p);
    void (*copy)(void* dst, const void* src);
    /* スクリプトにそのフックがないときは null */
    void (*beginPlay)(void* self, RmEntityId self_entity);
    void (*tick)(void* self, RmEntityId self_entity, float dt);
    void (*endPlay)(void* self, RmEntityId self_entity);
    /* ABI 4: ゲームの Play 中にスクリプトを作り直した後 (値は名前で引き継ぐ。
       BeginPlay はもう一度は呼ばない)。スクリプトにないときは null。 */
    void (*onReloaded)(void* self, RmEntityId self_entity);
    /* ABI 5: 物理が知らせること (RMPhysics.h)。`other` は相手のエンティティ。スクリプトにフックが
       ないときは null。そのフレームの固定ステップのあと、開始済みのスクリプトにだけ呼ぶ。 */
    void (*onHit)(void* self, RmEntityId self_entity, RmEntityId other);          /* 触れ始めた (トリガーではない) */
    void (*onTriggerEnter)(void* self, RmEntityId self_entity, RmEntityId other); /* どちらかがトリガー */
    void (*onTriggerExit)(void* self, RmEntityId self_entity, RmEntityId other);
    /* ABI 6: エンティティのゲーム UI のボタンが押された (RMUI.h: マウス、またはフォーカス中のものへの 決定)。
       そのフレームのシステムのあと、開始済みのスクリプトにだけ呼ぶ。スクリプトにないときは null。 */
    void (*onClick)(void* self, RmEntityId self_entity);
    /* ABI 7: rm::Events (RM.h) が送ったイベントがエンティティに届いた。そのフレームの Tick のあと
       (システム / サービスのフックのあとでもある)、開始済みのスクリプトにだけ呼ぶ。スクリプトにないときは null。 */
    void (*onEvent)(void* self, RmEntityId self_entity, const struct RmEvent* event);
} RmScriptDesc;

/* イベント (ABI 7): 名前、送った者 (0: エンジン / 誰でもない)、運んでくるもの。文字列はフックが
   戻るまで生きている (残したいものはコピーする)。 */
typedef struct RmEvent {
    const char* name;   /* UTF-8 */
    RmEntityId sender;  /* 送ったスクリプトのエンティティ (0: なし) */
    double number;
    const char* text;   /* UTF-8。null にはならない (ないときは "") */
    RmEntityId entity;  /* 運んでくるエンティティ (0: なし) */
} RmEvent;

/* フックの番号 (システムはこの番号で頼む: game_production::HookCall::hook)。 */
enum RmHook {
    RM_HOOK_BEGIN_PLAY = 0,
    RM_HOOK_TICK = 1,
    RM_HOOK_END_PLAY = 2,
    RM_HOOK_RELOADED = 3,
    RM_HOOK_HIT = 4,
    RM_HOOK_TRIGGER_ENTER = 5,
    RM_HOOK_TRIGGER_EXIT = 6,
    RM_HOOK_CLICK = 7, /* ABI 6 */
    RM_HOOK_EVENT = 8  /* ABI 7 (RmEvent を付けて呼ぶ) */
};

enum RmModuleFlags {
    RmModuleFlag_Debug = 1 /* Debug で作った (Debug の C ランタイム): Debug のエンジンだけが受け取る */
};

typedef struct RmModule {
    uint32_t abi;   /* DLL を作ったときの RM_ABI_VERSION */
    uint32_t flags; /* RmModuleFlags */
    uint32_t scriptCount;
    const RmScriptDesc* scripts;
    /* スクリプトの std::string フィールド (RmFieldDesc::access が渡すポインター) を、DLL 自身が扱う:
       get は入る分を buf にコピーし (0 で終わる)、全体の長さをバイトで返す。 */
    uint64_t (*stringGet)(const void* field, char* buf, uint64_t cap);
    void (*stringSet)(void* field, const char* utf8, uint64_t len);
} RmModule;

/* どのエンジンも渡すもの (ゲーム制作の部品)。`ctx` はエンジンのもの: そのまま返すこと。 */
typedef struct RmCoreApi {
    void (*log)(void* ctx, int level, const char* utf8); /* 0 = 情報、1 = 警告、2 = エラー */
    float (*deltaTime)(void* ctx);                       /* このフレームの秒数 */
    double (*time)(void* ctx);                           /* Play してからの秒数 */
    RmEntityId (*findByName)(void* ctx, const char* utf8);
    int (*exists)(void* ctx, RmEntityId e);
    int (*isActive)(void* ctx, RmEntityId e);
    void (*setActive)(void* ctx, RmEntityId e, int active);
    /* 名前 (UTF-8、0 で終わる、cap で切る) をコピーし、全体の長さをバイトで返す。 */
    uint32_t (*getName)(void* ctx, RmEntityId e, char* buf, uint32_t cap);
    RmEntityId (*parent)(void* ctx, RmEntityId e);
    /* キーは Windows の仮想キーコードで (VK_LEFT = 0x25、'A' = 0x41 ...)。 */
    int (*keyDown)(void* ctx, int vk);
    int (*keyPressed)(void* ctx, int vk);  /* このフレームに押された */
    int (*keyReleased)(void* ctx, int vk); /* このフレームに離された */
    RmEntityId (*camera)(void* ctx);       /* ゲームが見るカメラ */
    void (*setCamera)(void* ctx, RmEntityId e);
    /* エンティティの `typeName` (RmScriptDesc::name) 型の n 番目のスクリプト。null: ない。 */
    void* (*getScript)(void* ctx, RmEntityId e, const char* typeName, uint32_t nth);
    /* シーン (ABI 3)。ゲームのシーンは、ゲームの設定の一覧にある順のもの。シーンの名前は
       .rmscene を除いたファイル名。loadScene / loadSceneAt はこのフレームの終わりに切り替えを頼む
       (このシーンの EndPlay、新しいシーンの読み込み、その BeginPlay): 1 = 頼んだ、0 = そんなシーンはない (知らせる)。 */
    int (*loadScene)(void* ctx, const char* nameUtf8);
    int (*loadSceneAt)(void* ctx, int32_t index);
    /* 今動いているシーン: その名前 (getName と同じようにコピーし、全体の長さを返す)、一覧での
       位置 (-1: 一覧にない)、一覧にいくつあるか。 */
    uint32_t (*sceneName)(void* ctx, char* buf, uint32_t cap);
    int32_t (*sceneIndex)(void* ctx);
    int32_t (*sceneCount)(void* ctx);
    /* エンティティと部品 (ABI 4)。spawn / spawnPrefab はエンティティをすぐに作り (`parent` の下に。
       0: いちばん上)、それを返す。スクリプトは次のフレームに始まる (最初の Tick の前に BeginPlay)。
       destroy / removeComponent はこのフレームの終わりに効く: 先に EndPlay、エンティティの
       子もいっしょに。なくなったものの id は二度と使わない (exists() は 0 を返す)。 */
    RmEntityId (*spawn)(void* ctx, const char* nameUtf8, RmEntityId parent);
    RmEntityId (*spawnPrefab)(void* ctx, const char* assetId, RmEntityId parent); /* 0: ゲームの Prefab ではない (知らせる) */
    void (*destroy)(void* ctx, RmEntityId e);
    /* 型の名前による部品 (スクリプトの RmScriptDesc::name、またはエンジンのもの。例:
       "rm::space3d::Transform"): 既定値で足して返す (2 つ持てないものは、すでにあるもの)。
       null: そんな型 / そんなエンティティはない。 */
    void* (*addComponent)(void* ctx, RmEntityId e, const char* typeName);
    int (*removeComponent)(void* ctx, RmEntityId e, const char* typeName, uint32_t nth); /* 1: 消える */
    uint32_t (*countComponents)(void* ctx, RmEntityId e, const char* typeName);
} RmCoreApi;

typedef struct RmApi {
    uint32_t abi; /* エンジンの RM_ABI_VERSION */
    uint32_t reserved;
    void* ctx;
    const RmCoreApi* core;
    /* プラグインの API の部品を、名前と版で (RM3D.h: 3D の Transform ...)。そのプラグインが
       このエンジン / ゲームにないときは null。 */
    const void* (*part)(void* ctx, const char* name, uint32_t version);
} RmApi;

/* --- ゲーム制作の部品 (RmApi::part に名前で頼む。プラグインのものと同じ) -------------
 * 部品には自分の版がある: 足しても RmCoreApi も RM_ABI_VERSION も変わらず、それがない
 * エンジンは null を返す (RM.h は「なし」と答える)。部品を変えたら、その部品の版を新しくする。 */

/* 動作の名前による入力 (ゲームの設定の 入力): "rm_engine.game_production.input"、版 1。 */
#define RM_PART_INPUT "rm_engine.game_production.input"
#define RM_PART_INPUT_VERSION 1u
typedef struct RmInputApi {
    int (*down)(void* ctx, const char* action);     /* いま押している */
    int (*pressed)(void* ctx, const char* action);  /* このフレームに押された */
    int (*released)(void* ctx, const char* action); /* このフレームに離された */
    float (*axis)(void* ctx, const char* action);   /* -1..1 */
} RmInputApi;

/* 入力、版 2: 同じものをプレイヤーごとに、そしてプレイヤーが変えられるもの。`player`: -1 = みんな
 * (全部のゲームパッド、キーボード、マウス - 版 1 と同じ)、0..3 = そのプレイヤー: その番号のゲームパッドと、
 * ゲームの設定の キーボードとマウスのプレイヤー がその人のときのキーボードとマウス。
 * 割り当てはそのプレイヤーのもの: スクリプトが変える (setBinding ...) までは、ゲームの設定の 入力。
 * saveBindings はそれをプレイヤーの PC に書き (セーブの隣の input.json)、loadBindings は
 * 読み戻す (ゲームは始まるときに自分でやる)。 */
#define RM_PART_INPUT_VERSION2 2u
typedef struct RmInputApi2 {
    int (*down)(void* ctx, const char* action, int32_t player);
    int (*pressed)(void* ctx, const char* action, int32_t player);
    int (*released)(void* ctx, const char* action, int32_t player);
    float (*axis)(void* ctx, const char* action, int32_t player);
    int (*padConnected)(void* ctx, int32_t player);
    /* プレイヤーのパッドを震わせる: low = 左 (重い) モーター、high = 右 (軽い) モーター、0..1。 */
    void (*vibrate)(void* ctx, int32_t player, float low, float high, float seconds);
    uint32_t (*bindingCount)(void* ctx, const char* action);
    /* control: ゲームの設定の InputControl の値 (キーの仮想キーコード、パッドの 0x100 + ...)。 */
    int (*binding)(void* ctx, const char* action, uint32_t index, int32_t* control, float* scale);
    int (*setBinding)(void* ctx, const char* action, uint32_t index, int32_t control, float scale); /* index == count: 足す */
    int (*removeBinding)(void* ctx, const char* action, uint32_t index);
    void (*resetBindings)(void* ctx); /* ゲームの設定のものに戻す */
    int (*saveBindings)(void* ctx);
    int (*loadBindings)(void* ctx);
    /* `player` についてこのフレームに押されたもの (キー、マウスのボタン、パッドのボタン、半分まで
       倒したスティックやトリガー): その control と、*direction 1 / -1 (左に倒したスティック: -1)。0: なし。
       「ジャンプのキーを押してください」のため。 */
    int32_t (*anyPressed)(void* ctx, int32_t player, float* direction);
    /* control の名前 ("Space"、"A"、"PadA"、"MouseLeft" ...)。getName と同じようにコピーする。 */
    uint32_t (*controlName)(void* ctx, int32_t control, char* buf, uint32_t cap);
} RmInputApi2;

/* ゲームの画面にあるマウス: "rm_engine.game_production.mouse"、1。画面はゲームの設定の 画面
 * (DIP、左上が (0, 0)、y は下向き) - エディターのゲームビューはそれに合わせて拡大・縮小される。 */
#define RM_PART_MOUSE "rm_engine.game_production.mouse"
#define RM_PART_MOUSE_VERSION 1u
typedef struct RmMouseApi {
    int (*position)(void* ctx, float out[2]); /* 0: ゲームの上にない (out: 最後にあった所) */
    void (*delta)(void* ctx, float out[2]);   /* 前のフレームから動いた (ゲームの上にないあいだは 0) */
    float (*wheel)(void* ctx);                /* このフレームのホイールの目盛り: + は手前から奥へ回したとき */
    void (*screenSize)(void* ctx, float out[2]);
} RmMouseApi;

/* 時間: "rm_engine.game_production.time"、1。倍率はゲームのすべての dt (スクリプトの
 * Tick、システム、物理、パーティクル) に掛かる。0 でそれらが止まる (ポーズメニュー) - Tick は呼ばれ続ける。 */
#define RM_PART_TIME "rm_engine.game_production.time"
#define RM_PART_TIME_VERSION 1u
typedef struct RmTimeApi {
    float (*scale)(void* ctx);
    void (*setScale)(void* ctx, float scale);
    float (*unscaledDeltaTime)(void* ctx);
    double (*unscaledTime)(void* ctx);
    uint64_t (*frameCount)(void* ctx);
} RmTimeApi;

/* コルーチン: "rm_engine.game_production.coroutines"、1。コルーチンのフレームは DLL のもの (C++20 の
 * コルーチン: RM.h の rm::Task)。エンジンは次にいつ動かすかを持つ。resume(frame, &next) は次の待ちまで
 * 動かして、その待ちを書く。コルーチンが終わったときは 0 を返す。destroy(frame) はそれを解放する (終わった
 * もの、または途中で止めたもの)。持ち主は、始めたときに動いていたフック (またはコルーチン) のスクリプト:
 * その EndPlay (またはエンティティが壊れる、シーンが切り替わる、ゲームが止まる、スクリプトが作り直される)
 * で止まる。 */
enum RmWaitKind {
    RmWait_Frames = 0,      /* `frames` フレーム (1: 次のフレーム) */
    RmWait_Seconds = 1,     /* ゲーム時間で `seconds` 秒 (時間の倍率が効く) */
    RmWait_RealSeconds = 2, /* 実時間で `seconds` 秒 */
    RmWait_Coroutine = 3    /* コルーチン `coroutine` が終わるまで */
};
typedef struct RmWait {
    uint32_t kind;
    uint32_t frames;
    double seconds;
    uint64_t coroutine;
} RmWait;
#define RM_PART_COROUTINES "rm_engine.game_production.coroutines"
#define RM_PART_COROUTINES_VERSION 1u
typedef struct RmCoroutinesApi {
    /* 最初の待ちまですぐ動かす: その id (0: すぐ終わった - destroy を呼んだ)。 */
    uint64_t (*start)(void* ctx, void* frame, int (*resume)(void* frame, RmWait* next), void (*destroy)(void* frame));
    void (*stop)(void* ctx, uint64_t id);
    void (*stopAll)(void* ctx); /* いま動いているスクリプトのすべてのコルーチン */
    int (*running)(void* ctx, uint64_t id);
} RmCoroutinesApi;

/* イベント: "rm_engine.game_production.events"、1。送るのはすぐで、届くのはこのフレームの Tick のあと
 * (スクリプトの OnEvent、その順に)。OnEvent の中で送ったものはその直後に来る。scope: 0 = すべての
 * エンティティ、1 = `target` だけ、2 = `target` とその下すべて。 */
#define RM_PART_EVENTS "rm_engine.game_production.events"
#define RM_PART_EVENTS_VERSION 1u
typedef struct RmEventsApi {
    void (*send)(void* ctx, uint32_t scope, RmEntityId target, const char* name, double number, const char* text, uint64_t textLen,
                 RmEntityId entity);
} RmEventsApi;

/* エンティティ、その他: "rm_engine.game_production.entity"、1。タグとレイヤーは名前 (ゲームの設定が
 * 並べる)。"" = なし (レイヤーの "": 既定のもの、番号 0)。 */
#define RM_PART_ENTITY "rm_engine.game_production.entity"
#define RM_PART_ENTITY_VERSION 1u
typedef struct RmEntityApi {
    uint32_t (*getTag)(void* ctx, RmEntityId e, char* buf, uint32_t cap);
    void (*setTag)(void* ctx, RmEntityId e, const char* utf8);
    uint32_t (*getLayer)(void* ctx, RmEntityId e, char* buf, uint32_t cap);
    void (*setLayer)(void* ctx, RmEntityId e, const char* utf8);
    /* レイヤーの番号: "" は 0、ゲームの設定の レイヤー は並んでいる順に 1、2 ...。-1: ない。 */
    int32_t (*layerIndex)(void* ctx, const char* name);
    /* このタグのエンティティ (階層の順): いくつあるか、最初の `cap` 個をコピーする。 */
    uint32_t (*findWithTag)(void* ctx, const char* tag, RmEntityId* out, uint32_t cap);
    /* `e` の子 (0: いちばん上の階層) を順に: いくつあるか、最初の `cap` 個をコピーする。 */
    uint32_t (*children)(void* ctx, RmEntityId e, RmEntityId* out, uint32_t cap);
    /* `e` を (その下のものといっしょに) `parent` の下の、子の中の `index` の位置へ動かす
       (-1: 最後)。keepWorld: ワールドでは今の位置のまま (Transform が変わる)。0: 断った
       (自分の中へ、そんなエンティティはない)。 */
    int (*setParent)(void* ctx, RmEntityId e, RmEntityId parent, int keepWorld, int32_t index);
    void (*setName)(void* ctx, RmEntityId e, const char* utf8);
} RmEntityApi;

/* グローバル変数 (ゲームの設定の グローバル変数) を名前で: "rm_engine.game_production.globals"、1。
 * `type` はスクリプトがそれをどう持つか: RmField_Bool (int32_t の 0 / 1)、RmField_Int (int32_t)、
 * RmField_Float (float)、RmField_Vec2 (float[2])、RmField_Vec3 (float[3])。エンジンは変数自身の型との
 * 間で変換する (文字の変数は、文字としてだけ読み書きできる)。0 = そんな変数はない (1 度だけ知らせる。
 * `out` はそのまま)。 */
#define RM_PART_GLOBALS "rm_engine.game_production.globals"
#define RM_PART_GLOBALS_VERSION 1u
typedef struct RmGlobalsApi {
    int (*get)(void* ctx, const char* name, uint32_t type, void* out);
    int (*set)(void* ctx, const char* name, uint32_t type, const void* value);
    /* 文字として (数の変数: その文字、"3" / "1.5" / "true" / "1, 2"): getName と同じようにコピーし、
       全体の長さを返す。 */
    uint64_t (*getString)(void* ctx, const char* name, char* buf, uint64_t cap);
    int (*setString)(void* ctx, const char* name, const char* utf8, uint64_t len);
} RmGlobalsApi;

/* すべてのグローバル変数をスロットに保存する ("0"、"auto": 英字、数字、_ と -): "rm_engine.
 * game_production.save"、1。1 = できた、0 = できなかった (コンソールに知らせる)。 */
#define RM_PART_SAVE "rm_engine.game_production.save"
#define RM_PART_SAVE_VERSION 1u
typedef struct RmSaveApi {
    int (*write)(void* ctx, const char* slot);
    int (*read)(void* ctx, const char* slot);
    int (*exists)(void* ctx, const char* slot);
    int (*remove)(void* ctx, const char* slot);
} RmSaveApi;

/* セーブ、版 2: 版 1 の 4 つと、世界 (部品「セーブする」を付けた物の位置・有効・部品の値) のセーブ。
 * writeWorld: グローバル変数と今のシーンの世界をスロットに書く。readWorld: グローバル変数をすぐ読み、
 * 世界はこのフレームの後で (セーブしたシーンを読み直し、その上に当てる。シーンの切り替えと同じ: スクリプト
 * は EndPlay と BeginPlay を受ける)。hasWorld: そのスロットに世界がある。1 = できた、0 = できなかった。 */
#define RM_PART_SAVE_VERSION2 2u
typedef struct RmSaveApi2 {
    int (*write)(void* ctx, const char* slot);
    int (*read)(void* ctx, const char* slot);
    int (*exists)(void* ctx, const char* slot);
    int (*remove)(void* ctx, const char* slot);
    int (*writeWorld)(void* ctx, const char* slot);
    int (*readWorld)(void* ctx, const char* slot);
    int (*hasWorld)(void* ctx, const char* slot);
} RmSaveApi2;

/* ゲームそのもの: "rm_engine.game_production.game"、1。quit: このフレームのあとでゲームが終わる
 * (書き出したゲームのウィンドウが閉じる。エディターの Play は ■ のように止まる)。 */
#define RM_PART_GAME "rm_engine.game_production.game"
#define RM_PART_GAME_VERSION 1u
typedef struct RmGameApi {
    void (*quit)(void* ctx);
} RmGameApi;

/* スクリプトの DLL が書き出す、たった 1 つの関数 (rmgen が書く)。`abi` が DLL を作った版と
   違うときは null を返す。 */
typedef const RmModule* (*RmModuleInitFn)(const RmApi* api, uint32_t abi);

#ifdef __cplusplus
}
#endif
