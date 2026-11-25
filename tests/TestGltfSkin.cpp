// glTF の骨・スキン・アニメーションの読み込み (plugins/rm_engine/space3d/runtime/Mesh.cpp) と、取り込みの
// 設定を骨とクリップに掛けること (ApplyModelImport) のテスト。見本のモデル docs/samples/models/skinned_arm.gltf
// (make_skinned_sample.py が作る) と、テストの中で書く小さな glTF で確かめる。スキンで動かした頂点は
// ここで CPU で計算する (骨の行列 = 親の骨の行列 x TRS、頂点 = sum(ウェイト x 骨の行列 x 逆バインド x 頂点))。

#include "Test.h"

#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>

using namespace rm;
using namespace rm::space3d;

namespace {

bool NearV(Vec3 a, Vec3 b, float eps = 1e-4f) { return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.z - b.z) < eps; }
bool SameRotation(Quat a, Quat b) { return std::abs(std::abs(QuatDot(a, b)) - 1.f) < 1e-4f; }
constexpr float kPi = std::numbers::pi_v<float>;

std::string Base64(const std::vector<uint8_t>& b) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < b.size(); i += 3) {
        const uint32_t n = (uint32_t{b[i]} << 16) | (i + 1 < b.size() ? uint32_t{b[i + 1]} << 8 : 0u) | (i + 2 < b.size() ? uint32_t{b[i + 2]} : 0u);
        out += k[(n >> 18) & 63];
        out += k[(n >> 12) & 63];
        out += i + 1 < b.size() ? k[(n >> 6) & 63] : '=';
        out += i + 2 < b.size() ? k[n & 63] : '=';
    }
    return out;
}

std::span<const uint8_t> Bytes(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

// 小さな glTF を書く: 値を足すとアクセサーの番号が返り、Doc がバッファー (data: URI) とビューとアクセサーを付ける。
class Builder {
public:
    template <class T>
    int Acc(const std::vector<T>& values, int componentType, const char* type, size_t count, bool normalized = false) {
        while (bin_.size() % 4) bin_.push_back(0);
        const size_t offset = bin_.size();
        for (const T v : values) {
            uint8_t p[sizeof(T)];
            std::memcpy(p, &v, sizeof(T));
            bin_.insert(bin_.end(), p, p + sizeof(T));
        }
        if (n_ > 0) views_ += ",", accessors_ += ",";
        views_ += R"({"buffer":0,"byteOffset":)" + std::to_string(offset) + R"(,"byteLength":)" + std::to_string(values.size() * sizeof(T)) + "}";
        accessors_ += R"({"bufferView":)" + std::to_string(n_) + R"(,"componentType":)" + std::to_string(componentType) + R"(,"count":)" +
                      std::to_string(count) + R"(,"type":")" + type + "\"" + (normalized ? R"(,"normalized":true)" : "") + "}";
        return n_++;
    }
    int Floats(const std::vector<float>& v, const char* type, size_t count) { return Acc<float>(v, 5126, type, count); }
    // `rest`: "nodes"・"meshes"・"skins" などのキー (前後の {} は無し)。
    std::string Doc(const std::string& rest) const {
        return R"({"asset":{"version":"2.0"},)" + rest + R"(,"buffers":[{"byteLength":)" + std::to_string(bin_.size()) +
               R"(,"uri":"data:application/octet-stream;base64,)" + Base64(bin_) + R"("}],"bufferViews":[)" + views_ + R"(],"accessors":[)" +
               accessors_ + "]}";
    }

private:
    std::vector<uint8_t> bin_;
    std::string views_, accessors_;
    int n_ = 0;
};

std::string S(int v) { return std::to_string(v); }

// 骨の位置 (0, y, 0) の逆バインド行列 (列の順)。
std::vector<float> UnTranslate(float x, float y, float z) { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1}; }

// 骨ごとの姿勢: 休みの姿勢に、クリップの `frame` 番目のサンプルを入れる (clip が null: 休みの姿勢)。
std::vector<BoneTRS> PoseOf(const SkinData& skin, const Clip* clip, uint32_t frame) {
    std::vector<BoneTRS> pose;
    for (const Bone& b : skin.skeleton.bones) pose.push_back(b.rest);
    if (!clip) return pose;
    for (const BoneTrack& t : clip->tracks) {
        const int b = skin.skeleton.Find(t.bone);
        if (b < 0) continue;
        BoneTRS& p = pose[static_cast<size_t>(b)];
        if (!t.t.empty()) p.t = t.t[std::min<size_t>(frame, t.t.size() - 1)];
        if (!t.r.empty()) p.r = t.r[std::min<size_t>(frame, t.r.size() - 1)];
        if (!t.s.empty()) p.s = t.s[std::min<size_t>(frame, t.s.size() - 1)];
    }
    return pose;
}

// CPU のスキニング: 頂点ごとに動かした位置。
std::vector<Vec3> Skinned(const MeshData& m, const std::vector<BoneTRS>& pose) {
    const SkinData& skin = *m.skin;
    std::vector<Mat4> model(pose.size());
    for (size_t i = 0; i < pose.size(); ++i) {
        const int p = skin.skeleton.bones[i].parent;
        model[i] = p >= 0 ? model[static_cast<size_t>(p)] * TRSMatrix(pose[i]) : TRSMatrix(pose[i]);
    }
    std::vector<Vec3> out;
    for (size_t v = 0; v < m.vertices.size(); ++v) {
        Vec3 sum{0, 0, 0};
        for (size_t k = 0; k < 4; ++k) {
            const float w = skin.vertices[v].weight[k] / 65535.f;
            if (w == 0) continue;
            const size_t b = skin.vertices[v].bone[k];
            sum = sum + TransformPoint(model[b] * skin.inverseBind[b], m.vertices[v].position) * w;
        }
        out.push_back(sum);
    }
    return out;
}

std::optional<MeshData> ReadSample(std::string* error) {
    const auto file = std::filesystem::path(__FILE__).parent_path().parent_path() / "docs" / "samples" / "models" / "skinned_arm.gltf";
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ParseGltf(Bytes(text), nullptr, error);
}

bool HasWarning(const MeshData& m, std::string_view part) {
    for (const std::string& w : m.warnings) {
        if (w.find(part) != std::string::npos) return true;
    }
    return false;
}

// 三角形 1 つ (スキンあり)。`joints` / `weights` は頂点 3 つ分 (4 つずつ)。
struct SkinnedTri {
    int pos, joints, weights;
};
SkinnedTri Tri(Builder& b, std::vector<uint16_t> joints, std::vector<float> weights) {
    SkinnedTri t;
    t.pos = b.Floats({0, 0, 0, 1, 0, 0, 0, 1, 0}, "VEC3", 3);
    t.joints = b.Acc<uint16_t>(joints, 5123, "VEC4", 3);
    t.weights = b.Floats(weights, "VEC4", 3);
    return t;
}
std::string SkinnedMesh(const SkinnedTri& t) {
    return R"("meshes":[{"primitives":[{"attributes":{"POSITION":)" + S(t.pos) + R"(,"JOINTS_0":)" + S(t.joints) + R"(,"WEIGHTS_0":)" + S(t.weights) + "}}]}]";
}

}  // namespace

// --- 見本のモデル ------------------------------------------------------------------------------

RM_TEST(GltfSkin_SampleArm) {
    std::string error;
    const auto m = ReadSample(&error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->warnings.empty());
    CHECK(m->skin.has_value());
    if (!m->skin) return;
    const SkinData& skin = *m->skin;
    // 骨 3 本の鎖。休みの姿勢はノードの値 (骨でない「Arm」は単位行列なので何も足さない)。
    CHECK(skin.skeleton.bones.size() == 3 && ParentsFirst(skin.skeleton));
    CHECK(skin.skeleton.bones[0].name == "Base" && skin.skeleton.bones[1].name == "Middle" && skin.skeleton.bones[2].name == "Top");
    CHECK(skin.skeleton.bones[0].parent == -1 && skin.skeleton.bones[1].parent == 0 && skin.skeleton.bones[2].parent == 1);
    CHECK(NearV(skin.skeleton.bones[1].rest.t, {0, 2.f / 3, 0}));
    CHECK(skin.inverseBind.size() == 3 && skin.boundsMin.size() == 3);
    // ウェイトは頂点ごとに合計 1 (65535)。骨は 2 本まで。
    CHECK(skin.vertices.size() == m->vertices.size());
    bool sums = true, two = true;
    for (const SkinVertex& v : skin.vertices) {
        sums = sums && v.weight[0] + v.weight[1] + v.weight[2] + v.weight[3] == 65535;
        two = two && v.weight[2] == 0 && v.weight[3] == 0;
    }
    CHECK(sums && two);
    // 3 つの部分 (2 つの材質) - スキンのあるメッシュは、パーツが何個あっても同じ骨で動く。
    CHECK(m->materials.size() == 2 && m->parts.size() == 2);
    // 休みの姿勢 = バインドの姿勢: 動かしても形のまま。
    const auto rest = Skinned(*m, PoseOf(skin, nullptr, 0));
    bool same = true;
    for (size_t i = 0; i < rest.size(); ++i) same = same && NearV(rest[i], m->vertices[i].position);
    CHECK(same);
    // クリップ: 名前・長さ・サンプルの数。回るだけ (位置・大きさのトラックは無い)。ループする (最初と最後が同じ)。
    CHECK(m->clips.size() == 2);
    if (m->clips.size() != 2) return;
    const Clip& wave = m->clips[0];
    const Clip& idle = m->clips[1];
    CHECK(wave.name == "Wave" && std::abs(wave.duration - 2.f) < 1e-5f && wave.rate == kDefaultClipRate && wave.frames == 61);
    CHECK(idle.name == "Idle" && std::abs(idle.duration - 4.f) < 1e-5f && idle.frames == 121);
    CHECK(wave.tracks.size() == 3 && wave.tracks[0].bone == "Base");
    for (const BoneTrack& t : wave.tracks) {
        CHECK(t.t.empty() && t.s.empty() && t.r.size() == wave.frames);
        CHECK(SameRotation(t.r.front(), t.r.back()));
    }
    // 0.5 秒 (15 番目): Base は Z のまわりに 0.25 x sin(π/2) (キーの上なので補間の誤差は無い)。
    CHECK(SameRotation(wave.tracks[0].r[15], QuatAxisAngle({0, 0, 1}, 0.25f)));
    // 曲げると、上の端は横に動く。
    const auto bent = Skinned(*m, PoseOf(skin, &wave, 15));
    float maxX = 0;
    for (const Vec3& p : bent) maxX = std::max(maxX, std::abs(p.x));
    CHECK(maxX > 0.5f);
}

// 取り込みの設定 (鏡に映す座標系・前後の反転・大きさ) を骨とクリップにも掛ける: スキンで動かした頂点が、
// 元のモデルで動かした頂点に同じ設定を掛けたものになる (休みの姿勢でも、クリップの途中でも)。
RM_TEST(GltfSkin_ImportSettingsMoveBonesLikeVertices) {
    std::string error;
    const auto raw = ReadSample(&error);
    CHECK(raw && raw->skin && raw->clips.size() == 2);
    if (!raw || !raw->skin || raw->clips.size() != 2) return;
    struct Setting {
        ModelBasis basis;
        bool turn;
        float scale;
    };
    const Setting settings[] = {
        {{{0, 1, 0}, {0, 0, -1}, false}, false, 1.f},  // Y が上・右手系 (glTF): 鏡に映す
        {{{0, 0, 1}, {0, 1, 0}, false}, true, 2.f},    // Z が上・右手系・前後の反転・2 倍
        {{{0, 1, 0}, {0, 0, 1}, true}, true, 0.5f},    // Y が上・左手系 (鏡なし)・反転・半分
    };
    for (const Setting& s : settings) {
        MeshData m = *raw;
        ApplyModelImport(m, s.basis, s.turn, s.scale);
        // 比べる相手: 元のモデルで動かした頂点を、頂点だけのメッシュとして同じ設定で写したもの。
        auto expected = [&](const std::vector<Vec3>& points) {
            MeshData p;
            for (const Vec3& v : points) p.vertices.push_back({v, {0, 1, 0}, {0, 0}});
            ApplyModelImport(p, s.basis, s.turn, s.scale);
            std::vector<Vec3> out;
            for (const MeshVertex& v : p.vertices) out.push_back(v.position);
            return out;
        };
        const auto restWant = expected(Skinned(*raw, PoseOf(*raw->skin, nullptr, 0)));
        const auto rest = Skinned(m, PoseOf(*m.skin, nullptr, 0));
        const auto waveWant = expected(Skinned(*raw, PoseOf(*raw->skin, &raw->clips[0], 20)));
        const auto wave = Skinned(m, PoseOf(*m.skin, &m.clips[0], 20));
        bool restOk = rest.size() == restWant.size(), waveOk = wave.size() == waveWant.size();
        for (size_t i = 0; restOk && i < rest.size(); ++i) restOk = NearV(rest[i], restWant[i], 1e-3f) && NearV(rest[i], m.vertices[i].position, 1e-3f);
        for (size_t i = 0; waveOk && i < wave.size(); ++i) waveOk = NearV(wave[i], waveWant[i], 1e-3f);
        CHECK(restOk);
        CHECK(waveOk);
        // 回転は回転のまま (長さ 1)。
        bool unit = true;
        for (const BoneTrack& t : m.clips[0].tracks) {
            for (const Quat& q : t.r) unit = unit && std::abs(QuatDot(q, q) - 1.f) < 1e-4f;
        }
        CHECK(unit);
    }
}

// --- テストの中で書く glTF ---------------------------------------------------------------------

// 骨 2 本 (Root -> Child)。サンプラーの読み方: STEP・CUBICSPLINE・LINEAR の回転 (近い方の回り方)、
// ずっと同じ値は 1 つ、動かない骨にトラックは無い。スキンのあるメッシュのノードの行列は掛けない。
RM_TEST(GltfSkin_SamplersAreResampled) {
    Builder b;
    const SkinnedTri tri = Tri(b, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    const int times = b.Floats({0, 1}, "SCALAR", 2);
    const int step = b.Floats({0, 0, 0, 1, 0, 0}, "VEC3", 2);
    // CUBICSPLINE: キーごとに 入りの接線・値・出の接線。0 秒で値 0、出の接線 2 (1 秒あたり)。1 秒で値 1、接線 0。
    const int cubic = b.Floats({0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}, "VEC3", 6);
    // 回転: 0 度 -> Y のまわりに 90 度。2 つ目を -q で書く (同じ回転。遠回りしてはいけない)。
    const Quat q90 = QuatAxisAngle({0, 1, 0}, kPi / 2);
    const int rot = b.Floats({0, 0, 0, 1, -q90.x, -q90.y, -q90.z, -q90.w}, "VEC4", 2);
    const int scale = b.Floats({2, 2, 2, 2, 2, 2}, "VEC3", 2);
    const std::string doc = b.Doc(
        R"("scene":0,"scenes":[{"nodes":[0,1]}],"nodes":[{"name":"Body","mesh":0,"skin":0,"translation":[0,0,5]},)"
        R"({"name":"Root","children":[2]},{"name":"Child","translation":[0,1,0]}],)"
        R"("skins":[{"joints":[1,2]}],)" +
        SkinnedMesh(tri) + R"(,"animations":[)"
        R"({"name":"Step","samplers":[{"input":)" + S(times) + R"(,"output":)" + S(step) + R"(,"interpolation":"STEP"}],)"
        R"("channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]},)"
        R"({"name":"Cubic","samplers":[{"input":)" + S(times) + R"(,"output":)" + S(cubic) + R"(,"interpolation":"CUBICSPLINE"}],)"
        R"("channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]},)"
        R"({"samplers":[{"input":)" + S(times) + R"(,"output":)" + S(rot) + R"(},{"input":)" + S(times) + R"(,"output":)" + S(scale) + R"(}],)"
        R"("channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}},{"sampler":1,"target":{"node":1,"path":"scale"}},)"
        R"({"sampler":0,"target":{"node":0,"path":"rotation"}},{"sampler":1,"target":{"node":1,"path":"weights"}}]}])");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->skin && m->skin->skeleton.bones.size() == 2);
    // スキンのあるメッシュ: ノードの (0, 0, 5) は掛けない。
    CHECK(NearV(m->vertices[1].position, {1, 0, 0}));
    CHECK(m->clips.size() == 3);
    if (m->clips.size() != 3) return;
    const Clip& st = m->clips[0];
    CHECK(st.name == "Step" && st.frames == 31 && st.tracks.size() == 1 && st.tracks[0].bone == "Child");
    CHECK(st.tracks[0].t.size() == 31 && st.tracks[0].r.empty() && st.tracks[0].s.empty());
    CHECK(NearV(st.tracks[0].t[29], {0, 0, 0}) && NearV(st.tracks[0].t[30], {1, 0, 0}));  // 1 秒になるまで前の値
    // エルミート: s = 0.5 で h01 = 0.5、h10 x (キーの間 1 秒) x 接線 2 = 0.125 x 2 = 0.25。
    const Clip& cu = m->clips[1];
    CHECK(cu.name == "Cubic" && NearV(cu.tracks[0].t[15], {0.75f, 0, 0}) && NearV(cu.tracks[0].t[30], {1, 0, 0}));
    // 名前の無いアニメーション。回転は近い方へ 45 度 (15 番目)。大きさはずっと 2 なので 1 つ。
    const Clip& ro = m->clips[2];
    CHECK(ro.name == "アニメーション 2" && ro.tracks.size() == 1 && ro.tracks[0].bone == "Root");
    CHECK(ro.tracks[0].r.size() == 31 && SameRotation(ro.tracks[0].r[15], QuatAxisAngle({0, 1, 0}, kPi / 4)));
    CHECK(ro.tracks[0].s.size() == 1 && NearV(ro.tracks[0].s[0], {2, 2, 2}) && ro.tracks[0].t.empty());
    // 続くサンプルの四元数は同じ側 (補間で遠回りしない)。
    bool sameSide = true;
    for (size_t i = 1; i < ro.tracks[0].r.size(); ++i) sameSide = sameSide && QuatDot(ro.tracks[0].r[i - 1], ro.tracks[0].r[i]) > 0;
    CHECK(sameSide);
    CHECK(HasWarning(*m, "モーフ"));
}

// JOINTS_0 が unsigned byte、WEIGHTS_0 が正規化した unsigned byte / short。スキンの joints の番号から骨の番号へ
// (親が先に並ぶので、joints の順とは違う)。
RM_TEST(GltfSkin_ByteJointsAndUnormWeights) {
    for (const int weightType : {5121, 5123}) {
        Builder b;
        const int pos = b.Floats({0, 0, 0, 1, 0, 0, 0, 1, 0}, "VEC3", 3);
        const int joints = b.Acc<uint8_t>({0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 5121, "VEC4", 3);
        int weights = 0;
        if (weightType == 5121) {
            weights = b.Acc<uint8_t>({128, 127, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0}, 5121, "VEC4", 3, true);
        } else {
            weights = b.Acc<uint16_t>({32768, 32767, 0, 0, 65535, 0, 0, 0, 0, 0, 0, 0}, 5123, "VEC4", 3, true);
        }
        // joints は [子, 親] の順。
        const std::string doc = b.Doc(
            R"("scene":0,"scenes":[{"nodes":[0,1]}],"nodes":[{"mesh":0,"skin":0},{"name":"親","children":[2]},{"name":"子"}],)"
            R"("skins":[{"joints":[2,1]}],"meshes":[{"primitives":[{"attributes":{"POSITION":)" + S(pos) + R"(,"JOINTS_0":)" + S(joints) +
            R"(,"WEIGHTS_0":)" + S(weights) + "}}]}]");
        std::string error;
        const auto m = ParseGltf(Bytes(doc), nullptr, &error);
        CHECK(m && m->skin);
        if (!m || !m->skin) continue;
        const SkinData& s = *m->skin;
        CHECK(s.skeleton.bones.size() == 2 && s.skeleton.bones[0].name == "親" && s.skeleton.bones[1].name == "子" && s.skeleton.bones[1].parent == 0);
        // 頂点 0: joints 0 (= 子 = 骨 1) が半分少し、joints 1 (= 親 = 骨 0) が半分少し。
        CHECK(s.vertices[0].bone[0] == 1 && s.vertices[0].bone[1] == 0 && s.vertices[0].weight[0] + s.vertices[0].weight[1] == 65535);
        CHECK(std::abs(static_cast<int>(s.vertices[0].weight[0]) - 32768) < 200);
        // 頂点 1: 骨 0 だけ。頂点 2: ウェイトが全部 0 -> スキンの最初の骨 (子 = 骨 1) に 1。
        CHECK(s.vertices[1].bone[0] == 0 && s.vertices[1].weight[0] == 65535);
        CHECK(s.vertices[2].bone[0] == 1 && s.vertices[2].weight[0] == 65535);
    }
}

// 1 つの頂点に骨が 5 本以上 (JOINTS_1 / WEIGHTS_1): 重い 4 本にして、警告に数を出す。
RM_TEST(GltfSkin_MoreThanFourInfluences) {
    Builder b;
    const int pos = b.Floats({0, 0, 0, 1, 0, 0, 0, 1, 0}, "VEC3", 3);
    const int j0 = b.Acc<uint16_t>({0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0}, 5123, "VEC4", 3);
    const int w0 = b.Floats({0.1f, 0.3f, 0.05f, 0.2f, 1, 0, 0, 0, 1, 0, 0, 0}, "VEC4", 3);
    const int j1 = b.Acc<uint16_t>({4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 5123, "VEC4", 3);
    const int w1 = b.Floats({0.25f, 0.1f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "VEC4", 3);
    std::string nodes = R"({"mesh":0,"skin":0})";
    for (int i = 0; i < 6; ++i) nodes += R"(,{"name":"b)" + S(i) + "\"}";
    const std::string doc = b.Doc(R"("nodes":[)" + nodes + R"(],"skins":[{"joints":[1,2,3,4,5,6]}],)"
                                  R"("meshes":[{"primitives":[{"attributes":{"POSITION":)" + S(pos) + R"(,"JOINTS_0":)" + S(j0) + R"(,"WEIGHTS_0":)" + S(w0) +
                                  R"(,"JOINTS_1":)" + S(j1) + R"(,"WEIGHTS_1":)" + S(w1) + "}}]}]");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m && m->skin);
    if (!m || !m->skin) return;
    // 重い順: b1 0.3、b4 0.25、b3 0.2、b0 0.1 (b5 0.1 と b2 0.05 は捨てる)。
    const SkinVertex& v = m->skin->vertices[0];
    CHECK(v.bone[0] == 1 && v.bone[1] == 4 && v.bone[2] == 3 && v.bone[3] == 0);
    CHECK(v.weight[0] + v.weight[1] + v.weight[2] + v.weight[3] == 65535);
    CHECK(HasWarning(*m, "頂点 1 個に 5 本以上"));
}

// 骨の下の普通のメッシュ (手に持った剣) は、その骨にウェイト 1 で付く。バインドの姿勢がファイルの姿勢と
// 違っても、休みの姿勢ではノードの置き場所に来る。どの骨の下にも無いメッシュは、動かない骨に付く。
RM_TEST(GltfSkin_MeshUnderABoneFollowsIt) {
    Builder b;
    const SkinnedTri tri = Tri(b, {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    // 手の骨は、ファイルでは (0, 1, 0) にあるが、(0, 2, 0) でバインドした。
    std::vector<float> ibm = UnTranslate(0, 0, 0);
    const std::vector<float> hand = UnTranslate(0, 2, 0);
    ibm.insert(ibm.end(), hand.begin(), hand.end());
    const int ibmAcc = b.Floats(ibm, "MAT4", 2);
    const int plain = b.Floats({0, 0, 0, 0, 0, 1, 1, 0, 0}, "VEC3", 3);
    const std::string doc = b.Doc(
        R"("scene":0,"scenes":[{"nodes":[0,1,4]}],"nodes":[{"mesh":0,"skin":0},)"
        R"({"name":"Arm","children":[2]},{"name":"Hand","translation":[0,1,0],"children":[3]},)"
        R"({"name":"Sword","mesh":1,"translation":[1,0,0]},{"name":"Floor","mesh":1,"translation":[0,-1,0]}],)"
        R"("skins":[{"joints":[1,2],"inverseBindMatrices":)" + S(ibmAcc) + "}]," +
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":)" + S(tri.pos) + R"(,"JOINTS_0":)" + S(tri.joints) + R"(,"WEIGHTS_0":)" + S(tri.weights) +
        R"(}}]},{"primitives":[{"attributes":{"POSITION":)" + S(plain) + "}}]}]");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m && m->skin);
    if (!m || !m->skin) return;
    const SkinData& s = *m->skin;
    // Arm・Hand と、床のための動かない骨。
    CHECK(s.skeleton.bones.size() == 3 && s.skeleton.bones[2].name == kStaticBoneName && s.skeleton.bones[2].parent == -1);
    CHECK(m->vertices.size() == 9 && s.vertices.size() == 9);
    if (m->vertices.size() != 9 || s.vertices.size() != 9) return;
    // 剣 (頂点 3..5): 骨 1 (Hand) にウェイト 1。休みの姿勢で動かすと、ノードのワールドの (1, 1, 0) + 頂点。
    CHECK(s.vertices[3].bone[0] == 1 && s.vertices[3].weight[0] == 65535);
    const auto rest = Skinned(*m, PoseOf(s, nullptr, 0));
    CHECK(NearV(rest[3], {1, 1, 0}) && NearV(rest[4], {1, 1, 1}) && NearV(rest[5], {2, 1, 0}));
    // 形 (バインドの姿勢) では、手がバインドした場所 (1 上) にある。
    CHECK(NearV(m->vertices[3].position, {1, 2, 0}));
    // 手を Z のまわりに 90 度回すと、剣は手について回る: (1, 0, 0) ずれた点が (0, 1, 0) ずれる。
    auto pose = PoseOf(s, nullptr, 0);
    pose[1].r = QuatAxisAngle({0, 0, 1}, kPi / 2);
    const auto turned = Skinned(*m, pose);
    CHECK(NearV(turned[3], {0, 2, 0}));
    // 床 (頂点 6..8): 動かない骨 (休みの姿勢・逆バインドとも単位行列) で、ノードの置き場所のまま。
    CHECK(s.vertices[6].bone[0] == 2 && NearV(m->vertices[6].position, {0, -1, 0}) && NearV(turned[8], {1, -1, 0}));
}

// 骨でないノード (アーマチュア) が骨の上にあると、その行列を根の骨に掛けておく (骨の行列 = ノードのワールド行列)。
// クリップも同じに掛ける。
RM_TEST(GltfSkin_NodeAboveTheBonesIsFolded) {
    Builder b;
    const SkinnedTri tri = Tri(b, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    const int times = b.Floats({0, 1}, "SCALAR", 2);
    const int moves = b.Floats({0, 0, 0, 0, 1, 0}, "VEC3", 2);
    const std::string doc = b.Doc(
        R"("nodes":[{"mesh":0,"skin":0},{"name":"Armature","scale":[2,2,2],"translation":[5,0,0],"children":[2]},{"name":"Root"}],)"
        R"("skins":[{"joints":[2]}],)" +
        SkinnedMesh(tri) + R"(,"animations":[{"samplers":[{"input":)" + S(times) + R"(,"output":)" + S(moves) + R"(}],)"
        R"("channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}])");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m && m->skin && m->clips.size() == 1);
    if (!m || !m->skin || m->clips.size() != 1) return;
    const Bone& root = m->skin->skeleton.bones[0];
    CHECK(NearV(root.rest.t, {5, 0, 0}) && NearV(root.rest.s, {2, 2, 2}));
    // 1 秒で Root が (0, 1, 0) 動く -> アーマチュアの空間では (5, 2, 0)。3 つとも書く。
    const BoneTrack& t = m->clips[0].tracks[0];
    CHECK(t.t.size() == 31 && NearV(t.t[30], {5, 2, 0}) && t.r.size() == 1 && t.s.size() == 1 && NearV(t.s[0], {2, 2, 2}));
}

// 骨の名前: 日本語、同じ名前 (そのまま。動かすと警告)、名前の無いものは「骨 N」。
// 2 つのスキンが同じ骨に違う逆バインドを書く: 最初のもの + 警告。
RM_TEST(GltfSkin_NamesAndTwoSkins) {
    Builder b;
    const SkinnedTri tri = Tri(b, {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    const SkinnedTri tri0 = Tri(b, std::vector<uint16_t>(12, 0), {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    const int first = b.Floats(UnTranslate(0, 1, 0), "MAT4", 1);
    std::vector<float> two = UnTranslate(0, 3, 0);
    const std::vector<float> more = UnTranslate(0, 0, 0);
    two.insert(two.end(), more.begin(), more.end());
    const int second = b.Floats(two, "MAT4", 2);
    const int times = b.Floats({0, 1}, "SCALAR", 2);
    const int moves = b.Floats({0, 0, 0, 0, 1, 0}, "VEC3", 2);
    const std::string doc = b.Doc(
        R"("nodes":[{"mesh":0,"skin":1},{"mesh":1,"skin":0},{"name":"腕","children":[3,4]},{"name":"腕"},{}],)"
        R"("skins":[{"joints":[2],"inverseBindMatrices":)" + S(first) + R"(},{"joints":[2,3],"inverseBindMatrices":)" + S(second) + "},{\"joints\":[4]}]," +
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":)" + S(tri.pos) + R"(,"JOINTS_0":)" + S(tri.joints) + R"(,"WEIGHTS_0":)" + S(tri.weights) +
        R"(}}]},{"primitives":[{"attributes":{"POSITION":)" + S(tri0.pos) + R"(,"JOINTS_0":)" + S(tri0.joints) + R"(,"WEIGHTS_0":)" + S(tri0.weights) +
        R"(}}]}],"animations":[{"name":"振る","samplers":[{"input":)" + S(times) + R"(,"output":)" + S(moves) + R"(}],)"
        R"("channels":[{"sampler":0,"target":{"node":3,"path":"translation"}}]}])");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m && m->skin);
    if (!m || !m->skin) return;
    const Skeleton& sk = m->skin->skeleton;
    CHECK(sk.bones.size() == 3 && sk.bones[0].name == "腕" && sk.bones[1].name == "腕" && sk.bones[2].name == "骨 2");
    CHECK(sk.bones[1].parent == 0 && sk.bones[2].parent == 0);
    // 骨 0: 最初のスキンの (0, -1, 0)。2 つ目のスキンの (0, -3, 0) は使わない。
    CHECK(NearV(TransformPoint(m->skin->inverseBind[0], {0, 0, 0}), {0, -1, 0}));
    CHECK(HasWarning(*m, "スキンによってバインドの姿勢が違います"));
    CHECK(m->clips.size() == 1 && m->clips[0].name == "振る" && m->clips[0].tracks[0].bone == "腕");
    CHECK(HasWarning(*m, "同じ名前の骨"));
    // 1 つ目のメッシュ (スキン 1): その joints の 1 番 (= ノード 3 = 骨 1) を指す。2 つ目 (スキン 0): 0 番 (= 骨 0)。
    CHECK(m->skin->vertices.size() == 6 && m->skin->vertices[1].bone[0] == 1 && m->skin->vertices[4].bone[0] == 0);
}

// 読めないもの: 骨が多すぎる、無いノードの骨、骨の番号がスキンの骨の数を超える、壊れたサンプラー。
RM_TEST(GltfSkin_BrokenFilesAreToldWhy) {
    std::string error;
    {
        std::string nodes = R"({"mesh":0,"skin":0})", joints;
        for (uint32_t i = 0; i <= kMaxBones; ++i) {
            nodes += ",{}";
            joints += (i ? "," : "") + std::to_string(i + 1);
        }
        Builder b;
        const SkinnedTri tri = Tri(b, std::vector<uint16_t>(12, 0), {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
        const std::string doc = b.Doc(R"("nodes":[)" + nodes + R"(],"skins":[{"joints":[)" + joints + "]}]," + SkinnedMesh(tri));
        CHECK(!ParseGltf(Bytes(doc), nullptr, &error) && error.find("1024") != std::string::npos);
    }
    {
        Builder b;
        const SkinnedTri tri = Tri(b, std::vector<uint16_t>(12, 0), {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
        CHECK(!ParseGltf(Bytes(b.Doc(R"("nodes":[{"mesh":0,"skin":0}],"skins":[{"joints":[7]}],)" + SkinnedMesh(tri))), nullptr, &error));
        CHECK(!error.empty());
    }
    {
        Builder b;
        const SkinnedTri tri = Tri(b, {5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
        CHECK(!ParseGltf(Bytes(b.Doc(R"("nodes":[{"mesh":0,"skin":0},{}],"skins":[{"joints":[1]}],)" + SkinnedMesh(tri))), nullptr, &error));
        CHECK(error.find("骨の番号 5") != std::string::npos);
    }
    {
        // 時刻が減っていく。
        Builder b;
        const SkinnedTri tri = Tri(b, std::vector<uint16_t>(12, 0), {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
        const int times = b.Floats({1, 0}, "SCALAR", 2);
        const int moves = b.Floats({0, 0, 0, 0, 1, 0}, "VEC3", 2);
        const std::string doc = b.Doc(R"("nodes":[{"mesh":0,"skin":0},{}],"skins":[{"joints":[1]}],)" + SkinnedMesh(tri) +
                                      R"(,"animations":[{"samplers":[{"input":)" + S(times) + R"(,"output":)" + S(moves) + R"(}],)"
                                      R"("channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}])");
        CHECK(!ParseGltf(Bytes(doc), nullptr, &error) && error.find("時刻") != std::string::npos);
    }
}
