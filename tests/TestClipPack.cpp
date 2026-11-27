// クリップの圧縮 (plugins/rm_engine/space3d/runtime/ClipPack.h。docs/ANIMATION.md 7-3) のテスト: 回転の 48 ビット・
// 骨の先の誤差が上限の中・小さくなる・圧縮しないものはそのまま・焼いて読み戻す・壊れた圧縮のチャンクは断る・
// アニメーションの姿勢 (animation3d の SampleClip) も圧縮したクリップから同じに作る。

#include "Test.h"

#include "rm_engine/animation3d/runtime/Pose.h"
#include "rm_engine/space3d/runtime/ClipPack.h"
#include "rm_engine/space3d/runtime/ClipSettings.h"
#include "rm_engine/space3d/runtime/MeshBake.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace rm;
using namespace rm::space3d;

namespace {

float Dist(Vec3 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

// 骨 8 本: 根 -> 背骨 -> 首 -> 頭 の鎖 (0.3 m ずつ) と、背骨から腕 -> 前腕 -> 手 (0.3 m ずつ)・遠くの小物 1 つ。
Skeleton Body() {
    Skeleton s;
    auto add = [&](const char* name, int parent, Vec3 t) {
        Bone b;
        b.name = name;
        b.parent = parent;
        b.rest.t = t;
        s.bones.push_back(b);
    };
    add("根", -1, {0, 1, 0});
    add("背骨", 0, {0, 0.3f, 0});
    add("首", 1, {0, 0.3f, 0});
    add("頭", 2, {0, 0.3f, 0});
    add("腕", 1, {0.3f, 0, 0});
    add("前腕", 4, {0.3f, 0, 0});
    add("手", 5, {0.3f, 0, 0});
    add("小物", -1, {0, 0, 0});
    return s;
}

// 4 秒 (30 fps) の動き: どの骨も違う速さで回り、根は 6 m 進み、小物は 300 m 飛ぶ (16 ビットの範囲に入らない)・
// 首の大きさはずっと同じ・頭は動かない。
Clip Moves(const Skeleton& s) {
    Clip c;
    c.name = "歩く";
    c.duration = 4;
    c.frames = ClipFrames(c.duration, c.rate);
    for (size_t i = 0; i < s.bones.size(); ++i) {
        BoneTrack t;
        t.bone = s.bones[i].name;
        if (t.bone == "頭") {
            c.tracks.push_back(t);
            continue;
        }
        for (uint32_t f = 0; f < c.frames; ++f) {
            const float x = static_cast<float>(f) / 30.f;
            const float k = 1.f + static_cast<float>(i) * 0.37f;
            t.r.push_back(QuatAxisAngle({std::sin(k), 1, std::cos(k)}, 0.8f * std::sin(x * k * 2.1f) + 0.1f * std::sin(x * 9.f)));
            if (i == 0) t.t.push_back({x * 1.5f, 1 + 0.05f * std::sin(x * 6), 0});
            if (t.bone == "小物") t.t.push_back({x * 75.f, std::sin(x) * 2, 0});
        }
        if (t.bone == "首") t.s = {{1, 1.2f, 1}};
        c.tracks.push_back(t);
    }
    return c;
}

// 姿勢 (刻みの位置 frame) の、骨ごとのモデルの空間の場所。raw: 圧縮しないクリップ (frame は整数)。
std::vector<Vec3> Places(const Skeleton& s, const Clip& c, float frame) {
    std::vector<Mat4> world(s.bones.size());
    std::vector<Vec3> out(s.bones.size());
    const auto f = static_cast<size_t>(frame);
    for (size_t i = 0; i < s.bones.size(); ++i) {
        BoneTRS p = s.bones[i].rest;
        const int ti = [&] {
            for (size_t k = 0; k < c.tracks.size(); ++k) {
                if (c.tracks[k].bone == s.bones[i].name) return static_cast<int>(k);
            }
            return -1;
        }();
        if (ti >= 0 && !c.packed.empty()) {
            const PackedTrack& pt = c.packed[static_cast<size_t>(ti)];
            p.t = SamplePackedVec(pt.t, frame, p.t);
            p.r = SamplePackedQuat(pt.r, frame, p.r);
            p.s = SamplePackedVec(pt.s, frame, p.s);
        } else if (ti >= 0) {
            // (刻みの間は、前後のサンプルを直線で混ぜる: animation3d の SampleChannel と同じ)
            const BoneTrack& tr = c.tracks[static_cast<size_t>(ti)];
            const float w = frame - static_cast<float>(f);
            auto mixV = [&](const std::vector<Vec3>& v, Vec3 rest) {
                if (v.empty()) return rest;
                const Vec3 a = v[std::min(f, v.size() - 1)], b = v[std::min(f + 1, v.size() - 1)];
                return Vec3{a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w};
            };
            p.t = mixV(tr.t, p.t);
            p.s = mixV(tr.s, p.s);
            if (!tr.r.empty()) {
                const Quat a = tr.r[std::min(f, tr.r.size() - 1)];
                Quat b = tr.r[std::min(f + 1, tr.r.size() - 1)];
                if (QuatDot(a, b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
                p.r = QuatNormalize({a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w, a.w + (b.w - a.w) * w});
            }
        }
        const int parent = s.bones[i].parent;
        world[i] = parent >= 0 ? world[static_cast<size_t>(parent)] * TRSMatrix(p) : TRSMatrix(p);
        out[i] = TransformPoint(world[i], {0, 0, 0});
    }
    return out;
}

// 全部の刻みで、骨の場所の一番大きなずれ。
float WorstError(const Skeleton& s, const Clip& raw, const Clip& packed) {
    float worst = 0;
    for (uint32_t f = 0; f < raw.frames; ++f) {
        const std::vector<Vec3> a = Places(s, raw, static_cast<float>(f)), b = Places(s, packed, static_cast<float>(f));
        for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, Dist(a[i], b[i]));
    }
    return worst;
}

}  // namespace

RM_TEST(ClipPack_Quat48RoundTrip) {
    float worst = 0;
    for (int k = 0; k < 2000; ++k) {
        const float a = static_cast<float>(k) * 0.731f, b = static_cast<float>(k) * 1.37f;
        const Quat q = QuatAxisAngle({std::sin(a), std::cos(b), std::sin(a + b)}, static_cast<float>(k % 360) * 0.0175f - 3.1f);
        uint16_t bits[3];
        PackQuat48(q, bits);
        const Quat back = UnpackQuat48(bits);
        // 回転の違い = 4 つの数の向きの間の角 x 2 (acos は 1 の近くで丸めが大きいので atan2 で)。
        const double sign = double{q.x} * back.x + double{q.y} * back.y + double{q.z} * back.z + double{q.w} * back.w < 0 ? -1 : 1;
        const double dx = q.x - sign * back.x, dy = q.y - sign * back.y, dz = q.z - sign * back.z, dw = q.w - sign * back.w;
        const double sx = q.x + sign * back.x, sy = q.y + sign * back.y, sz = q.z + sign * back.z, sw = q.w + sign * back.w;
        worst = std::max(worst, static_cast<float>(4.0 * std::atan2(std::sqrt(dx * dx + dy * dy + dz * dz + dw * dw),
                                                                    std::sqrt(sx * sx + sy * sy + sz * sz + sw * sw))));
        CHECK(std::fabs(QuatDot(back, back) - 1) < 1e-5f);
    }
    CHECK(worst < 2e-4f);  // (0.012 度より小さい)
    // 回らないもの・一番大きい成分が負のもの (-q は同じ回転)。
    uint16_t bits[3];
    PackQuat48({}, bits);
    CHECK(std::fabs(UnpackQuat48(bits).w - 1) < 1e-4f);
    PackQuat48({0, 0, 0, -1}, bits);
    CHECK(std::fabs(std::fabs(UnpackQuat48(bits).w) - 1) < 1e-4f);
}

RM_TEST(ClipPack_TipErrorWithinTolerance) {
    const Skeleton body = Body();
    const Clip raw = Moves(body);
    for (const ClipCompression level : {ClipCompression::Normal, ClipCompression::Strong}) {
        Clip c = raw;
        const float tolerance = PackTolerance(level);
        PackClip(c, body, tolerance);
        CHECK(c.packed.size() == c.tracks.size());
        for (const BoneTrack& t : c.tracks) CHECK(t.t.empty() && t.r.empty() && t.s.empty() && !t.bone.empty());
        CHECK(WorstError(body, raw, c) <= tolerance * 1.01f);
        // 小さくなる (強くの方がもっと)。圧縮しないときの大きさは、圧縮した後でも同じに求まる。
        CHECK(RawClipBytes(c) == ClipBytes(raw));
        CHECK(ClipBytes(c) * 2 < ClipBytes(raw));  // (この動きは細かい: 本物のモーションはもっと小さくなる)
        // 列の種類: 頭は動かない・首の大きさはずっと同じ・小物の位置は float・根の位置は 16 ビット。
        CHECK(c.packed[3].r.kind == PackedChannel::Kind::Empty && c.packed[2].s.kind == PackedChannel::Kind::Constant);
        CHECK(c.packed[7].t.kind == PackedChannel::Kind::KeysFloat && c.packed[0].t.kind == PackedChannel::Kind::Keys16);
        for (const PackedTrack& p : c.packed) {
            CHECK(PackedChannelValid(p.t, c.frames, false) && PackedChannelValid(p.r, c.frames, true) && PackedChannelValid(p.s, c.frames, false));
        }
    }
    Clip normal = raw, strong = raw;
    PackClip(normal, body, PackTolerance(ClipCompression::Normal));
    PackClip(strong, body, PackTolerance(ClipCompression::Strong));
    CHECK(ClipBytes(strong) < ClipBytes(normal));
    // 刻みの間 (どちらも直線で戻す) も、ほぼ上限の中 (サンプルの所だけで鍵を選ぶので、少し広く見る)。
    float between = 0;
    for (uint32_t f = 0; f + 1 < raw.frames; ++f) {
        const std::vector<Vec3> a = Places(body, raw, static_cast<float>(f) + 0.5f), b = Places(body, normal, static_cast<float>(f) + 0.5f);
        for (size_t i = 0; i < b.size(); ++i) between = std::max(between, Dist(a[i], b[i]));
    }
    std::printf("ClipPack: between %.5f m  bytes raw %zu normal %zu strong %zu\n", between, ClipBytes(raw), ClipBytes(normal), ClipBytes(strong));
    CHECK(between < PackTolerance(ClipCompression::Normal) * 2);
}

RM_TEST(ClipPack_NoneAndOddClipsStayRaw) {
    const Skeleton body = Body();
    Clip c = Moves(body);
    const Clip raw = c;
    PackClip(c, body, PackTolerance(ClipCompression::None));
    CHECK(c.packed.empty() && ClipBytes(c) == ClipBytes(raw) && c.tracks[0].r == raw.tracks[0].r);
    // 列の長さが frames でない (壊れた) クリップは圧縮しない。2 度目の圧縮もしない。
    Clip odd = raw;
    odd.tracks[1].r.pop_back();
    PackClip(odd, body, 0.001f);
    CHECK(odd.packed.empty());
    Clip twice = raw;
    PackClip(twice, body, 0.001f);
    const size_t once = ClipBytes(twice);
    PackClip(twice, body, 0.004f);
    CHECK(ClipBytes(twice) == once);
    // 骨の無い名前の動きも圧縮する (先までを 1 m として)。
    Skeleton other = body;
    other.bones.pop_back();
    Clip lost = raw;
    PackClip(lost, other, 0.001f);
    CHECK(lost.packed.size() == lost.tracks.size());
    // 設定の読み込み: "bake" の圧縮といつも入れる (無い・知らない値は既定 = ふつう)。
    std::vector<Clip> clips = {raw, raw};
    clips[1].name = "走る";
    const auto import = json::Parse(R"({"clips": {"歩く": {"bake": {"compression": "None", "always": true}}, "走る": {"bake": {"compression": "Huge"}}}})");
    CHECK(import.has_value());
    if (import) ApplyClipSettings(clips, *import);
    CHECK(clips[0].bake.compression == ClipCompression::None && clips[0].bake.always);
    CHECK(clips[1].bake.compression == ClipCompression::Normal && !clips[1].bake.always);
    PackClips(clips, body);
    CHECK(clips[0].packed.empty() && clips[1].packed.size() == clips[1].tracks.size());
}

RM_TEST(ClipPack_BakeRoundTripAndBroken) {
    const Skeleton body = Body();
    MeshData m;
    m.vertices = {{{0, 0, 0}, {0, 1, 0}, {0, 0}}, {{1, 0, 0}, {0, 1, 0}, {1, 0}}, {{0, 1, 0}, {0, 1, 0}, {0, 1}}};
    m.indices = {0, 1, 2};
    m.max = {1, 1, 0};
    m.parts = {{0, 3, -1}};
    SkinData skin;
    skin.skeleton = body;
    skin.vertices.resize(3);
    for (SkinVertex& v : skin.vertices) v.weight = {65535, 0, 0, 0};
    skin.inverseBind.assign(body.bones.size(), Mat4{});
    std::vector<Vec3> positions;
    for (const MeshVertex& v : m.vertices) positions.push_back(v.position);
    ComputeBoneBounds(skin, positions);
    m.skin = skin;
    Clip packed = Moves(body);
    PackClip(packed, body, 0.001f);
    Clip plain = Moves(body);
    plain.name = "そのまま";
    m.clips = {packed, plain};

    std::string err;
    const std::string bytes = WriteBakedMesh(m, {});
    const auto back = ReadBakedMesh(bytes, &err);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->clips.size() == 2 && back->clips[0].packed.size() == packed.packed.size() && back->clips[1].packed.empty());
    CHECK(back->clips[1].tracks[0].r == plain.tracks[0].r);
    CHECK(WriteBakedMesh(*back, {}) == bytes);
    for (const float f : {0.f, 17.25f, 63.5f, 120.f}) {
        const std::vector<Vec3> a = Places(body, packed, f), b = Places(body, back->clips[0], f);
        for (size_t i = 0; i < a.size(); ++i) CHECK(Dist(a[i], b[i]) < 1e-6f);
    }
    // 壊れた圧縮: 鍵が増える順でない・最後の鍵が frames - 1 でない → 読まない (外を読まない)。
    MeshData bad = m;
    for (PackedTrack& p : bad.clips[0].packed) {
        if (p.r.keys.size() >= 3) {
            std::swap(p.r.keys[1], p.r.keys[2]);
            break;
        }
    }
    CHECK(!ReadBakedMesh(WriteBakedMesh(bad, {}), &err) && err.find("CPAK") != std::string::npos);
    bad = m;
    bad.clips[0].packed[0].r.keys.back() = 5;
    CHECK(!ReadBakedMesh(WriteBakedMesh(bad, {}), &err));
    bad = m;
    bad.clips[0].packed.pop_back();
    CHECK(!ReadBakedMesh(WriteBakedMesh(bad, {}), &err));
    // 途中で切れたファイル。
    for (const size_t cut : {size_t{40}, size_t{400}, size_t{4000}}) {
        if (cut < bytes.size()) CHECK(!ReadBakedMesh(std::string_view(bytes).substr(0, bytes.size() - cut), &err));
    }
}

RM_TEST(ClipPack_PoseFromPackedClip) {
    // animation3d の姿勢 (SampleClip) は、圧縮したクリップでも骨の名前で引いて、同じ式で作る。
    const Skeleton body = Body();
    const Clip raw = Moves(body);
    Clip packed = raw;
    PackClip(packed, body, 0.001f);
    const animation3d::ClipBinding a = animation3d::BindClip(raw, body), b = animation3d::BindClip(packed, body);
    CHECK(a.track == b.track && b.unmatched == 0);
    animation3d::Pose pa, pb;
    for (const float t : {0.f, 0.5f, 1.234f, 3.99f, 4.f}) {
        animation3d::SampleClip(raw, a, body, t, false, pa);
        animation3d::SampleClip(packed, b, body, t, false, pb);
        CHECK(pa.size() == pb.size());
        for (size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
            CHECK(Dist(pa[i].t, pb[i].t) < 0.002f);
            CHECK(std::fabs(QuatDot(pa[i].r, pb[i].r)) > 0.9999f);
        }
    }
}

// クリップのファイル (.rmclip): 1 つのクリップを書いて同じに読み戻す (圧縮したもの・イベント・ルートモーションも)・
// モデルの一覧 (CLPF) から読む・壊れた物と無い物は飛ばして理由を返す。
RM_TEST(ClipPack_ClipFilesRoundTrip) {
    const Skeleton body = Body();
    Clip walk = Moves(body);
    PackClip(walk, body, 0.001f);
    walk.events = {{0.5f, "Step", 1, "左"}};
    walk.root.enabled = true;
    Clip idle = Moves(body);
    idle.name = "待つ";
    const std::string a = WriteBakedClip(walk), b = WriteBakedClip(idle);
    std::string err;
    const auto back = ReadBakedClip(a, &err);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->name == "歩く" && back->packed.size() == walk.packed.size() && back->events.size() == 1 && back->root.enabled);
    CHECK(WriteBakedClip(*back) == a);
    // 壊れた物・違う物。
    CHECK(!ReadBakedClip("RMCLIP", &err) && !err.empty());
    CHECK(!ReadBakedClip(std::string_view(a).substr(0, a.size() - 7), &err));
    std::string newer = a;
    newer[8] = 9;
    CHECK(!ReadBakedClip(newer, &err) && err.find("新しい") != std::string::npos);
    MeshData mesh;
    CHECK(!ReadBakedClip(WriteBakedMesh(mesh, {}), &err));
    // モデルの一覧から読む (並びの順。無い・壊れた物は飛ばす)。一覧は焼いたモデルに入って戻る。
    mesh.vertices = {{{0, 0, 0}, {0, 1, 0}, {0, 0}}, {{1, 0, 0}, {0, 1, 0}, {1, 0}}, {{0, 1, 0}, {0, 1, 0}, {0, 1}}};
    mesh.indices = {0, 1, 2};
    mesh.parts = {{0, 3, -1}};
    mesh.clipFiles = {{"歩く", "Baked/m/clip0.rmclip"}, {"待つ", "Baked/m/clip1.rmclip"}, {"無い", "Baked/m/clip2.rmclip"}, {"壊れた", "Baked/m/clip3.rmclip"}};
    const auto baked = ReadBakedMesh(WriteBakedMesh(mesh, {}), &err);
    CHECK(baked && baked->clipFiles.size() == 4 && baked->clipFiles[1].name == "待つ" && baked->clips.empty());
    if (!baked) return;
    MeshData loaded = *baked;
    const std::vector<std::string> problems = LoadClipFiles(loaded, [&](std::string_view path) -> std::optional<std::string> {
        if (path == "Baked/m/clip0.rmclip") return a;
        if (path == "Baked/m/clip1.rmclip") return b;
        if (path == "Baked/m/clip3.rmclip") return a.substr(0, 20);
        return std::nullopt;
    });
    CHECK(loaded.clips.size() == 2 && loaded.clips[0].name == "歩く" && loaded.clips[1].name == "待つ" && loaded.clipFiles.empty());
    CHECK(problems.size() == 2);
}

// 書き出すクリップ: 最初のもの・いつも入れる・シーン / 状態とルールの文字列の値・スクリプトに "名前" と書いたもの。
RM_TEST(ClipPack_UsedClipsOnly) {
    std::vector<Clip> clips(6);
    const char* names[] = {"Idle", "Walk", "Run", "Jump", "歩く2", "Dance"};
    for (size_t i = 0; i < clips.size(); ++i) clips[i].name = names[i];
    clips[5].bake.always = true;
    const std::vector<std::string> docs = {
        R"({"entities": [{"components": [{"type": "rm::game_x_animation::Animator", "data": {"clip": "Walk"}}]}]})",
        R"({"kind": "rm.animator", "states": [{"name": "走る", "clip": "Run"}], "layers": [{"states": [{"motions": [{"clip": "\u6b69\u304f2"}]}]}]})",
        "void Update() { anim.PlayClip(\"Jump\", 0.2f); }  // Dance (書いてあるだけ: 引用符が無い)",
        "{ broken json \"Idle2\" ",
    };
    const std::vector<bool> used = UsedClips(clips, docs);
    CHECK(used.size() == 6);
    CHECK(used[0] && used[1] && used[2] && used[3] && used[4] && used[5]);
    // どこにも名前が無い物は入れない (キーの名前・引用符の無い文字は数えない)。
    clips[5].bake.always = false;
    clips.push_back({});
    clips.back().name = "clip";
    const std::vector<bool> again = UsedClips(clips, docs);
    CHECK(!again[5] && !again[6]);
    CHECK(UsedClips({}, docs).empty());
    CHECK(UsedClips(clips, {})[0] && !UsedClips(clips, {})[1]);
}
