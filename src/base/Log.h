#pragma once
// プロセス全体のログ (純粋)。プラットフォームのコードがシンク (デバッガー出力、ログファイル) を足し、
// エディターのコンソールは Snapshot() を読む。
//
// どのスレッドからも書ける (モデルを読むワーカー、サムネイルを作るワーカーなど)。シンクは 2 種類:
//   Delivery::AnyThread  書いたスレッドですぐ呼ぶ - シンクはスレッドセーフでなければならない
//                        (デバッガー出力、専用のロックを持つログファイル)
//   Delivery::Queued     持ち主のスレッドが DeliverQueued() を呼ぶまで、エントリーはキューで待つ
//                        (UI 用: UI スレッドしか触れないウィンドウを触るため)。キューに何か入ると
//                        SetWake が呼ばれ、待っているスレッドが起きる。
// RemoveSink は、シンクが他のスレッドで呼ばれなくなってから戻り、その後は二度と呼ばれない
// (オブジェクトがデストラクターで自分のシンクを外してよい)。テストは tests/TestBase.cpp。

#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <vector>

namespace rm::log {

enum class Level : uint8_t { Info, Warn, Error };

struct Entry {
    Level level = Level::Info;
    double time = 0.0;  // 最初のログ呼び出しからの秒数
    uint64_t seq = 0;   // 1, 2, 3 ...
    std::string text;   // UTF-8
};

using Sink = std::function<void(const Entry&)>;

enum class Delivery : uint8_t { AnyThread, Queued };

int AddSink(Sink sink, Delivery delivery = Delivery::AnyThread);
void RemoveSink(int id);

// Queued のシンク: 待っているエントリーを、今このスレッドで渡す (書いた順)。待つのは最大
// 2000 件で、古いものは捨てる (どのシンクにも新しい分は届く)。
void DeliverQueued();
// 空だったキューでエントリーが待ち始めたときに (書いたスレッドで、ログのロックの外で) 呼ばれ、
// DeliverQueued を呼ぶスレッドを起こす。空: 何もしない。
void SetWake(std::function<void()> wake);

void Write(Level level, std::string text);

// 新しいエントリー (最大 2000 件まで残す)。
std::vector<Entry> Snapshot();
uint64_t LastSeq();
// プログラムが始まってから (または ResetCounted から) 書かれたメッセージの、レベルごとの数
// (デバッグパネル: 数を 0 に戻す)。メッセージ自体は残る。
struct Counts {
    uint64_t info = 0, warn = 0, error = 0;
};
Counts Counted();
void ResetCounted();

template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace rm::log
