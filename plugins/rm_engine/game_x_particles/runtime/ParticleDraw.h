#pragma once
// ゲーム制作 × パーティクル - 3D の絵 (ゲーム制作 × 3D の WorldDrawer) に出すパーティクル。
// パーティクルを持つエンティティごとに EffectPlayer を 1 つ作り、絵の時間で進めて、絵の中に描く。
//
//   再生中 (Play、書き出したゲーム)   始めから出す がついたパーティクルすべて
//   再生していない (エディター)        選んだもの (見るため)。選んだ時に始めから
//
// エフェクトはアセットから 1 度だけ読み、あとは時々見直す (その間に保存されたファイル -
// パーティクルタブや別のソフトで - は、今の内容で再生される)。問題 (読めないエフェクト、
// コンパイルできないシェーダー) はコンソールに 1 度だけ知らせる。

#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/particles/runtime/ModuleSource.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticleShader.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <functional>
#include <memory>
#include <set>
#include <optional>
#include <string>
#include <unordered_map>

namespace rm::game_x_particles {

std::unique_ptr<game_x_3d::WorldDrawer> CreateParticleDraw();

// エフェクトが名前で挙げるモジュール: 組み込みのものと、`read` で読む (.rmfx の) ファイル。
// 時々見直す (その間に保存されたファイルは新しい元として扱い、プレイヤーがシェーダーを作り直す)。
class ModuleFiles {
public:
    using Read = std::function<std::optional<std::string>(const AssetRef&)>;
    using Name = std::function<std::string(const AssetRef&)>;
    // `often`: 4 分の 1 秒ごとに見直す (エディター)。そうでなければ 1 秒半ごと。
    std::shared_ptr<const particles::ModuleSource> Find(const particles::ParticleModule& m, std::string& file, const Read& read, const Name& name,
                                                        bool often);

private:
    struct Entry {
        std::shared_ptr<const particles::ModuleSource> source;
        std::string bytes;
        std::string name;
        double lookedAt = -1e9;
    };
    std::unordered_map<std::string, Entry> files_;  // アセット ID ごと
};

// エミッターが見せる絵 (その 絵) を GPU に置いたもの: `read` で読み (絵のファイル、
// またはゲームに焼いたもの)、時々見直す。読めないものは 1 度だけ知らせる。
class ParticleImages {
public:
    ID3D11ShaderResourceView* Find(const AssetRef& ref, platform::Gfx& gfx, const ModuleFiles::Read& read, bool often);

private:
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        std::string bytes;
        double lookedAt = -1e9;
        uint64_t generation = 0;
    };
    std::unordered_map<std::string, Entry> images_;  // アセット ID ごと
    std::set<std::string> told_;
};

// アセットを読み直す間隔 (秒): 止まっているエディターの時と、それ以外の時。
inline constexpr double kLookOften = 0.25, kLookSeldom = 1.5;
double NowSeconds();

// パーティクルのシェーダーが受け取る形にした、3D の絵のカメラ。
particles::ViewConstants ViewOf(const space3d::ExtraDrawContext& c);

}  // namespace rm::game_x_particles
