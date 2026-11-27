#pragma once
// ゲーム制作 × タイルマップ (エディター) - パネル「タイル」とシーンビューの塗る道具が分け合うもの:
//   TileBrush       選んだもの (タイルセット・絵の元・パレットの四角・スポイトで取ったスタンプ) とパネルの見え方。
//                   人ごとの見え方 (UserSettings: パネル「タイル」の SaveState)。プロジェクトには入れない
//   TilesetReader   タイルセットの中身 (エディターが今持っているもの) を、間を空けて読み直す
//   TargetLayer     塗る層 = 最後に選んだ「タイルの層」のエンティティ

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/tilemap/runtime/TilePaint.h"
#include "rm_engine/tilemap/runtime/Tileset.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace rm::game_x_tilemap {

RM_STRUCT()
struct TileBrushState {
    RM_PROPERTY() std::string tileset;  // 選んだものが属するタイルセット (アセットの id)
    RM_PROPERTY() int source = 0;       // 絵の元の番号 (0: 選んでいない)
    RM_PROPERTY() int column = 0;
    RM_PROPERTY() int row = 0;
    RM_PROPERTY() int width = 0;        // パレットで選んだ四角 (0: 選んでいない)
    RM_PROPERTY() int height = 0;
    RM_PROPERTY(Min=0.25, Max=16) float zoom = 2;  // パレットの拡大 (1 ピクセル = zoom DIP)
    RM_PROPERTY() int page = 0;         // 0: パレット / 1: タイルセット
    RM_PROPERTY() int tilesetPage = 0;  // タイルセットの中: 0: 切り方 / 1: タイル (当たり・タグ・独自データ) / 2: 独自データの欄
    RM_PROPERTY() int paint = 0;        // タイル: 0: 選ぶ / 1〜: 当たりの形を塗る (tilemap::TileShape + 1)
};

struct TileBrush {
    TileBrushState state;
    tilemap::Stamp picked;  // スポイトで取ったもの (空でなければ、パレットの選択より先に使う)
    // 今塗るもの (選んだタイルセット `tileset` のもの。違うタイルセットの層には塗らない: 空)。
    tilemap::Stamp Current(const std::string& tileset) const;
};
TileBrush& Brush();

class TilesetReader {
public:
    // `ref` のタイルセット (無い・読めない: null)。`now` (秒) が前に読んでから 0.25 秒たつまでは前のもの。
    const tilemap::Tileset* Get(editor::SoftwareHost& host, const AssetRef& ref, double now);
    // 読んだときの中身 (Undo の「前」)。
    const std::string* Bytes(const AssetRef& ref) const;

private:
    struct Entry {
        std::string bytes;
        std::optional<tilemap::Tileset> set;
        double lookedAt = -1;
        bool told = false;
    };
    std::unordered_map<std::string, Entry> entries_;
};

// 塗る層: 選択の中の、最後に選んだ「タイルの層」のエンティティ (0: 無い)。
int TargetLayer(const game_production::SceneViewHost& host);

}  // namespace rm::game_x_tilemap
