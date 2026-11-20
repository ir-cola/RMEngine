#pragma once
// ゲーム制作の「デバッグ」パネル: 作っているゲームを見るための、今起きていること - シーンビューと
// ゲームビューを描く頻度 (FPS)、マウスの位置、キーボードとゲームパッドで押されているもの、
// Play 中の入力アクション、シーンのエンティティ / 部品の数、モニターと GPU、プラグインが足すもの
// (パーティクル: 生きている粒の数)。
//
//   GameSoftware   測る (各ビューの描画と Play のフレームに FrameMeter)、DebugView を埋め、
//                  パネルが表示されたら DebugPanel::Draw を呼ぶ
//   plugins        節を足す (kDebugInfoPoint の DebugInfoInfo): 描くたびに行を尋ねる

#include "base/Math.h"
#include "platform/SystemInfo.h"
#include "rm_engine/game_production/editor/ScriptBuild.h"
#include "rm_engine/game_production/runtime/Input.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include "editor/plugin/Software.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <optional>
#include <string>
#include <vector>

namespace rm::platform {
class Gfx;
}  // namespace rm::platform
namespace rm::editor {
class PluginRegistry;
}
namespace rm::ui {
class Context;
struct Rect;
}  // namespace rm::ui

namespace rm::game_production {

class Scene;
class SystemRunner;
struct ScriptWorld;
struct GameSettings;

// 何かを描く (や動かす) 頻度: 直近の 1 秒のフレーム。純粋 - tests/TestDebugPanel.cpp。
class FrameMeter {
public:
    // `now` (秒) の 1 フレーム。このプログラムの時間を `workMs` 使った。
    void Frame(double now, double workMs = 0) {
        marks_.push_back({now, workMs});
        while (!marks_.empty() && now - marks_.front().at > 1.0) marks_.pop_front();
    }
    struct Summary {
        int frames = 0;      // 直近の 1 秒で (0: 描いていない - 何も変わらなかった)
        double fps = 0;      // その間の 1 秒あたりのフレーム数
        double meanMs = 0;   // 2 つのフレームの間の時間の平均
        double worstMs = 0;  // ... 一番長いもの
        double workMs = 0;   // それぞれにかかった時間の平均
    };
    Summary Over(double now) const {
        Summary s;
        const Mark* last = nullptr;
        double first = 0, gaps = 0, work = 0;
        for (const Mark& m : marks_) {
            if (now - m.at > 1.0) continue;
            if (last) {
                const double gap = (m.at - last->at) * 1000.0;
                gaps += gap;
                if (gap > s.worstMs) s.worstMs = gap;
            } else {
                first = m.at;
            }
            work += m.work;
            ++s.frames;
            last = &m;
        }
        if (s.frames == 0) return s;
        s.workMs = work / s.frames;
        if (s.frames >= 2) {
            s.meanMs = gaps / (s.frames - 1);
            const double span = last->at - first;
            s.fps = span > 0 ? (s.frames - 1) / span : 0;
        } else {
            s.fps = 1;  // 直近の 1 秒に 1 フレーム
        }
        return s;
    }

private:
    struct Mark {
        double at, work;
    };
    std::deque<Mark> marks_;
};

// プラグインのパネルの節。PluginRegistry の口: 項目は DebugInfoInfo (static)。`collect` は
// パネルを描くたびに呼ばれる (頻繁: 軽く作る)。その行はそのまま表示される (日本語は Tr を通す。
// 画面のどの文字とも同じ)。
inline constexpr char kDebugInfoPoint[] = "rm_engine.game_production.debug_info";
struct DebugInfoLine {
    std::wstring label;
    std::wstring value;
    bool lit = false;  // アクセントで表示 (目立たせたいもの)
};
// 節が見てよいもの: 表示中のシーン (Play 中は Play しているコピー) とゲーム。
struct DebugContext {
    Scene* scene = nullptr;
    bool playing = false;
    const ScriptWorld* world = nullptr;  // Play 中 (そのサービス: 音、ゲームの UI ...)
};
// The デバッグ window's tabs.
enum class DebugTab { Basic = 0, Controller = 1, Pc = 2, Performance = 3, Game = 4 };
struct DebugInfoInfo {
    const wchar_t* title = L"";  // TrKey(L"...")
    int order = 0;
    DebugTab tab = DebugTab::Game;
    void (*collect)(const DebugContext& ctx, std::vector<DebugInfoLine>& out) = nullptr;
};

// ビューが最後の描画について知らせること (3D / 2D ビュー: 何を描いたか、そのカメラ)。パネルが
// 表示されている間だけ知らせる (ViewReportsWanted: パネルは描くたびに尋ねる)。ビュー 1 つにつき 1 つ。
struct ViewReport {
    std::wstring title;  // "シーンビュー (3D)" ... (ビューが Tr を通す)
    std::vector<DebugInfoLine> lines;
    double at = 0;
};
void WantViewReports();
bool ViewReportsWanted();
void ReportView(const void* view, ViewReport report);
std::vector<ViewReport> RecentViews(double maxAge = 1.0);

// グラフ用の直近のフレーム: 2 つの間隔 (ms) と、それぞれの処理 (ms)。古い順。
struct FrameGraph {
    static constexpr size_t kSamples = 240;
    std::vector<float> intervals, work;
    double last = 0;
    void Push(double now, double workMs) {
        if (last > 0) {
            intervals.push_back(static_cast<float>(std::min(250.0, (now - last) * 1000.0)));
            work.push_back(static_cast<float>(workMs));
            if (intervals.size() > kSamples) intervals.erase(intervals.begin());
            if (work.size() > kSamples) work.erase(work.begin());
        }
        last = now;
    }
};

// Play の 1 フレームの各部分にかかった時間 (ms)。
struct PlayTimes {
    double scripts = 0, systems = 0, hooks = 0, services = 0, total = 0;
};

// ゲームソフトが、描くたびにパネルへ知らせるもの。
struct DebugView {
    const editor::PluginRegistry* registry = nullptr;
    platform::Gfx* gfx = nullptr;
    const Scene* scene = nullptr;  // 表示中のシーン (Play 中は Play しているコピー)
    const GameSettings* settings = nullptr;
    bool playing = false, paused = false;
    const ScriptWorld* world = nullptr;     // Play 中
    const SystemRunner* runner = nullptr;   // Play 中
    FrameMeter::Summary sceneView, gameView, update;
    bool sceneViewOpen = false, gameViewOpen = false;  // (レイアウトにないビューは描かれない)
    std::optional<Vec2> sceneMouse;  // シーンビューの上 (左上からの DIP)
    std::optional<Vec2> gameMouse;   // ゲーム画面の上 (ゲームの設定の 画面)
    size_t scriptTypes = 0;          // ビルドした DLL にあるスクリプト
    // スクリプトのビルド: 直近のもの、今動いているもの、ボタンがすること (ビルドし直す: オブジェクトを
    // すべて作り直す。読み込み直す: 一番新しい DLL をもう一度使う)。
    ScriptBuildOutcome scriptBuild;
    bool scriptsBuilding = false;
    std::function<void()> rebuildScripts, reloadScripts;
    // Play 中: 直近のフレームの各部分にかかった時間を、型ごと・システムごとに (パネルの表示中に測る)、
    // と実行した固定ステップの数。
    PlayTimes playTimes;
    std::vector<ScriptRunner::ScriptTime> scriptTimes;
    std::vector<SystemRunner::SystemTime> systemTimes;
    int fixedSteps = 0;
    FrameGraph playGraph, sceneGraph;  // Play のフレーム、シーンビューの描画
};

// ファイルフォルダの形をしたウィンドウ (ソフトのツールウィンドウ: Windows の枠なし) - デバッグ
// ウィンドウとゲームの設定ウィンドウ: シートの上に 5 つのタブ、動かすための帯、その ×。純粋な
// レイアウト - 形・当たり判定・描画が同じものを使う。
inline constexpr int kFolderTabs = 5;
inline constexpr float kFolderTabH = 32, kFolderTabGap = 4, kFolderTabRadius = 9, kFolderBodyRadius = 12;
struct FolderWindowLayout {
    ui::Rect tabs[kFolderTabs];
    ui::Rect body, close, grip, content;
};
FolderWindowLayout LayoutFolderWindow(float w, float h);
std::vector<editor::ToolRoundRect> FolderWindowShape(float w, float h);
editor::ToolHit HitFolderWindow(const FolderWindowLayout& l, float x, float y);
// ウィンドウを描く: タブ (`titles`。1 つをクリックすると `tab` が変わる)、シート、帯の `status`、
// その × (`close`)。どこに何があるかを返す (選ばれたタブが見せるものは `content` に入る)。
FolderWindowLayout DrawFolderWindow(ui::Context& c, float width, float height, std::span<const std::wstring> titles, int& tab, std::wstring_view status,
                                    const std::function<void()>& close);

class DebugPanel {
public:
    // ウィンドウ全体 (そのタブ、シート、× : `close`) - と、選ばれたタブが見せるもの (`r`)。
    void DrawWindow(ui::Context& c, float width, float height, const DebugView& v, const std::function<void()>& close);
    void Draw(ui::Context& c, const ui::Rect& r, const DebugView& v);

private:
    int tab_ = 0;
    std::map<std::wstring, bool> open_;  // たたまれているか開いているか、タイトルごと
    PadWatcher pads_;
    std::array<PadState, kPlayers> padState_{};
    double systemAt_ = -1e9;  // モニター・GPU・CPU・メモリを最後に読んだ時刻
    struct System;
    std::shared_ptr<System> system_;
    platform::CpuMeter cpuMeter_;  // 2 回の読みの間 (1 秒あけて) の CPU のふさがり具合

};

}  // namespace rm::game_production
