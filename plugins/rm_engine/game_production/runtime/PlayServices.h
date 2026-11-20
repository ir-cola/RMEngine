#pragma once
// プラグインが出す、ゲームが動いている間だけ生きているもの。ゲーム制作は中身を知らない
// (音: サウンドデバイスと鳴っている全部の音 - rm_engine/game_x_audio)。2 つのホスト
// (エディターの Play と書き出したゲーム) は、どちらも同じやり方で動かす:
//
//   Play / ゲームの開始        PlayServices::Start   出された全サービスを作る (PlayServiceInfo::create)
//   毎フレーム                 Frame                 スクリプトとシステムのあと (動いたあとのシーン)
//   ⏸ / ▶                      SetPaused
//   シーンの切り替え           SceneSwitched         (Game::LoadScene: フレームの間)
//   ■ / ゲームの終了           Stop                  全サービスが消える (音もいっしょに止まる)
//
//   // プラグインのランタイム側 (runtime/Register.cpp) で:
//   static const game_production::PlayServiceInfo kAudio{"rm_engine.audio", &CreateAudioService};
//   registry.Contribute(game_production::kPlayServicePoint, kAudio);
//
// スクリプトは、API 側 (ScriptWorld::services->Find(name)) からサービスに届く。
//
// エンティティがどこにいるか (世界での位置と向き) は、空間の仕事。空間を知っているプラグインが
// PlaceInfo (ゲーム制作 × 3D, × 2D) を出し、サービスは EntityPose で尋ねる。

#include "base/Json.h"
#include "base/Math.h"
#include "base/Ref.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Input.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rm::game_production {

class PlayServices;
struct ScriptWorld;

// ホストがサービスに渡すもの (サービスと同じ間だけ生きる)。
struct PlayHost {
    const RuntimeRegistry* registry = nullptr;
    const GameSettings* settings = nullptr;  // ゲームの設定 (エディターのもの: Play 中に編集されるので、今ある値のまま読む)
    // アセットのバイト列 (エディター: そのファイル。ゲーム: 書き出しが入れたもの - 焼いたものか
    // ファイルそのまま)。無いときは nullopt。
    std::function<std::optional<std::string>(const AssetRef&)> asset;
    // その取り込みの設定 (.meta の "import": エディター)。ゲームは何も渡さない (null): アセットが
    // 必要とする分は、アセットに焼き込んである。
    std::function<json::Value(const AssetRef&)> import;
    // そのファイルの名前。メッセージ用 ("jump.wav"。空: 分からない)。
    std::function<std::string(const AssetRef&)> name;
    // コンソールへのメッセージ (レベル 0 情報 / 1 警告 / 2 エラー。日本語)。
    std::function<void(int level, std::string_view text)> log;
    bool silent = false;  // 何も聞こえない (テスト): サービスはデバイスを開かない
};

// 1 フレーム分。スクリプトとシステムのあとに呼ばれる。
struct PlayFrame {
    Scene& scene;
    uint64_t camera = 0;  // ゲームが見ているエンティティ (SystemContext::camera)
    float dt = 0;
    // ゲームの画面 (DIP: 書き出したゲームのウィンドウ。エディターではゲームビューを、ゲームの設定の
    // 画面であるかのように扱う) と、その上のマウス (nullopt: 上にいない - 別のパネルや、ウィンドウの外)。
    Vec2 screen{1280, 720};
    std::optional<Vec2> pointer;
    // 前のフレームから今までに、左ボタンが押された / 離された回数。ホストのウィンドウが知らされた
    // とおりに数える (そのメッセージで。フレームごとにボタンを一度見るのではない): フレームより短い
    // クリック - タッチパッドの軽い叩きや素早いクリック - は両方を持つ。どちらのフレームでもボタンは
    // 上がっていてもそうなる。
    bool pointerWentDown = false, pointerWentUp = false;
    // このフレームと前のフレームの入力 (pressed / released はこの 2 つを比べる。マウスのボタンも)、
    // それとゲームの設定の 入力。何も渡さないテストでは null: 何も押されていない。
    const InputFrame* input = nullptr;
    const InputFrame* inputBefore = nullptr;
    const std::vector<InputAction>* actions = nullptr;
    // サービスが頼むスクリプトフック (ゲーム UI の OnClick): ホストが、すべてのサービスの Frame の
    // あとにスクリプトへ渡す (ScriptRunner::Deliver)。null: 頼めない。
    std::vector<HookCall>* hooks = nullptr;
    // この Play のほかのサービス (PlayServices::Frame が入れる): どのプラグインが出してもよい機能
    // (Sounds) を、どのプラグインかを知らずに頼むため。
    const PlayServices* services = nullptr;
    // ゲーム自身 (Scripts.h: そのシーン - sceneAsked はフレームの間に切り替える -、グローバル変数、
    // quitAsked): スクリプトと同じように、サービスがゲームに頼んでよいこと (ゲーム UI の 押したときの動作)。
    // 何も渡さないテストでは null。
    ScriptWorld* world = nullptr;
};

class PlayService {
public:
    virtual ~PlayService() = default;  // Stop: やっていることはすべてここで終わる
    virtual void Frame(const PlayFrame& f) { (void)f; }
    virtual void SetPaused(bool paused) { (void)paused; }
    // Game::LoadScene がシーンを切り替えた (エンティティは別物になる - 同じ id がまた出ることもある)。
    virtual void SceneSwitched() {}
};

// 音を鳴らすサービスがほかに見せるもの (音: game_x_audio の AudioService) - ゲーム UI の
// 乗った / 押したときの音は、音のプラグインを知らずに PlayServices::Sounds から頼む。None
// (プラグインがゲームに無い): 何も鳴らず、ほかは何も変わらない。
class SoundPlayer {
public:
    // 効果音として (音楽ではなく) 1 回、`volume` (0..1) で。
    virtual void PlaySound(const AssetRef& sound, float volume) = 0;
    // 今の音楽の大きさ: 全体 × 音楽 (ゲームの設定の 音量、またはスクリプトが決めた値) - 別の所で
    // 鳴る音 (ゲーム UI の 動画の音) のため。
    virtual float MusicVolume() const = 0;

protected:
    ~SoundPlayer() = default;
};

// RuntimeRegistry の点: 項目は PlayServiceInfo (static)。
inline constexpr char kPlayServicePoint[] = "rm_engine.game_production.play_service";
struct PlayServiceInfo {
    const char* name = "";  // スクリプトの API 側が見つける名前 ("rm_engine.audio")
    std::unique_ptr<PlayService> (*create)(const PlayHost& host) = nullptr;
};

// 1 回の Play (ゲーム) のサービス。
class PlayServices {
public:
    PlayServices() = default;
    PlayServices(const PlayServices&) = delete;
    PlayServices& operator=(const PlayServices&) = delete;
    ~PlayServices() { Stop(); }

    // プラグインが出した全サービスを `host` 用に作る (ここで持つ: サービスは host を握り続ける)。
    void Start(PlayHost host);
    void Frame(const PlayFrame& f);
    void SetPaused(bool paused);
    void SceneSwitched();
    // 全サービスを消す。最後に作ったものから。
    void Stop();
    bool Running() const { return host_ != nullptr; }
    PlayService* Find(std::string_view name) const;
    // 音を鳴らす最初のサービス (SoundPlayer)。null: 無い。
    SoundPlayer* Sounds() const;

private:
    std::unique_ptr<PlayHost> host_;
    std::vector<std::pair<std::string, std::unique_ptr<PlayService>>> list_;
};

// --- エンティティの居場所 ---------------------------------------------------------------------
// 世界での位置。右手系、Y が上 (2D のゲーム: x y は画面上、z = 0。奥向き)。
struct Pose {
    Vec3 position{0, 0, 0};
    Vec3 forward{0, 0, 1};  // 向いている方向 (カメラ。左手系: +Z が前)
    Vec3 up{0, 1, 0};
};

// RuntimeRegistry の点: 項目は PlaceInfo (static)。
inline constexpr char kPlacePoint[] = "rm_engine.game_production.place";
struct PlaceInfo {
    const char* space = "";  // SceneViewInfo::space と同じ ("3d", "2d")
    // エンティティにこの空間のものが無いときは false (Transform が無い)。
    bool (*pose)(const Scene& scene, int entity, Pose& out) = nullptr;
    // `entity` を `newParent` の下 (0: 一番上) に入れる直前: この空間の Transform を変えて、世界での
    // 位置を保つ (Entity::SetParent(p, keepWorld))。null: 保つものは無い。
    void (*keepWorld)(Scene& scene, int entity, int newParent) = nullptr;
};

// `entity` の居場所を知っている最初の空間。どれも知らないときは false (そんなエンティティが無い、Transform が無い)。
bool EntityPose(const RuntimeRegistry& registry, const Scene& scene, int entity, Pose& out);

}  // namespace rm::game_production
