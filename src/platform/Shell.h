#pragma once
// Windows のシェルの機能: ファイルダイアログ、エクスプローラー、既知のフォルダ、クリップボード、
// ファイル、時刻。
// ファイルダイアログは STA モードの COM が必要 (MTA だと Show() が永久に待つ - 前のエンジン 4-2)。

#include "platform/FileLock.h"
#include "platform/Win.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rm::platform {

// ダイアログは `initialFolder` で開く。そこがまだ無いときは、その上で一番近い、あるフォルダで開く
// (Windows が前から覚えているフォルダでは、どれも無いときを除いて、決して開かない)。
std::optional<std::wstring> PickFolder(HWND owner, std::wstring_view title, std::wstring_view initialFolder);
// `path` がフォルダとして存在すればそれ、無ければその上で一番近い、あるフォルダ。空: 無い
// (存在しないドライブ、空のパス)。
std::filesystem::path ExistingFolderAtOrAbove(const std::filesystem::path& path);
// filterSpec の例: L"*.rmpf"
std::optional<std::wstring> PickOpenFile(HWND owner, std::wstring_view title, std::wstring_view filterName,
                                         std::wstring_view filterSpec);

// エクスプローラーでファイル (またはフォルダ) を選んだ状態で開く。
void ShowInExplorer(const std::filesystem::path& path);
// フォルダをエクスプローラーで開く (中身を表示。何も選ばない)。
void OpenFolder(const std::filesystem::path& folder);
// Web のページをいつものブラウザーで開く (ヘルプのリンク)。http:// か https:// で始まるものだけ。
bool OpenWebPage(std::wstring_view url);
// Windows がそのファイルに割り当てているプログラムで開く。無ければ、どれで開くか聞く (.rmfx や
// .rmsky をテキストエディターで)。どちらもだめなら false。
bool OpenFileOutside(const std::filesystem::path& file);
// ファイル / フォルダをごみ箱へ移す (質問せず、進行状況のウィンドウも出さない)。完全には決して
// 消さない: ごみ箱が無い (USB メモリ、ネットワークのフォルダ) か、項目に対して小さすぎるとき、
// シェルは完全に消してしまうので、その項目は断って残す (*notRecyclable = true)。移せなかった
// ものが 1 つでもあれば false。理由は *error (UTF-8)。
bool MoveToRecycleBin(std::span<const std::filesystem::path> paths, std::string* error, bool* notRecyclable = nullptr);

// フォルダの下で何かが追加・削除・名前変更されたことを知らせる (Windows の変更通知)。`contents` が
// あれば、その中のファイルが書かれたことも。待たずに聞く: 変更のたびに Changed() が 1 回だけ true になる
// (エディターが書く .meta ファイルも数える - そのとき見直しても新しいものは何も見つからない)。
// `onSignal` があれば、変更が来て、まだ誰も聞いていないときに 1 回呼ばれる - システムのプールの
// スレッドの上なので、UI スレッドを起こすだけにする (editor::PostToUi)。UI スレッドが Changed() を聞く。
// 無いと、エディターが何もしていない間に起きた変更は、次にマウスを動かすまで待たされる。
class FolderWatch {
public:
    FolderWatch() = default;
    FolderWatch(const FolderWatch&) = delete;
    FolderWatch& operator=(const FolderWatch&) = delete;
    ~FolderWatch();  // (まだ動いている onSignal を待つ)
    // false: そのフォルダは見張れない (Changed() は false のまま)
    bool Start(const std::filesystem::path& folder, bool contents = false, std::function<void()> onSignal = {});
    bool Changed();
    // 見張りが、そのパスのフォルダをもう見ていない: 止まった、またはフォルダが消された / ごみ箱へ
    // 移された / 作り直された (見張りは古いほうを追う)。持ち主がもう一度始める。聞くためにフォルダを
    // 開く - 多くても 1 秒に 1 回で、毎フレームではない。
    bool Lost() const;

private:
    static void CALLBACK Signaled(void* self, BOOLEAN timedOut);
    void Arm();
    void Close();

    HANDLE handle_ = nullptr;
    HANDLE wait_ = nullptr;
    std::function<void()> onSignal_;
    std::filesystem::path folder_;
    std::optional<std::pair<DWORD, uint64_t>> identity_;  // 見張っているフォルダ (Lost を見る)
};

enum class KnownFolder { Documents, RoamingAppData, LocalAppData };
std::filesystem::path GetKnownFolder(KnownFolder which);

bool SetClipboardText(HWND owner, const std::wstring& text);
std::wstring GetClipboardText(HWND owner);

// Win32 エラーコードの文章 (UTF-8。末尾の改行なし)。
std::string ErrorText(DWORD code);

// ReadFileBytes が読み、WriteFileAtomic が書く、一番大きいファイル: 両方で 1 つの上限にして、
// 開き直せないものを保存できないようにする (上限を超えた .rmpf は、以前は保存できたのに開けなかった)。
inline constexpr uint64_t kMaxFileBytes = 256ull << 20;  // 256 MB

// ファイルを丸ごと読む (多くても kMaxFileBytes)。失敗したら nullopt を返し、*error (UTF-8) を入れる。
std::optional<std::string> ReadFileBytes(const std::filesystem::path& path, std::string* error);
// 先に "<path>.tmp" に書き、それから 1 回でファイルを置き換える。途中でクラッシュしても、
// 書きかけのプロジェクトファイルが残らない。kMaxFileBytes を超えるものは断る
// (何も書かない。ファイルはそのまま)。
bool WriteFileAtomic(const std::filesystem::path& path, std::string_view data, std::string* error);

// 画面上の点 (物理ピクセル) の下にある最上位ウィンドウ。`exclude`、隠れているもの、最小化されたもの、
// クリックが通り抜けるもの、cloaked のウィンドウは飛ばす。パネルをウィンドウ間でドラッグする間に使う。
HWND TopLevelWindowAt(POINT screen, HWND exclude = nullptr);
// 見えているウィンドウの矩形 (Windows 10/11 の見えないサイズ変更用の縁を除く)。
RECT VisibleWindowRect(HWND hwnd);

// 動いている exe のフォルダ (エンジン自身のファイル - Plugins/ など - はその隣にある)。
std::filesystem::path ExeDirectory();
// エンジンのフォルダ: exe をソースからビルドしたときはソースのフォルダ (RM_Engine.sln があるもの)、
// そうでなければ exe のフォルダ。新しいプロジェクトは、既定ではその Project/ に作る。
std::filesystem::path EngineDirectory();

// Windows の「アプリモード」がライトである (設定 > 個人用設定 > 色)。
bool SystemPrefersLight();

// ファイルの SHA-256 を小文字の 16 進で返す。読めないとき (無い など) は nullopt。
std::optional<std::string> Sha256File(const std::filesystem::path& path);

// 適当な開始点からの秒数 (QueryPerformanceCounter)。
double NowSeconds();

}  // namespace rm::platform
