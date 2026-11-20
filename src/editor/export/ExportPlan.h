#pragma once
// ゲームの書き出しの純粋な部分 (設計 NewEngineBluePrint 11-b / 11-b-2、RME_PLATFORM 5-a
// "書き出しを作り直す"): どのプラグインを入れるか、exe の名前、それらをリンクする小さな生成 main、
// そして作った PC のパスが残っていないかの確認。tests/TestExport.cpp でテストする。
// ファイルとリンカーは Exporter.cpp の仕事。
//
//   1  .rmpf に記録された使用プラグイン  ->  + その前提とコネクタ (ExportPlugins)
//   2  ゲームキットの、核と各プラグインの runtime/ の .lib (エンジンと一緒にビルドする)
//   3  GameMainSource: それらを並べるだけの .cpp (登録とリフレクション型) -
//      静的ライブラリの中身が、名前を挙げていないせいでリンカーに捨てられないようにする
//   4  アイコンとゲームの名前を入れたリソース、そして link  ->  <exe 名>.exe (その名前のまま作る)

#include "base/Json.h"
#include "core/PluginId.h"
#include "editor/plugin/PluginManifest.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::exporter {

// ゲーム名から作る exe 名の初期値: ファイル名に使えない文字を除き、前後の空白とドットを
// 取り、最大 64 文字。何も残らない (または CON など Windows が予約する名前) なら "Game"。
// 黙って変えることはない: 書き出し画面で専用の欄に見せる。
std::string ExeNameFrom(std::string_view gameName);
// `name` が exe の名前にできない理由 (日本語)。できるなら空。
std::string ExeNameProblem(std::string_view name);

// 書き出したゲームがリンクするプラグイン: 記録されたものと、その前提と必要なコネクタ
// (インストールしたときと同じ: 前提が先、それぞれ 1 回ずつ)。記録にあってこのエンジンに
// 無いプラグインは *missing に入る。
std::vector<PluginId> ExportPlugins(const std::vector<PluginId>& recorded, const std::vector<PluginManifest>& available,
                                    std::vector<PluginId>* missing);

// ゲームにリンクするライブラリ 1 つ: 核のもの (id なし) か、プラグインの runtime 部分。
struct GameLib {
    std::optional<PluginId> plugin;
    bool registers = false;                // RegisterRuntime がある (RM_RUNTIME_ENTRY)
    std::vector<std::string> types;        // リフレクション型のシンボル (rmgen: T_...)
    std::vector<std::string> enums;        // ... と enum のシンボル (E_...)
};
// プラグインの runtime 登録の C 名 (core/RuntimeRegistry.h の RM_RUNTIME_ENTRY)。
std::string RuntimeEntryName(const PluginId& id);
// 生成する main: この順 (前提が先) のプラグイン、ライブラリの全リフレクション型
// (reflect::AllTypes / AllEnums)、ゲームのタイトル、書き出しのビルド番号。
std::string GameMainSource(const std::vector<GameLib>& libs, std::string_view titleUtf8, uint64_t build);

// ライブラリのリフレクションのシンボル。ヘッダーに対する rmgen のファイルから読む
// (行 "extern const ::rm::reflect::TypeDesc T_x = {" / "... EnumDesc E_x = {")。
void ReadSymbols(std::string_view generatedCpp, std::vector<std::string>& types, std::vector<std::string>& enums);

// ゲームの exe のリソーススクリプト (rc /c65001: UTF-8): アイコン (隣の game.ico、
// `icon` のとき) と、ファイルの詳細に出る名前 (説明 / 製品名 / 元のファイル名)。
std::string GameResourceSource(std::string_view titleUtf8, std::string_view exeName, bool icon);
// PNG 画像 1 枚を入れた .ico (Windows Vista 以降はアイコンの中の PNG を読む)。
// `png` が PNG でなければ nullopt。
std::optional<std::string> IcoFromPng(std::string_view png);
// アイコン画像から作る .ico: .ico ならそのまま (全サイズの画像ごと)、PNG ならその中に
// 入れる (IcoFromPng)。それ以外は nullopt。
std::optional<std::string> IconFile(std::string_view bytes);

// 書き出しの番号 (exe と .rmpak が持つ): .rmpak に入るものから作るので、同じゲームを
// もう一度書き出すと同じ番号 (つまり同じファイル) になる。0 にはならない。
uint64_t BuildNumber(const std::vector<std::pair<std::string, std::string>>& entries);

// 書き出したゲームのパス: そのフォルダからの相対で、区切りは "/" か "\"、外には出ない。
bool PortablePath(std::string_view path);
// `v` の中の、作った PC のパスに見える文字列 (ドライブ "C:\"、"\\server"、ルートから
// 始まるパス): ゲームのデータを書く前に書き出しが確かめるもの。
std::vector<std::string> AbsolutePathsIn(const json::Value& v);

}  // namespace rm::editor::exporter
