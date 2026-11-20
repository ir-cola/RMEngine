#pragma once
// パーティクル - GPU で再生している 1 つのエフェクト (Direct3D 11、フィーチャーレベル 11_0: コンピュートシェーダー)。
//
//   EmitterClock   エミッターがこのフレームに出す粒の数 (レート、バースト、長さ、ループ)。純粋:
//                  tests/TestParticles.cpp でテストする
//   EffectPlayer   エフェクトのエミッターを GPU 上で動かす: それぞれ粒のバッファー (リング: 新しい粒は
//                  次の枠に入り、いっぱいなら一番古いものが消える)、新しい粒を始めるコンピュート
//                  シェーダー、全部を動かすコンピュートシェーダー、カメラに向いた四角の描画
//                  (ParticleShader.h)。シェーダーはテキストとデバイスごとに 1 度だけ作り、すべての
//                  プレイヤーで共有する (同じエフェクトの松明が百本あっても、コンパイルは 1 回)。
//
// プレイヤーは GPU のデバイスが作り直された時に作り直す (platform::Gfx::Generation: 持ち主の仕事)。

#include "rm_engine/particles/runtime/ModuleSource.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticleShader.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rm::particles {

struct EmitterClock {
    float age = 0;          // このラウンドに入ってからの秒数
    float owed = 0;         // まだ出していない粒の端数 (レート x 時間)
    bool burstDone = false;
    bool finished = false;  // ループしておらず、長さが終わった
};
// 次の `dt` 秒に出す粒 (多くてもエミッターの容量まで)。
uint32_t AdvanceClock(EmitterClock& c, const ParticleEmitter& e, float dt);
// スクリプトに止められているかもしれないエミッターが、次の `dt` 秒に出す粒 (emitting が false:
// 自分の分は出さない - 時計はそのまま) と、一度に頼まれた `extra` 個 (rm::Particles::Emit):
// 多くても容量まで。
uint32_t EmitterBirths(EmitterClock& c, const ParticleEmitter& e, float dt, bool emitting, uint32_t extra);

// モジュールの元を探す: 組み込みのもの、またはモジュールが名前で挙げる .rmfx (null: 無し;
// `file` はコンパイラーのメッセージ用の名前)。読み直した (その間に保存された) ファイルは新しい元で、
// それを見たプレイヤーがシェーダーを作り直す。
using ModuleLookup = std::function<std::shared_ptr<const ModuleSource>(const ParticleModule& m, std::string& file)>;
// 組み込みモジュールだけ (ファイルのモジュールは除く)。
std::shared_ptr<const ModuleSource> BuiltinLookup(const ParticleModule& m, std::string& file);

// このフレームに粒が当たる相手 (CollideShapes / CollideDepth); null: 無し。
struct CollisionWorld {
    std::span<const CollisionShape> shapes;  // ワールドにあるコライダーの形
    // 絵の直近のフレームの深度 (レンダラーの ExtraDrawContext::depth) と、それを描いたカメラ;
    // null: 当たる深度が無い。
    ID3D11ShaderResourceView* depth = nullptr;
    uint32_t samples = 1;
    float viewProj[16]{};
    float inverse[16]{};  // クリップ -> ワールド
    float eye[3]{};
    uint32_t width = 0, height = 0;
};

class EffectPlayer {
public:
    EffectPlayer();
    ~EffectPlayer();
    EffectPlayer(const EffectPlayer&) = delete;
    EffectPlayer& operator=(const EffectPlayer&) = delete;

    // 再生するエフェクト (次に Set するまで、渡されたまま持つ: 生かしておくのは持ち主)。毎フレーム
    // 聞かれる: モジュールは毎回引き直し、エフェクトかモジュールの元が変わった時だけ作り直す。
    // シェーダーが変わらないエミッターは粒を持ち続け、値が変わるだけなら定数バッファーだけが変わる。
    void Set(std::shared_ptr<const ParticleEffect> effect, const ModuleLookup& lookup);
    const std::shared_ptr<const ParticleEffect>& Effect() const { return effect_; }

    // また始めから (粒は無し、時計は 0)。
    void Restart();
    // 新しい粒を出す (スクリプトの Stop: false - 出ているものは生き続けて消えていく) と、次の Advance で
    // 各エミッターから `count` 個多く出す (スクリプトの Emit)。
    void SetEmitting(bool on);
    bool Emitting() const { return emitting_; }
    void Emit(uint32_t count) { pending_ += count; }
    // dt だけ進める (`world` が置いたエミッターの位置から新しい粒を出す - 列優先の 4x4)。
    void Advance(ID3D11Device* device, ID3D11DeviceContext* ctx, const float world[16], float dt, const CollisionWorld* collide = nullptr);
    // 粒を描く: 呼び出し側が絵のレンダーターゲットと深度バッファーを結びつけてある。`images`:
    // エミッターの 絵 が指す GPU 上の絵 (null: 無い - 代わりに点の形を描く)。
    using ImageLookup = std::function<ID3D11ShaderResourceView*(const AssetRef&)>;
    void Draw(ID3D11DeviceContext* ctx, const ViewConstants& view, const ImageLookup& images = {});

    // エミッターが動かせない理由 (シェーダーがコンパイルできなかった: コンパイラーの言葉); 空: 動く。
    std::string Error(size_t emitter) const;
    // すべてのエミッターが終わった (ループしておらず、粒が消えるだけの時間もたった)。
    bool Finished() const;
    // 動いているエミッター、それらが持てる粒の最大数、生きている数 (CountingAlive の間に GPU から
    // 最後に読み戻した値; aliveKnown が false: まだ読めていない)。
    struct Census {
        uint32_t emitters = 0;
        uint64_t capacity = 0;
        uint64_t alive = 0;
        bool aliveKnown = true;
    };
    Census Count() const;
    // 前に呼んでからうまくいかなかったこと (読めないモジュール、コンパイルできないシェーダー:
    // コンソール用の日本語) - それぞれ 1 度だけ知らせる。
    std::vector<std::string> TakeProblems();

private:
    struct Emitter;
    static bool MakeCounter(ID3D11Device* device, Emitter& e);
    static void ReadBackCount(ID3D11DeviceContext* ctx, Emitter& e);
    std::shared_ptr<const ParticleEffect> effect_;
    std::vector<std::unique_ptr<Emitter>> emitters_;
    std::vector<std::string> problems_;
    float time_ = 0;
    bool emitting_ = true;
    uint32_t pending_ = 0;
    // エミッターが当たる相手 (みんなで共有する): 形と、深度のカメラ。
    Microsoft::WRL::ComPtr<ID3D11Buffer> shapes_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shapesView_;
    size_t shapesCapacity_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> collideCb_;
};

// 生きている粒を数える (デバッグパネル用): 各プレイヤーが次の `seconds` 秒の間 GPU で数え、数フレーム
// 後に数を読み戻す。頼まれなければ何も数えない (負担なし)。
void CountAliveFor(double seconds);
bool CountingAlive();

// Direct3D がパーティクルを動かせる (フィーチャーレベル 11_0 以上)。
bool CanRunParticles(ID3D11Device* device);

}  // namespace rm::particles
