#pragma once
// ゲーム制作 × 3D - 3D 空間のレンダラー (RenderFrame::sky) 用に整えたシーンの 空: 空を見せる
// すべての絵 (エディターのシーンビュー、ゲームビュー、カメラプレビュー、書き出したゲーム) に
// 描き手は 1 つ。どれでも空が同じに見える。画像とユーザーの .rmsky の出どころは違う
// (エディターのファイル / ゲームの .rmpak): 呼ぶ側が言う (Sources)。
//
//   SkyPainter painter;                                   // 絵の持ち主ごとに 1 つ
//   const SkyLook look = LookOf(*sky, seconds);
//   if (const space3d::SkyDraw* d = painter.Prepare(renderer, *sky, look, sources)) frame.sky = d;
//   FollowSky(look, lights, ambient);                    // 光を空に合わせる

#include "base/Ref.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

namespace rm::game_x_3d {

class SkyPainter {
public:
    struct Sources {
        // GPU 上の画像 (色の画像: リニアの光として読む)。準備ができていないか読めない間は null
        // (呼ぶ側が理由を知らせる)。
        std::function<space3d::TextureRef(const AssetRef&)> picture;
        // .rmsky ファイルのテキスト。無いときは nullopt。
        std::function<std::optional<std::string>(const AssetRef&)> code;
        // メッセージ用の名前 (ファイルの名前)。
        std::function<std::string(const AssetRef&)> name;
        // .rmsky を読み直す間隔 (秒。編集は開き直さなくても出る)。< 0: 1 度だけ。
        double readEvery = 0.25;
        double now = 0;  // 秒。始まりはどこでもよい (readEvery 用)
    };

    // 何よりも後ろに描くもの。null: クリアの色 (なし か、空が描けないとき: シェーダーが
    // コンパイルできない - コンソールに 1 度だけ知らせる - か、.rmsky が無い)。
    const space3d::SkyDraw* Prepare(space3d::Renderer& renderer, const Sky& sky, const SkyLook& look, const Sources& sources);

private:
    std::string Code(const Sky& sky, const SkyLook& look, const Sources& sources, std::string& fileName);

    space3d::SkyDraw draw_;
    std::string compiled_;  // draw_.program を作った HLSL
    bool failed_ = false;   // compiled_ がコンパイルできなかった: コードが変わるまで再び試さない
    // 最後に読んだユーザーの .rmsky と、その時刻。
    std::string codeGuid_;
    std::optional<std::string> code_;
    double codeReadAt_ = -1e9;
    std::unordered_set<std::string> told_;  // 1 度だけ出したメッセージ
};

}  // namespace rm::game_x_3d
