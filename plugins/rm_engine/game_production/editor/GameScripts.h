#pragma once
// ゲームの C++ スクリプトのエディター側: ビルド (ワーカースレッドの MSBuild)、ソースの監視、
// 一番新しい DLL の読み込み、フレームの境目での受け渡し (ホットリロード)、Visual Studio の
// プロジェクト。DLL を読み込んで動かすのはランタイム (runtime/Scripts.h) の仕事で、書き出した
// ゲームでも同じ。
//
//   .cpp を保存 ──(1 秒後)──▶ ワーカーでビルド ──▶ 成功: Scripts_0008.dll ができる
//        編集中:  GameSoftware が次のフレームで受け取り (TakeReady)、入れ替える (値は名前で)
//        Play 中: Stop まで待つ ("Stop で反映されます")。Stop でシーンを戻してから受け取る
//        失敗:    エラーをコンソールに。使っている DLL はそのまま

#include "editor/app/Worker.h"
#include "editor/plugin/Software.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/editor/ScriptBuild.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace rm::game_production {

// このプロジェクトのスクリプトのビルドに使うもの (エディターのビルドと書き出しのビルド)。
struct ScriptBuildRequest;
ScriptBuildRequest ScriptRequestFor(const editor::SoftwareHost& host);

class GameScripts {
public:
    explicit GameScripts(editor::SoftwareHost& host);

    // プロジェクトを開く: Visual Studio のプロジェクトを書き、前にビルドした一番新しい DLL を読み
    // (シーンを読むときにスクリプトが分かるように)、ソースがそれより新しければビルドを始める。
    void Open();
    // 今ビルドする (動いているビルドがあればその後でもう一度)。rebuild: 変わった所だけでなく
    // オブジェクトをすべて作り直す (デバッグウィンドウの ビルドし直す)。
    void Build(bool rebuild = false);
    // 一番新しくビルドした DLL をもう一度読む (ビルドの後と同じに: すぐ、Play 中なら Stop で) -
    // スクリプトの値は名前で引き継ぐ (デバッグウィンドウの 読み込み直す)。
    void Reload();
    // 直近のビルドがしたこと (None: プロジェクトを開いてから何もない)。
    const ScriptBuildOutcome& LastBuild() const { return last_; }
    // Scripts.sln を開く (Visual Studio)。このフレームの後で。
    void OpenVisualStudio();
    // ゲームの設定の グローバル変数 (RmGlobals.h はそこから書く: Global::<name>)。Open の前と
    // 毎フレーム設定する: 名前や型が変わるとヘッダーをもう一度書いてビルドする - 一覧が 1 秒
    // じっとしてから (名前を打つのは 1 回のビルド)。
    void SetGlobals(const std::vector<GlobalVariable>& globals);
    // 毎フレーム: Assets/Scripts で何か変わったらビルドする。保存はすぐ分かる - エディターが
    // 待機中でも (フォルダの監視が起こす) - ビルドは 0.5 秒後 (続けて数回保存しても 1 回のビルド)。
    // それまでフレームを求める。
    void Poll(ui::Context& c);
    // 使われるのを待っているビルド済みの DLL。1 回だけ渡す - `playing` の間は渡さない (Stop を待つ)。
    // 空のパス: スクリプトがすべてなくなった (何も使わない)。
    std::optional<std::filesystem::path> TakeReady(bool playing);
    // `dll` (空: なし) を専用のモジュールに読み込む。古いもののオブジェクトがなくなったら、呼んだ側が
    // Swap で入れ替える。使えないときは null (コンソールに知らせる)。
    std::unique_ptr<ScriptModule> LoadModule(const std::filesystem::path& dll);
    void Swap(std::unique_ptr<ScriptModule> next);

    const ScriptModule& Module() const { return *module_; }
    ScriptWorld& World() { return world_; }
    // Play が始まる: 前の Play からエディターで打ったものはゲームでは押されたことにしない。今
    // 押されているキー (Ctrl+P) は押された扱いではなく、押し続けている扱いにする (最初のフレームで)。
    void StartInput();
    // すべてのパッドの振動が止まる (■)。
    void StopInput() { input_.StopVibration(); }
    ScriptRunner& Runner() { return runner_; }
    bool Building() const { return building_; }
    bool WaitingForStop() const { return ready_.has_value(); }

private:
    void Finished(const struct ScriptBuildResult& r);
    ScriptBuildRequest Request() const;

    editor::SoftwareHost& host_;
    std::unique_ptr<ScriptModule> module_;
    InputReader input_;
public:
    // 今マウスがゲームビューの上にあるか (ゲームはそのときだけマウスのボタンを受け取る)。
    std::function<bool()> mouseInGame;

private:
    std::vector<GlobalVariable> globals_;
    double globalsChangedAt_ = -1;  // 書かれるのを待っている変更 (-1: なし)
    ScriptWorld world_;
    RmApi api_{};
    ScriptRunner runner_;
    editor::Worker worker_;
    bool building_ = false;
    bool again_ = false;
    bool rebuildNext_ = false;  // 動いているビルドの次のビルドは、オブジェクトをすべて作り直す
    ScriptBuildOutcome last_;
    bool toldNoVs_ = false;
    std::optional<std::filesystem::path> ready_;
    long long seenSources_ = -1;
    double lastLook_ = 0;
    double changedAt_ = -1;  // 監視が最後に変更を知らせた時刻 (-1: 待っているものなし)
    editor::UiWake wake_{[this] { host_.Redraw(); }};
    platform::FolderWatch watch_;  // Assets/Scripts、ファイル名と中身 (wake_ の後: それを呼ぶ)
    bool watching_ = false;
    double lastAliveCheck_ = 0;
};

}  // namespace rm::game_production
