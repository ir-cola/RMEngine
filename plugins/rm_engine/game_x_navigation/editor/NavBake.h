#pragma once
// ゲーム制作 × ナビゲーション (エディター) - 「焼く」1 回の仕事。UI スレッドでシーンを写し (BakeJob)、
// 作業スレッド (editor::Worker) で 焼く物を集める → 焼く → .rmnav を書く。結果 (BakeOutcome) は UI スレッドで
// 部品の「焼いた物」に入れる (初めて焼いたときだけ Undo に積む: docs/NAVIGATION.md 6-3)。

#include "rm_engine/game_x_navigation/runtime/NavScene.h"
#include "rm_engine/navigation/runtime/NavFile.h"

#include <filesystem>
#include <functional>
#include <map>
#include <stop_token>
#include <string>
#include <vector>

namespace rm::game_x_navigation {

struct BakeJob {
    game_production::Scene scene;  // 写し
    NavVolume volume;
    NavAgentType type;
    std::vector<const NavSourceInfo*> sources;              // (差し出された static な物)
    std::map<std::string, std::filesystem::path> files;     // シーンが使うアセットの id -> ファイル
    std::filesystem::path out;                              // 書く .rmnav
    navigation::NavFileHead head;                           // 版・型・指紋 (書く前に前のファイルの知らないキーを足す)
    bool partial = false;                                   // 変わった区画だけ焼き直す (前のファイルの区画を使う)
};

struct BakeOutcome {
    bool ok = false;
    bool stopped = false;
    std::string error;   // 日本語
    int entity = 0;      // 部品「ナビメッシュ」のエンティティ
    std::filesystem::path file;
    navigation::NavData data;
    navigation::NavFileHead head;
    size_t triangles = 0;  // 焼いた三角形の数
    double ms = 0;         // かかった時間
    int bakedTiles = -1;   // 焼き直した区画の数 (-1: 全部)
    bool partial = false;
};

// 作業スレッドで。`progress(済んだ区画, 全部)` はどのスレッドからも来る。
BakeOutcome RunBake(const BakeJob& job, std::stop_token stop, const std::function<void(int, int)>& progress);

// 焼いた物のファイルの名前に使える文字だけにした範囲の名前 (空: "NavMesh")。
std::string FileNameOf(std::string_view name);

}  // namespace rm::game_x_navigation
