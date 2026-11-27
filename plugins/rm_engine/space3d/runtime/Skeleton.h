#pragma once
// 3D 空間 - 骨のあるモデルのデータ (docs/ANIMATION.md 3-1): 骨の木 (Skeleton)・スキン (頂点ごとの
// 骨とウェイト、骨ごとの逆バインド行列)・クリップ (骨の動きのサンプル)。
//   読む:   glTF (Mesh.cpp)・FBX (Fbx.cpp) -> MeshData::skin / MeshData::clips
//   焼く:   MeshBake (.rmmesh の SKEL / SKIN / IBND / BBOX)・クリップは .rmclip
//   描く:   Renderer3D (GPU スキニング: MeshDraw::bones)
//   姿勢:   クリップ -> 骨の行列 は animation3d プラグイン (ここは型と小さな計算だけ)
// 純粋 (描画もファイルもない)。tests/TestSkeleton.cpp でテストする。
//
// 骨の空間: 骨の行列 (モデルの空間) = 親の骨の行列 x TRSMatrix(その骨の位置・回転・大きさ)。
// スキンの行列 = 骨の行列 x 逆バインド行列 (休みの姿勢では単位行列になる)。
// 頂点の位置 = sum(ウェイト x スキンの行列 x 頂点)。

#include "base/Math.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/space3d/runtime/Morph.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rm::space3d {

// 1 つのスキンの骨の上限 (読み込み・GPU の 1 回の描画とも)。超えたら読めないとして理由を出す
// (前のエンジンは 512 を超えた骨が黙って 1 本に潰れた)。
inline constexpr uint32_t kMaxBones = 1024;
// クリップの刻み (1 秒のサンプル数) の上限と、ファイルに無いときの値。
inline constexpr float kMaxClipRate = 60.f;
inline constexpr float kDefaultClipRate = 30.f;

// 回転 (単位四元数)。x y z が軸 x sin(角度 / 2)、w が cos(角度 / 2)。glTF と同じ並び。
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    bool operator==(const Quat&) const = default;
};
Quat operator*(Quat a, Quat b);  // a を b の後に (行列の a * b と同じ)
Quat QuatNormalize(Quat q);           // 長さ 0: 回らないもの
Quat QuatConjugate(Quat q);           // 逆の回転 (単位四元数のとき)
float QuatDot(Quat a, Quat b);
Vec3 QuatRotate(Quat q, Vec3 v);
// `axis` まわりに `radians` (右手系: 先端から見て反時計回り。AxisAngle と同じ向き)。
Quat QuatAxisAngle(Vec3 axis, float radians);
Mat4 QuatMatrix(Quat q);  // 回転だけの行列 (長さは 1 に直してから)
// 回転の行列 (各列の長さが 1、行列式 +1) の四元数。w >= 0 のものを返す。
Quat QuatFromMatrix(const Mat4& rotation);

// 骨の位置・回転・大きさ (親の空間で)。
struct BoneTRS {
    Vec3 t{0, 0, 0};
    Quat r{};
    Vec3 s{1, 1, 1};
};
Mat4 TRSMatrix(const BoneTRS& p);  // T * R * S
// 行列を分ける (TRSMatrix の逆)。鏡写し (行列式 < 0) のときは X の大きさを負にする。
// 軸ごとに違う大きさが回っている (シアー) ときは近いもの。
BoneTRS DecomposeTRS(const Mat4& m);

// GPU に送る骨の行列: Mat4 の上 3 行 (3x4。シェーダーは float4 を 3 つ読んで dot(行, float4(p, 1)))。
struct BoneMatrix {
    float row[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
};
BoneMatrix ToBoneMatrix(const Mat4& m);

struct Bone {
    std::string name;
    int parent = -1;  // この骨より前の番号 (-1: 根)
    BoneTRS rest;     // 休みの姿勢 (ファイルのノードの値。クリップに無い骨はこのまま)
};

// 骨の木。**親が先に並ぶ** (1 回のループで親から順に掛けられる)。骨はエンティティにしない。
struct Skeleton {
    std::vector<Bone> bones;
    // 名前で引く (同じ名前が 2 つあるときは最初のもの)。無ければ -1。
    int Find(std::string_view name) const;
};
// 親が先に並んでいて、親の番号がどれも正しいか (読んだ後とテストで)。
bool ParentsFirst(const Skeleton& s);

// 頂点ごとの骨 4 本とウェイト (unorm16: 65535 = 1。合計は 65535)。使わない枠はウェイト 0。
struct SkinVertex {
    std::array<uint16_t, 4> bone{};
    std::array<uint16_t, 4> weight{};
    bool operator==(const SkinVertex&) const = default;
};

// 頂点ごとの (骨, ウェイト) を 4 本にする: 重い 4 本を残し、合計 1 にし直して unorm16 に丸める
// (丸めの差は一番重いものに足す)。`dropped` に、5 本目より後で捨てたものがあれば true。
// ウェイトが全部 0 (や負) のときは骨 `fallback` にウェイト 1。
SkinVertex PackInfluences(std::vector<std::pair<uint32_t, float>> influences, uint32_t fallback, bool* dropped = nullptr);

// 揺れる骨の設定 (docs/ANIMATION.md 7-2-3)。モデルのファイル (VRM の VRMC_springBone / secondaryAnimation) から読む。
// 部品「揺れる骨」の「モデルの揺れの設定を使う」で使われる。値は VRM と同じ意味 (Spring.h の式)。
struct SpringJointSetting {
    std::string bone;
    float stiffness = 1.f, drag = 0.4f, gravity = 0.f, radius = 0.02f;
    Vec3 gravityDir{0, -1, 0};
};
struct SpringChainSetting {
    std::string name;
    std::vector<SpringJointSetting> joints;  // below: 1 つ目の骨の下全部をその設定で。でなければ並びのとおり (2 つ目から動く)
    bool below = false;
};
struct SpringColliderSetting {
    std::string bone;
    Vec3 offset{0, 0, 0};  // 骨の空間
    float radius = 0.1f;
};

struct SkinData {
    Skeleton skeleton;
    std::vector<SkinVertex> vertices;  // MeshData::vertices と同じ数・同じ順
    std::vector<Mat4> inverseBind;     // 骨ごと: モデルの空間 -> バインドしたときの骨の空間
    // 骨ごと: その骨が動かす頂点 (ウェイト > 0) を、バインドしたときの骨の空間で囲む箱
    // (見えるかの判定: 今の骨の行列で動かした箱の和)。動かす頂点が無い骨は min > max。
    std::vector<Vec3> boundsMin, boundsMax;
    // ブレンドシェイプの名前と初めの重み (MeshData::morphs の並び)。姿勢を作る側 (アニメーション) がクリップの曲線を
    // 名前で引くのに使う。読み込み・焼いたものは空のまま: GPU に上げた後で game_x_3d::TakeSkinned が入れる。
    std::vector<std::string> morphs;
    std::vector<float> morphDefaults;
    // 揺れる骨の設定 (VRM から読んだもの。無ければ空)。焼くと .rmmesh の SPRG。
    std::vector<SpringChainSetting> springs;
    std::vector<SpringColliderSetting> springColliders;
};
// 頂点と inverseBind から boundsMin / boundsMax を作る。
void ComputeBoneBounds(SkinData& skin, const std::vector<Vec3>& positions);

// 骨 1 本の動き。それぞれの列の長さ: 0 = 動かない (休みの姿勢のまま)、1 = ずっと同じ値、
// Clip::frames = 刻みごとのサンプル。骨は**名前**で指す (同じ骨の名前の別のモデルでも使える。
// 名前から番号へはクリップを使う側が 1 回だけ引く)。
struct BoneTrack {
    std::string bone;
    std::vector<Vec3> t;
    std::vector<Quat> r;
    std::vector<Vec3> s;
};

// クリップのイベント (docs/ANIMATION.md 3-6): 再生の位置がその時刻を通ったら、そのエンティティのスクリプトの
// OnEvent に名前・数・文字が届く。置くのはモデルの .meta の取り込みの設定 (ClipSettings.h。編集は 3D アニメーションの
// インスペクターの「クリップとイベント」)。保存・読み込み・インスペクターの欄はこの定義から (憲法 9)。
RM_STRUCT(Label="イベント")
struct ClipEvent {
    RM_PROPERTY(Edit, Label="時刻 (秒)", Tooltip="クリップの始めからの秒。再生がここを通るとイベントが出ます", Min=0, Step=0.01)
    float time = 0;
    RM_PROPERTY(Edit, Label="名前", Tooltip="スクリプトの OnEvent に届く名前 (足音なら Footstep など)")
    std::string name = "Event";
    RM_PROPERTY(Edit, Label="数値", Tooltip="名前と一緒に届く数") float number = 0;
    RM_PROPERTY(Edit, Label="文字", Tooltip="名前と一緒に届く文字") std::string text;
};
inline bool operator==(const ClipEvent& a, const ClipEvent& b) {
    return a.time == b.time && a.name == b.name && a.number == b.number && a.text == b.text;
}

// クリップのルートモーション (docs/ANIMATION.md 3-5・7-1): 根の骨の動いた分を取り出して姿勢からは消し、
// エンティティを動かす (足が滑らない・攻撃で踏み込む)。置くのはイベントと同じモデルの .meta の "clips"
// (ClipSettings.h)。保存・読み込み・欄はこの定義から (憲法 9)。計算は 3D アニメーションの RootMotion.h。
RM_STRUCT(Label="ルートモーション")
struct ClipRootMotion {
    RM_PROPERTY(Edit, Label="使う", Tooltip="根の骨の動きを姿勢から取り出して、エンティティを動かします (アニメーターの「ルートモーション」の欄で、動かす / その場で / スクリプトに渡す を選べます)")
    bool enabled = false;
    RM_PROPERTY(Edit, Label="根の骨", Tooltip="動きを取り出す骨 (親の無い骨だけ)。空: 自動 (親の無い骨のうち「(骨の外)」でない最初のもの)")
    std::string bone;
    RM_PROPERTY(Edit, Label="横の移動 (XZ)", Tooltip="前後・左右の動きを取り出します") bool horizontal = true;
    RM_PROPERTY(Edit, Label="縦の移動 (Y)", Tooltip="上下の動きを取り出します (物理のキャラクターがあるときは使いません: 重力とジャンプは物理)") bool vertical = false;
    RM_PROPERTY(Edit, Label="Y のまわりの回転", Tooltip="向きの変わり (曲がる・振り向く) を取り出して、エンティティを回します") bool turn = true;
};
inline bool operator==(const ClipRootMotion& a, const ClipRootMotion& b) {
    return a.enabled == b.enabled && a.bone == b.bone && a.horizontal == b.horizontal && a.vertical == b.vertical && a.turn == b.turn;
}

// クリップの圧縮 (docs/ANIMATION.md 7-3)。誤差は骨の先の位置で: ふつう 1 mm・強く 4 mm (ClipPack.h)。
RM_ENUM(Label="圧縮")
enum class ClipCompression {
    RM_ITEM(Label="しない") None,
    RM_ITEM(Label="ふつう") Normal,
    RM_ITEM(Label="強く") Strong,
};

// クリップの書き出しの設定 (モデルの .meta の "clips" の "bake"。ClipSettings.h)。保存・読み込み・欄はこの定義から (憲法 9)。
RM_STRUCT(Label="書き出し")
struct ClipBake {
    RM_PROPERTY(Edit, Label="圧縮", Tooltip="クリップを小さくします。ふつう: 骨の先が 1 mm より動かないように、強く: 4 mm まで。しない: そのまま (エディターの再生もこの設定で)")
    ClipCompression compression = ClipCompression::Normal;
    RM_PROPERTY(Edit, Label="いつも入れる", Tooltip="書き出したゲームに、どのアニメーターも使っていなくても入れます (スクリプトがクリップの名前で流すときに)")
    bool always = false;
};
inline bool operator==(const ClipBake& a, const ClipBake& b) { return a.compression == b.compression && a.always == b.always; }

// 圧縮した列 1 つ (位置・回転・大きさのどれか。ClipPack.h)。サンプルの全部ではなく「鍵」の刻みだけを持ち、間は直線で戻す。
struct PackedChannel {
    enum class Kind : uint8_t {
        Empty,      // 動かない (休みの姿勢のまま)
        Constant,   // ずっと同じ値 (constant)
        Keys16,     // 鍵ごとに 16 ビット x 3 (位置・大きさ: lo + q x step、回転: 大きい成分を除いた 3 つ = 48 ビット)
        KeysFloat,  // 鍵ごとに float x 3 / 4 (16 ビットでは誤差に収まらない大きな動き)
    };
    Kind kind = Kind::Empty;
    std::array<float, 4> constant{0, 0, 0, 1};
    std::vector<uint16_t> keys;  // 鍵の刻み (0 から frames - 1 まで、増える順。最初は 0、最後は frames - 1)
    std::vector<uint16_t> q;     // Keys16: 鍵ごとに 3 つ
    std::vector<float> f;        // KeysFloat: 鍵ごとに 3 つ (位置・大きさ) か 4 つ (回転)
    Vec3 lo{0, 0, 0}, step{0, 0, 0};  // Keys16 の位置・大きさ
};
struct PackedTrack {
    PackedChannel t, r, s;
};

struct Clip {
    std::string name;
    float duration = 0;              // 秒
    float rate = kDefaultClipRate;   // 1 秒のサンプル数
    uint32_t frames = 1;             // サンプルの数 = round(duration x rate) + 1
    // 骨の動き。圧縮したクリップ (packed が空でない) は、tracks には骨の名前だけが残り (t / r / s は空)、
    // packed[i] が tracks[i] の動き。
    std::vector<BoneTrack> tracks;
    std::vector<PackedTrack> packed;
    ClipBake bake;                   // 書き出しの設定 (.meta から。焼いたモデルには入らない: 焼くときにはもう圧縮してある)
    std::vector<ClipEvent> events;   // 時刻の順
    ClipRootMotion root;             // ルートモーション (既定: 使わない)
    std::vector<MorphTrack> morphs;  // ブレンドシェイプの重みの曲線 (Morph.h。形は名前で指す)
};
// duration と rate からサンプルの数。
uint32_t ClipFrames(float duration, float rate);

}  // namespace rm::space3d
