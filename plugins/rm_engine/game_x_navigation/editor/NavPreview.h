#pragma once
// ゲーム制作 × ナビゲーション (エディター) - 焼いたナビメッシュ (.rmnav) を読んで覚えておく所。シーンビューの線と
// 「ナビ」タブの地図・経路を試すが使う。ファイルが変わった (焼き直した・別の PC から来た) ら読み直す。UI スレッドだけ。

#include "base/Ref.h"
#include "editor/plugin/Software.h"
#include "rm_engine/navigation/runtime/NavFile.h"
#include "rm_engine/navigation/runtime/Query.h"

#include <bitset>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rm::game_x_navigation {

struct NavPreview {
    bool ok = false;
    std::string error;                // 読めなかったわけ (日本語)
    navigation::NavFileHead head;
    navigation::NavData info;         // 範囲・体の形・区画の数 (区画のデータは持たない)
    navigation::NavMesh mesh;         // 経路を試す
    std::vector<navigation::NavEdge> edges;
    std::vector<navigation::NavPolygon> polygons;
    size_t polygonCount = 0;
    std::bitset<64> blocked;          // mesh に今かけてある通れないエリア

    // 通れないエリアを今の設定に (変わったときだけ多角形の印を変える)。
    navigation::NavMesh& MeshWith(const std::bitset<64>& b) {
        if (b != blocked) mesh.SetBlockedAreas(b), blocked = b;
        return mesh;
    }
};

// `ref` の焼いた物 (ファイルが無い: null)。
NavPreview* FindPreview(editor::SoftwareHost& host, const AssetRef& ref);
// 焼いた直後に (ファイルを読み直さずに) 覚える。
void RememberPreview(const AssetRef& ref, const std::filesystem::path& file, const navigation::NavData& data, const navigation::NavFileHead& head);

}  // namespace rm::game_x_navigation
