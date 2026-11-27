#pragma once
// 2D の絵に描く物を集める 1 か所 (憲法 3): 2D のシーンビュー・ゲームビュー (editor/SceneView2D) と
// 書き出したゲーム (runtime/GameDraw2D) が、同じ関数で物を置き、同じ順に並べる。
//
//   PlaceScene      Transform 2D を持つエンティティの場所 (親子をたどったワールド行列・並び順・階層の順)。
//                   スプライトは画像 (`images` で引く) から大きさとアトラスのセルを決める
//   CollectDraws    スプライトと、ほかのプラグインが口 kWorldDraw2DPoint に差し出す描く物 (タイルの層) を
//                   1 本の列にし、前後の順 (DrawKey: ワールドの並び順・自分の並び順・階層の順) に並べる
//
// 画像の読み方はホストごと (エディター: ファイルを作業スレッドで / ゲーム: 焼いた .rmimage) なので、
// 引く関数 (ImageLookup2D) だけを受け取る。

#include "base/Ref.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/space2d/runtime/Renderer2D.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rm::game_x_2d {

// ホストが読んだ画像。
struct Image2D {
    ID2D1Bitmap* bitmap = nullptr;
    uint32_t width = 0, height = 0;  // ファイルのピクセル数 (ビットマップはもっと小さいことがある: kMaxImageSize)
    bool sharp = false;              // 取り込みの設定の くっきり
};
// アセットの参照 -> 読んだ画像 (null: 無い・まだ・読めない)。
using ImageLookup2D = std::function<const Image2D*(const AssetRef&)>;

// Transform 2D を持つエンティティの、このフレームでの状態。
struct Placed2D {
    int id = 0;
    space2d::Mat3 world, parentWorld;
    int order = 0;     // ワールドでの並び順 (親の分を足したもの)
    size_t tree = 0;   // 階層での位置 (後ろほど手前)
    bool active = true;
    bool sprite = false;  // スプライトがある
    int subOrder = 0;     // スプライトの 並び順
    space2d::SpriteDraw draw;  // スプライトの描き方 (sprite のとき)
    std::optional<Camera2D> camera;  // カメラ (2D) である
};

// Transform 2D を持つエンティティ (階層の順)。
std::vector<Placed2D> PlaceScene(const game_production::Scene& scene, const ImageLookup2D& images);
// `placed` の中のエンティティ `id` (無ければ null)。
const Placed2D* FindPlaced(std::span<const Placed2D> placed, int id);

// --- 口: ほかのプラグインが 2D の絵に描く物を足す --------------------------------------------
// RuntimeRegistry の口: 中身は WorldDraw2DInfo (static)。ビュー (とゲーム) ごとに 1 つ作り、毎フレーム頼む。
inline constexpr char kWorldDraw2DPoint[] = "rm_engine.game_x_2d.world_draw";

struct WorldDraw2DFrame {
    const game_production::Scene& scene;
    std::span<const Placed2D> placed;
    ID2D1DeviceContext* dc = nullptr;
    uint64_t generation = 0;  // GPU のデバイスの代 (platform::Gfx::Generation)。変わったら GPU の物を捨てる
    const ImageLookup2D& images;
    // アセットの中身 (エディター: まだ書いていない編集も / ゲーム: .rmpak の物)。無ければ nullopt。
    std::function<std::optional<std::string>(const AssetRef&)> asset;
    space2d::Camera2D camera;     // 見ている所 (見える物だけを描くため)
    space2d::ScreenRect viewport;
    bool game = false;  // ゲームの絵 (ゲームビュー・書き出したゲーム): 有効でない物は描かない (シーンビュー: 薄く)
    // 時計 (秒。増えるだけ): アセットの中身を読み直す間を空けるため (毎フレームはファイルを読まない)。
    double time = 0;
};

// 描く物 1 つ。`key` はスプライトと同じ前後の順。
struct Drawn2D {
    space2d::SpriteDraw draw;
    space2d::DrawKey key;
    int entity = 0;
};

class WorldDrawer2D {
public:
    virtual ~WorldDrawer2D() = default;
    // このフレームに描く物を `out` に足す。
    virtual void Collect(const WorldDraw2DFrame& f, std::vector<Drawn2D>& out) = 0;
    // エンティティ `entity` が描いた物が、平面の点 `world` の上にあるか (シーンビューのクリックで選ぶ)。
    virtual bool Covers(const game_production::Scene& scene, std::span<const Placed2D> placed, int entity, Vec2 world) {
        (void)scene, (void)placed, (void)entity, (void)world;
        return false;
    }
};

struct WorldDraw2DInfo {
    std::unique_ptr<WorldDrawer2D> (*create)() = nullptr;
};

// 口に差し出された描き手を、ビュー (とゲーム) ごとに 1 組持つ。
class WorldDrawers2D {
public:
    // まだ作っていなければ、`registry` の描き手を作る。
    void Sync(const RuntimeRegistry& registry);
    std::span<const std::unique_ptr<WorldDrawer2D>> All() const { return drawers_; }

private:
    bool made_ = false;
    std::vector<std::unique_ptr<WorldDrawer2D>> drawers_;
};

// スプライト (`f.placed` の) と描き手の物を、奥から手前へ並べて `out` に入れる (前の中身は捨てる)。
// シーンビュー (f.game = false) では有効でないスプライトは薄く、ゲームでは描かない。
void CollectDraws(const WorldDraw2DFrame& f, std::span<const std::unique_ptr<WorldDrawer2D>> drawers, std::vector<Drawn2D>& out);

}  // namespace rm::game_x_2d
