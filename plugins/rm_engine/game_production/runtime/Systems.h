#pragma once
// システム: 毎フレームゲームを動かすもの (ECS の S)。ゲーム制作は、どのシステムが何をするかを
// 知らない。プラグインが部品 (Components.h) と同じようにシステムを渡す:
//
//   // プラグインのランタイムの部分 (runtime/Register.cpp - core/RuntimeRegistry.h) に書く。
//   // 書き出したゲームも、エディターの Play と同じ一覧を読める:
//   static const game_production::SystemInfo kSpin{"回り続ける", game_production::SystemPhase::Frame, 0, &RunSpin};
//   registry.Contribute(game_production::kSystemPoint, kSpin);
//
// 種類は 2 つ:
//   Fixed  ゲーム設定の 物理の刻み (fixedTimestep) の刻みで動く: 1 フレームに 0 回、1 回、またはそれ以上。
//          PC がどれだけ速くても同じに動く (物理、ゲームのルール)。
//   Frame  前回からの時間を受け取って、1 フレームに 1 回動く (見た目: 回る、アニメーション)。
// 毎フレーム: まず Fixed の刻みすべて、そのあと Frame のシステム。同じ種類の中では `order` (小さい
// ほうが先)、そのあとは渡された順。
//
// 純粋 (ランタイム: エディターも描画もファイルも使わない)。tests/TestSystems.cpp でテストする。

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <vector>

namespace rm::game_production {

// PluginRegistry の口: 項目は SystemInfo (static)。
inline constexpr char kSystemPoint[] = "rm_engine.game_production.system";

enum class SystemPhase { Fixed, Frame };

// アセットの中身。それを必要とするシステム (メッシュコライダーのモデル) 向けに、ホストがそこへ行く道を渡す。
struct AssetAccess {
    // ゲームが持っているままのバイト列 (書き出したゲーム: .rmpak から。ベイク済み)。nullopt: なし。
    std::function<std::optional<std::string>(const AssetRef&)> bytes;
    // ディスク上のファイル (エディター: プロジェクトの Assets/...)。空: なし。
    std::function<std::filesystem::path(const AssetRef&)> file;
};

// システムが頼むスクリプトのフック (物理の接触 ...): フレームの固定の刻みの後に、`entity` の始まった
// スクリプトすべてに、スクリプトの順で呼ぶ (ScriptRunner::Deliver)。`other` が付く。フックの番号は
// RmAbi.h の RM_HOOK_*  (ゲーム制作は、システムが何かを知らない)。
struct HookCall {
    int entity = 0;
    int hook = 0;
    int other = 0;
};

// システムがスクリプトに送るイベント (クリップのイベント ...)。送り手はエンジン (rm::Event::sender は無効)。
// 届け方は rm::Events::SendTo と同じ: このフレームの Tick の後に、`target` のスクリプトの OnEvent へ
// (ホストが ScriptWorld::events に移す: QueueSystemEvents)。
struct SystemEvent {
    int target = 0;
    std::string name;
    double number = 0;
    std::string text;
    int entity = 0;  // 運ぶエンティティ (rm::Event::entity。0: なし)
};

// システムが動くたびに受け取るもの。
struct SystemContext {
    Scene& scene;
    float dt = 0;          // 秒数: 固定の刻み (Fixed) か、前のフレームからの時間 (Frame)
    double time = 0;       // この実行より前のゲーム内の秒数
    uint64_t frame = 0;    // これより前に動いたフレーム数 (最初のフレームは 0)
    // 今ゲームが見ているエンティティ (その id。0: なし)。Play の開始時は、ゲーム設定の 最初のカメラ に
    // なる。システムはこれを別のエンティティにして、ゲームのカメラを切り替える (何がエンティティを
    // カメラにするかは空間の仕事: 3D の Camera 部品 ...)。
    uint64_t camera = 0;
    // スクリプトへのフックの届け先 (null: 届ける者がいない。頼むことがない)。
    std::vector<HookCall>* hooks = nullptr;
    // ホストのアセット (null: なし)。
    const AssetAccess* assets = nullptr;
    // スクリプトへのイベントの届け先 (null: 届ける者がいない)。
    std::vector<SystemEvent>* events = nullptr;
};

struct SystemInfo {
    const char* name = "";  // コンソール用 (ログと同じく日本語)
    SystemPhase phase = SystemPhase::Frame;
    int order = 0;
    void (*run)(SystemContext& ctx) = nullptr;
};

// `type` の部品すべて。動くエンティティのもの (そのエンティティも親もすべて 有効)。階層の順で。
// その型を複数持つエンティティは、それぞれを順に返す。プラグインが無い部品は返さない。
void ForEachComponent(Scene& scene, std::string_view type, const std::function<void(SceneEntity&, Component&)>& fn);

// プラグインが渡したシステム (そのランタイムの部分)。渡された順。ゲームが動かすのはこれ
// (SystemRunner::SetSystems が並べ替える)。
std::vector<const SystemInfo*> RegisteredSystems(const RuntimeRegistry& registry);

// ゲームが見通す部品の型 (ComponentInfo::camera)。それぞれ 1 回だけ。
std::vector<std::string> CameraTypes(const RuntimeRegistry& registry);
// シーン設定の型 (SceneSettingInfo)。それぞれ 1 回だけ、渡された順 (Scene::SetSettingTypes)。
std::vector<const reflect::TypeDesc*> SettingTypes(const RuntimeRegistry& registry);
// Play を始めるシーンのカメラ (Play、ゲーム、切り替えた先のシーン): `wanted` (ゲーム設定の
// 最初のカメラ) が、このシーンのエンティティでカメラ部品を持つならそれ。そうでなければ、カメラ部品を
// 持つ最初の動いているエンティティ (階層の順)。無ければ 0。
uint64_t SceneCamera(const Scene& scene, const std::vector<std::string>& cameraTypes, uint64_t wanted);

// シーン (Play するコピー) の上でシステムを動かす。ゲームの時計と、固定の刻みの余りを持つ。
class SystemRunner {
public:
    // 1 フレームでの Fixed の刻みの上限。とても長くかかったフレーム (ブレークポイント、ウィンドウを
    // ドラッグ) で、追いつくために何百ステップも動かさないようにする。
    static constexpr int kMaxFixedSteps = 8;

    void SetSystems(std::span<const SystemInfo* const> systems);
    const std::vector<const SystemInfo*>& Systems() const { return systems_; }

    // 最初に戻す (Play をもう一度押した)。
    void Reset();
    // `dt` 秒の 1 フレーム (負: 0)。`fixedStep` <= 0: Fixed の刻みなし。動いた Fixed の刻みの数を返す。
    int Advance(Scene& scene, double dt, float fixedStep);

    double Time() const { return time_; }
    uint64_t Frames() const { return frames_; }
    // ゲームが見通すカメラ (SystemContext::camera): Play の開始時に設定し、システムが変える。Reset で 0 になる。
    void SetCamera(uint64_t entity) { camera_ = entity; }
    uint64_t Camera() const { return camera_; }
    // システムがアセットを読む方法 (SystemContext::assets)。
    void SetAssets(AssetAccess a) { assets_ = std::move(a); }
    // 最後の Advance でシステムが頼んだフック (ScriptRunner::Deliver 用)。
    const std::vector<HookCall>& Hooks() const { return hooks_; }
    // 最後の Advance でシステムが送ったイベント (ホストが QueueSystemEvents で ScriptWorld に移す)。
    const std::vector<SystemEvent>& Events() const { return events_; }

    // 最後の Advance で各システムにかかった時間 (固定の刻みは合計)。頼まれている間だけ測る
    // (エディターの デバッグ パネル)。それと、最後の Advance で動いた固定の刻みの数。
    struct SystemTime {
        const SystemInfo* system = nullptr;
        double ms = 0;
        int runs = 0;
    };
    void SetTiming(bool on) { timing_ = on; }
    const std::vector<SystemTime>& Times() const { return times_; }
    int LastSteps() const { return lastSteps_; }

private:
    std::vector<const SystemInfo*> systems_;  // Fixed が先、そのあと Frame。それぞれの中は order 順
    double time_ = 0;
    double fixedTime_ = 0;  // Fixed の刻みが進んだところまでのゲーム内の時間
    double carry_ = 0;      // まだ Fixed の刻みで消化していない時間
    uint64_t frames_ = 0;
    uint64_t camera_ = 0;
    std::vector<HookCall> hooks_;
    std::vector<SystemEvent> events_;
    AssetAccess assets_;
    bool timing_ = false;
    std::vector<SystemTime> times_;
    int lastSteps_ = 0;
    // システムを 1 つ動かす (頼まれたら時間を測る)。
    void Run(size_t index, SystemContext& ctx);
};

}  // namespace rm::game_production
