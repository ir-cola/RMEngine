#pragma once
// 開いているプロジェクト (その Assets/ フォルダー) のアセットをエディターに出す。id
// (core/asset) をディスクに合わせて最新に保ち、アセットパネルのファイル操作もする。開いている
// プロジェクトにつき 1 つ - どのソフトのアセットパネルも同じフォルダーを見せる。
//
// id はファイルについて行く: ここでの操作は .meta をファイルと一緒に動かし / 改名する。外 (エクスプローラー)
// で行った変更は、次にエディターが描くときに見つけ (platform::FolderWatch)、
// 合わせ直す (asset::Reconcile)。
//
// フォルダーを調べるのは Worker (editor/app/Worker) で走る: フォルダーと .meta ファイルを読み、
// 計画を立てる (asset::Look) ので、描画は止まらない。計画は UI スレッドに戻り、UI スレッドが
// その .meta ファイルを書き、新しい id をまとめて置く (ApplyLook)。調べるたびと、ここで行う
// 変更のたびに新しい番号が付く (asset::LookNumber): 番号がもう最新でない結果 - フォルダーが
// また変わった、またはその間にエディターが何かを改名 / 移動 / 削除した - は捨てられ、
// もう一度調べるよう頼む。その間もパネルとインスペクターは最後の結果で動き続ける
// (Looking(): パネルは「調べています」と言う)。ここでの操作は自分の変更をすぐ id に入れ
// (パネルは待たずに追いつく)、そのあと調べを頼む。ファイル操作はプロジェクトのデータではない:
// ディスクですぐ起き、Ctrl+Z では戻らず、プロジェクトを未保存にもしない (削除はごみ箱へ行く
// - それが戻る道)。

#include "base/Json.h"
#include "core/asset/AssetFiles.h"
#include "core/asset/AssetIndex.h"
#include "editor/app/Worker.h"
#include "platform/Shell.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace rm::editor {

class AssetLibrary {
public:
    // `onChange`: 誰も頼んでいないのにフォルダーで何か変わったときに (どのスレッドからでも) 呼ばれる。
    // 持ち主はこれで UI スレッドを起こす (editor::UiWake)。エクスプローラーでの変更がすぐ見える。
    explicit AssetLibrary(std::filesystem::path root, std::function<void()> onChange = {});
    ~AssetLibrary();  // まだ待っている取り込みの設定を書く

    const std::filesystem::path& Root() const { return root_; }
    const asset::Index& Index() const { return index_; }
    // Index().Entries() への添字。フォルダーツリーの順
    std::span<const size_t> Order() const { return order_; }
    // id やパスが変わったかもしれないたびに変わる (リフレッシュ)。
    uint64_t Version() const { return version_; }

    // フォルダーを調べるよう頼む (なければ作る)。Worker で行い、まだ続いている調べは捨てる。
    // 前回の調べから変わっていない .meta (サイズと時刻が同じ) は読み直さない。自分で書く .meta に
    // ついてのフォルダー監視の知らせは捨てる。
    void Refresh();
    // プロジェクトを開くとき、最初の調べをここですぐ (UI スレッドで) 行う。各ソフトはその直後に
    // データを読み、Prefab やモデルなどを id で探す。まだ id がないと、どれもみな「見つからない」に
    // なってしまう。これ以降の調べはすべて Worker で走る。
    void LookNow();
    // 調べが進んでいる (またはまだ 1 つも終わっていない): id は最後の調べのもの。
    bool Looking() const { return looking_; }
    // 調べが 1 度使われた (id はフォルダーのもので、まだ調べていないから空、ということではない)。
    bool Ready() const { return ready_; }
    // 調べの結果を置いた後に UI スレッドで呼ばれる (id を見せている物を描き直す)。
    std::function<void()> onChanged;
    // 前回の調べの後に、フォルダーの下で何かが足された / 消された / 改名されたら、調べるよう頼む。
    // 少し静かになってから (プログラムが自前のファイル経由でしばらく保存している間は、それが
    // 終わってから見つかる)。頼んだら true。
    bool Poll();
    // 変更を見つけたが、フォルダーはまだ静かになっていない: 持ち主が次のフレームを頼む。
    bool Settling() const { return changedAt_ >= 0; }
    // エディターが Assets/ の中に書いたばかりのファイルの id (`path` は Assets/ からの相対):
    // 調べを待たずに、すぐ与える - その .meta をここで書く。与えられないときは ""。
    std::string AdoptFile(const std::string& path);

    // アセットの取り込みの設定の今の姿 - まだ書かれずに待っているインスペクターでの変更も含む。
    json::Value Import(const std::string& guid) const;
    // その変更: 保持し、編集が終わったとき (ドラッグを離した、入力が済んだ) に FlushImports が
    // .meta に書く。1 歩ごとには書かない。そのアセットを読む側は、そのときに読み直す。
    void QueueImport(const std::string& guid, json::Value import);
    // エディターが今持っているアセットの中身 (ソフト独自の種類のファイル - パーティクルの
    // エフェクトなど - を編集して、まだ書いていない物)。なければファイル。ないときは nullopt。
    std::optional<std::string> Content(const std::string& guid) const;
    // その編集: 保持し、FlushImports がファイルに書く (取り込みの設定と同じ)。
    void QueueContent(const std::string& guid, std::string bytes);
    bool HasQueuedImports() const { return !queuedImports_.empty() || !queuedContents_.empty(); }
    // 待っている物を、それぞれの .meta とファイルに書く (そのためにフォルダーをもう一度調べはしない)。
    void FlushImports();

    std::filesystem::path FullPath(const asset::Entry& e) const;
    std::wstring NameOf(const asset::Entry& e) const;      // ファイル名
    std::wstring FolderOf(const asset::Entry& e) const;    // "Assets/Models" (どこにあるか)

    // ファイル操作。どれも、何が悪かったかを (使っている言語で) 返す (空 = 済んだ)。コンソールには
    // 日本語で出す。id はファイルについて行く。
    std::wstring Rename(const std::string& guid, const std::string& newNameUtf8);
    // フォルダー `folderGuid` の中へ ("" = Assets/ の一番上)。
    std::wstring Move(std::span<const std::string> guids, const std::string& folderGuid);
    // `whereGuid` (フォルダー、またはファイルのあるフォルダー。"" = 一番上) の中に、新しいフォルダー
    // "新しいフォルダ" を作る (使われていれば 2、3 ...)。*made = その id。
    std::wstring NewFolder(const std::string& whereGuid, std::string* made);
    // ごみ箱へ、その .meta ファイルと一緒に。
    std::wstring Delete(std::span<const std::string> guids);

private:
    std::filesystem::path root_;
    asset::Index index_;
    std::vector<size_t> order_;
    std::function<void()> onChange_;
    platform::FolderWatch watch_;
    bool watching_ = false;
    double changedAt_ = -1;  // 監視が、まだ調べていない変更を最後に知らせた時刻
    double lastAliveCheck_ = 0;  // 監視に、まだフォルダーが見えているか最後に尋ねた時刻
    // ここで .meta ファイルを書いた後: その知らせは、静かになったらもう 1 度調べるよう頼む
    // (捨てない - 同じ時に外で行った変更も一緒に捨てられてしまう)。
    void SettleAfterOwnWrites();
    uint64_t version_ = 0;
    std::set<std::string> lastNotes_;  // 最後の調べがコンソールに伝えた物
    std::map<std::string, json::Value> queuedImports_;
    std::map<std::string, std::string> queuedContents_;  // guid -> 書くのを待っているファイルの中身  // guid -> 書くのを待っている取り込みの設定

    // 調べの結果。UI スレッドで: `number` が最新の間だけ使う。
    void ApplyLook(uint64_t number, asset::Plan plan);
    // ここで (操作で) 変わった id: すぐ置く。そのあと頼んだ調べが確かめる。
    void SetEntries(std::vector<asset::Entry> entries);
    void MovedInIndex(const std::string& from, const std::string& to);  // `from` とその下の物
    void RemovedFromIndex(const std::string& path);                     // `path` とその下の物

    // 最後の調べが読んだ .meta ファイル。Worker の調べと共有する (調べが読むのは同時に 1 つだけ:
    // まだ走っている古い調べは、止められたと気づくまでロックを持つ)。
    struct CacheBox {
        std::mutex mutex;
        asset::MetaCache cache;
    };
    std::shared_ptr<CacheBox> cache_ = std::make_shared<CacheBox>();
    asset::LookNumber looks_;
    bool looking_ = false;
    bool ready_ = false;
    Worker worker_;  // 最後に置く: 最初に壊れる - まだ調べが post する物は捨てられ、なくなったライブラリーには当てない
};

}  // namespace rm::editor
