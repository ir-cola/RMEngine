// FBX のスキンとテイク (plugins/rm_engine/space3d/runtime/Fbx) のテスト: 骨の木・ウェイト・逆バインド
// (休みの姿勢でスキニングした頂点 = 読んだ頂点)・バインドとファイルの姿勢が違うとき・テイクを刻みごとの
// サンプルに焼く (前の回転・ピボット・回転の順・一定のキー)・刻みと長さ・5 本以上の骨の警告・骨の上限。
// どのファイルも Z が上・1 単位 10 cm (ファイルの軸と単位を直すのを、骨とクリップにも確かめる)。

#include "Test.h"

#include "rm_engine/space3d/runtime/Fbx.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>
#include <format>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::space3d;

namespace {

std::vector<uint8_t> Bytes(const std::string& s) { return {s.begin(), s.end()}; }
bool NearV(Vec3 a, Vec3 b, float eps = 1e-4f) { return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.z - b.z) < eps; }
bool NearM(const Mat4& a, const Mat4& b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(a.m[c][r] - b.m[c][r]) > eps) return false;
        }
    }
    return true;
}
Mat4 T(Vec3 t) {
    Mat4 m;
    m.m[3][0] = t.x, m.m[3][1] = t.y, m.m[3][2] = t.z;
    return m;
}
Mat4 Inv(const Mat4& m) { return Inverse(m).value_or(Mat4{}); }
Mat4 RotationOnly(Mat4 m) {
    m.m[3][0] = m.m[3][1] = m.m[3][2] = 0;
    return m;
}

// ファイルの軸 (Z が上・-Y が前・X が右。`coordSign` = -1 で X を鏡に) と単位 (10 cm) -> このエンジン。
Mat4 Axes(float coordSign) {
    Mat4 m;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) m.m[c][r] = 0;
    }
    m.m[0][0] = 0.1f * coordSign;  // X <- ファイルの X
    m.m[2][1] = 0.1f;              // Y <- ファイルの Z
    m.m[1][2] = -0.1f;             // Z <- ファイルの -Y
    m.m[3][3] = 1;
    return m;
}
Mat4 Conj(float coordSign, const Mat4& m) { return Axes(coordSign) * m * Inv(Axes(coordSign)); }

// --- 小さな ASCII FBX 7.4 を書く --------------------------------------------------------------

std::string Num(double v) { return std::format("{}", v); }
std::string P3(std::string_view name, Vec3 v) { return std::format("\t\t\tP: \"{}\", \"Vector3D\", \"Vector\", \"\",{},{},{}\n", name, Num(v.x), Num(v.y), Num(v.z)); }
std::string P1(std::string_view name, double v) { return std::format("\t\t\tP: \"{}\", \"Number\", \"\", \"A\",{}\n", name, Num(v)); }
std::string Arr(std::string_view name, const std::vector<double>& v) {
    std::string s = std::format("\t\t{}: *{} {{\n\t\t\ta: ", name, v.size());
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + Num(v[i]);
    return s + "\n\t\t}\n";
}
std::vector<double> Floats(const Mat4& m) {
    std::vector<double> v;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) v.push_back(m.m[c][r]);
    }
    return v;
}
std::string Model(int64_t id, std::string_view name, std::string_view type, const std::string& props) {
    return std::format("\tModel: {}, \"Model::{}\", \"{}\" {{\n\t\tVersion: 232\n\t\tProperties70:  {{\n{}\t\t}}\n\t\tShading: Y\n\t}}\n", id, name, type, props);
}
std::string Header(const std::string& settings) {
    return "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXVersion: 7400\n}\nGlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n" + settings +
           "\t}\n}\n";
}
std::string Settings(float coordSign, int timeMode, double customRate) {
    std::string s = "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",2\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
                    "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",1\n\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",-1\n"
                    "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n";
    s += std::format("\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",{}\n", coordSign < 0 ? -1 : 1);
    s += "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",10\n";
    if (timeMode >= 0) s += std::format("\t\tP: \"TimeMode\", \"enum\", \"\", \"\",{}\n", timeMode);
    if (customRate > 0) s += std::format("\t\tP: \"CustomFrameRate\", \"double\", \"Number\", \"\",{}\n", Num(customRate));
    return s;
}
std::string Curve(int64_t id, const std::vector<double>& seconds, const std::vector<double>& values, int flags) {
    std::vector<double> ticks;
    for (double s : seconds) ticks.push_back(std::round(s * 46186158000.0));
    return std::format("\tAnimationCurve: {}, \"AnimCurve::\", \"\" {{\n\t\tDefault: 0\n\t\tKeyVer: 4008\n", id) + Arr("KeyTime", ticks) +
           Arr("KeyValueFloat", values) + Arr("KeyAttrFlags", {static_cast<double>(flags)}) + Arr("KeyAttrDataFloat", {0, 0, 0, 0}) +
           Arr("KeyAttrRefCount", {static_cast<double>(seconds.size())}) + "\t}\n";
}

// 骨 3 本 (腰 -> 背骨 -> Head。上に Null の Armature) のキャラクター: 体のメッシュ (自分の位置と回転と
// ジオメトリのオフセットを持つ) はスキンで動き、帽子のメッシュは Head の子 (スキンなし)。
//   背骨: 前の回転 Z 90・回転のピボット (1, 0, 0)・回転の順 ZXY。Head: 後の回転 Y 10。
// テイク「歩く」: 腰の T (X は 0 -> 4 を直線、Z は 10 -> 0.5 秒で 20 に一定のキー)、背骨の R (Z: 0 -> 90)。
struct Character {
    float coordSign = 1;
    bool skin = true;
    bool take = false;
    bool localRange = true;
    bool extraLayer = false;
    int timeMode = 11;  // 24 コマ
    double customRate = 0;
    Vec3 hips{0, 0, 10};   // ファイルの腰の位置 (Lcl Translation)
    float spine = 0;       // ファイルの背骨の Lcl Rotation の Z
    Vec3 bindHips{0, 0, 10};  // クラスターの TransformLink を作る値 (バインドしたとき)
    float bindSpine = 0;
};

// ファイルの値でのワールド行列 (ファイルの軸のまま)。テストの側で手で書いた式。
Mat4 ArmatureWorld() { return T({1, 2, 0}); }
Mat4 HipsWorld(Vec3 hips) { return ArmatureWorld() * T(hips); }
Mat4 SpineLocal(float z) { return T({0, 0, 10}) * T({1, 0, 0}) * RotationMatrix({0, 0, 90 + z}) * T({-1, 0, 0}); }
Mat4 SpineWorld(Vec3 hips, float z) { return HipsWorld(hips) * SpineLocal(z); }
Mat4 HeadWorld(Vec3 hips, float z) { return SpineWorld(hips, z) * T({0, 0, 10}) * Inv(RotationMatrix({0, 10, 0})); }
Mat4 BodyWorld() { return T({3, 0, 0}) * RotationMatrix({0, 0, 30}); }

std::string Cluster(int64_t id, const std::vector<double>& indexes, const std::vector<double>& weights, const Mat4& link) {
    return std::format("\tDeformer: {}, \"SubDeformer::\", \"Cluster\" {{\n\t\tVersion: 100\n\t\tUserData: \"\", \"\"\n", id) + Arr("Indexes", indexes) +
           Arr("Weights", weights) + Arr("Transform", Floats(BodyWorld())) + Arr("TransformLink", Floats(link)) + "\t}\n";
}

std::string Doc(const Character& o) {
    std::string s = Header(Settings(o.coordSign, o.timeMode, o.customRate));
    s += "Objects:  {\n";
    s += "\tGeometry: 100, \"Geometry::Body\", \"Mesh\" {\n" + Arr("Vertices", {0, 0, 0, 1, 0, 0, 0, 0, 10, 1, 0, 10, 0, 0, 20, 1, 0, 20}) +
         Arr("PolygonVertexIndex", {0, 1, 3, -3, 2, 3, 5, -5}) + "\t}\n";
    s += "\tGeometry: 110, \"Geometry::Hat\", \"Mesh\" {\n" + Arr("Vertices", {0, 0, 0, 1, 0, 0, 0, 1, 1}) + Arr("PolygonVertexIndex", {0, 1, -3}) + "\t}\n";
    s += Model(300, "Armature", "Null", P3("Lcl Translation", {1, 2, 0}));
    s += Model(301, "腰", "LimbNode", P3("Lcl Translation", o.hips));
    s += Model(302, "背骨", "LimbNode",
               P3("Lcl Translation", {0, 0, 10}) + P3("PreRotation", {0, 0, 90}) + P3("RotationPivot", {1, 0, 0}) + P3("Lcl Rotation", {0, 0, o.spine}) +
                   "\t\t\tP: \"RotationOrder\", \"enum\", \"\", \"\",4\n");
    s += Model(303, "Head", "LimbNode", P3("Lcl Translation", {0, 0, 10}) + P3("PostRotation", {0, 10, 0}));
    s += Model(200, "Body", "Mesh", P3("Lcl Translation", {3, 0, 0}) + P3("Lcl Rotation", {0, 0, 30}) + P3("GeometricTranslation", {0, 0, 1}));
    s += Model(210, "Hat", "Mesh", P3("Lcl Translation", {0, 0, 2}));
    if (o.skin) {
        s += "\tDeformer: 400, \"Deformer::\", \"Skin\" {\n\t\tVersion: 101\n\t\tLink_DeformAcuracy: 50\n\t}\n";
        s += Cluster(401, {0, 1, 2, 3}, {1, 1, 0.5, 0.5}, HipsWorld(o.bindHips));
        s += Cluster(402, {2, 3, 4, 5}, {0.5, 0.5, 0.3, 0.3}, SpineWorld(o.bindHips, o.bindSpine));
        s += Cluster(403, {4, 5}, {0.7, 0.7}, HeadWorld(o.bindHips, o.bindSpine));
    }
    if (o.take) {
        s += "\tAnimationStack: 600, \"AnimStack::歩く\", \"\" {\n\t\tProperties70:  {\n";
        if (o.localRange) s += "\t\t\tP: \"LocalStart\", \"KTime\", \"Time\", \"\",0\n\t\t\tP: \"LocalStop\", \"KTime\", \"Time\", \"\",46186158000\n";
        s += "\t\t}\n\t}\n";
        s += "\tAnimationLayer: 601, \"AnimLayer::BaseLayer\", \"\" {\n\t}\n";
        if (o.extraLayer) s += "\tAnimationLayer: 602, \"AnimLayer::Extra\", \"\" {\n\t}\n";
        s += "\tAnimationCurveNode: 610, \"AnimCurveNode::T\", \"\" {\n\t\tProperties70:  {\n" + P1("d|X", 0) + P1("d|Y", 0) + P1("d|Z", 10) + "\t\t}\n\t}\n";
        s += "\tAnimationCurveNode: 611, \"AnimCurveNode::R\", \"\" {\n\t\tProperties70:  {\n" + P1("d|X", 0) + P1("d|Y", 0) + P1("d|Z", 0) + "\t\t}\n\t}\n";
        s += Curve(620, {0, 1}, {0, 4}, 0x4);              // 直線
        s += Curve(621, {0, 0.5, 1}, {10, 20, 20}, 0x2);   // 一定
        s += Curve(622, {0, 1}, {0, 90}, 0x4);              // 直線
    }
    s += "}\nConnections:  {\n";
    const auto oo = [&](int64_t child, int64_t parent) { s += std::format("\tC: \"OO\",{},{}\n", child, parent); };
    const auto op = [&](int64_t child, int64_t parent, std::string_view p) { s += std::format("\tC: \"OP\",{},{}, \"{}\"\n", child, parent, p); };
    oo(300, 0), oo(301, 300), oo(302, 301), oo(303, 302), oo(200, 0), oo(100, 200), oo(210, 303), oo(110, 210);
    if (o.skin) oo(400, 100), oo(401, 400), oo(402, 400), oo(403, 400), oo(301, 401), oo(302, 402), oo(303, 403);
    if (o.take) {
        oo(601, 600), oo(610, 601), oo(611, 601);
        if (o.extraLayer) oo(602, 600);
        op(610, 301, "Lcl Translation"), op(611, 302, "Lcl Rotation");
        op(620, 610, "d|X"), op(621, 610, "d|Z"), op(622, 611, "d|Z");
    }
    return s + "}\n";
}

std::optional<MeshData> Read(const Character& o, std::string* error = nullptr) {
    std::string e;
    auto m = ParseFbx(Bytes(Doc(o)), error ? error : &e);
    return m;
}

// --- スキニング (CPU で: 頂点 = sum(ウェイト x 骨の行列 x 逆バインド x 頂点)) ---------------------

std::vector<Mat4> ModelMatrices(const Skeleton& s, const std::vector<BoneTRS>& local) {
    std::vector<Mat4> m(s.bones.size());
    for (size_t b = 0; b < s.bones.size(); ++b) {
        const Mat4 l = TRSMatrix(local[b]);
        m[b] = s.bones[b].parent >= 0 ? m[static_cast<size_t>(s.bones[b].parent)] * l : l;
    }
    return m;
}
std::vector<BoneTRS> RestPose(const Skeleton& s) {
    std::vector<BoneTRS> p;
    for (const Bone& b : s.bones) p.push_back(b.rest);
    return p;
}
// クリップのコマ `f` の姿勢 (トラックの無い骨・列は休みの姿勢)。
std::vector<BoneTRS> ClipPose(const Skeleton& s, const Clip& clip, uint32_t f) {
    std::vector<BoneTRS> p = RestPose(s);
    for (const BoneTrack& t : clip.tracks) {
        const int b = s.Find(t.bone);
        if (b < 0) continue;
        BoneTRS& x = p[static_cast<size_t>(b)];
        if (!t.t.empty()) x.t = t.t[t.t.size() == 1 ? 0 : f];
        if (!t.r.empty()) x.r = t.r[t.r.size() == 1 ? 0 : f];
        if (!t.s.empty()) x.s = t.s[t.s.size() == 1 ? 0 : f];
    }
    return p;
}
std::vector<Vec3> Skinned(const MeshData& m, const std::vector<Mat4>& bones) {
    std::vector<Vec3> out;
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const SkinVertex& sv = m.skin->vertices[i];
        Vec3 p{0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            if (sv.weight[k] == 0) continue;
            const float w = sv.weight[k] / 65535.f;
            const Vec3 q = TransformPoint(bones[sv.bone[k]] * m.skin->inverseBind[sv.bone[k]], m.vertices[i].position);
            p = {p.x + w * q.x, p.y + w * q.y, p.z + w * q.z};
        }
        out.push_back(p);
    }
    return out;
}
bool SamePositions(const std::vector<Vec3>& a, const MeshData& m, float eps = 1e-4f) {
    if (a.size() != m.vertices.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!NearV(a[i], m.vertices[i].position, eps)) return false;
    }
    return true;
}
const BoneTrack* Track(const Clip& c, std::string_view bone) {
    for (const BoneTrack& t : c.tracks) {
        if (t.bone == bone) return &t;
    }
    return nullptr;
}

}  // namespace

RM_TEST(FbxSkin_BonesWeightsAndBindPose) {
    for (const float sign : {1.f, -1.f}) {  // -1: ファイルの X が鏡 (左手系のファイル)
        Character c;
        c.coordSign = sign;
        std::string error;
        const auto m = Read(c, &error);
        CHECK(m.has_value());
        if (!m) continue;
        CHECK(m->skin.has_value() && m->clips.empty() && m->warnings.empty());
        if (!m->skin) continue;
        const Skeleton& s = m->skin->skeleton;
        // 骨: Armature (つなぐ Null)・腰・背骨・Head と、Head の子の帽子 (スキンの無いメッシュ)。親が先。
        CHECK(s.bones.size() == 5 && ParentsFirst(s));
        const int arm = s.Find("Armature"), hips = s.Find("腰"), spine = s.Find("背骨"), head = s.Find("Head"), hat = s.Find("Hat");
        CHECK(arm == 0 && hips >= 0 && spine >= 0 && head >= 0 && hat >= 0 && s.Find("Body") < 0);
        if (arm < 0 || hips < 0 || spine < 0 || head < 0 || hat < 0) continue;
        CHECK(s.bones[0].parent == -1 && s.bones[static_cast<size_t>(hips)].parent == arm && s.bones[static_cast<size_t>(spine)].parent == hips);
        CHECK(s.bones[static_cast<size_t>(head)].parent == spine && s.bones[static_cast<size_t>(hat)].parent == head);
        CHECK(m->skin->vertices.size() == m->vertices.size() && m->skin->inverseBind.size() == 5 && m->skin->boundsMin.size() == 5);
        // 休みの姿勢: ファイルの値を、このエンジンの軸と単位で (ファイルの (x, y, z) -> 0.1 x (±x, z, -y))。
        CHECK(NearV(s.bones[0].rest.t, {0.1f * sign, 0, -0.2f}) && NearV(s.bones[static_cast<size_t>(hips)].rest.t, {0, 1, 0}));
        CHECK(NearM(TRSMatrix(s.bones[static_cast<size_t>(spine)].rest), Conj(sign, SpineLocal(0))));
        CHECK(NearM(m->skin->inverseBind[static_cast<size_t>(hips)], Inv(Conj(sign, HipsWorld({0, 0, 10})))));
        // ウェイト: 点 0 と 1 は腰だけ、帽子は帽子の骨だけ (合計はいつも 65535)。
        CHECK(m->skin->vertices[0].bone[0] == hips && m->skin->vertices[0].weight[0] == 65535);
        CHECK(m->skin->vertices[8].bone[0] == hat && m->skin->vertices[8].weight[0] == 65535);
        const SkinVertex& two = m->skin->vertices[3];  // 最初の面の 4 つ目の角 = 点 2 (腰 0.5・背骨 0.5)
        CHECK(two.weight[0] + two.weight[1] == 65535 && two.weight[2] == 0 && std::abs(int(two.weight[0]) - int(two.weight[1])) <= 1);
        // 休みの姿勢でスキニングすると、読んだ頂点のまま。
        CHECK(SamePositions(Skinned(*m, ModelMatrices(s, RestPose(s))), *m));
        // スキンをつながないファイルは、同じ頂点・骨なし。
        Character plain = c;
        plain.skin = false;
        const auto p = Read(plain);
        CHECK(p.has_value() && !p->skin && p->clips.empty());
        if (!p) continue;
        CHECK(p->vertices.size() == m->vertices.size() && p->indices == m->indices && p->parts == m->parts);
        bool same = p->vertices.size() == m->vertices.size();
        for (size_t i = 0; same && i < p->vertices.size(); ++i) {
            same = NearV(p->vertices[i].position, m->vertices[i].position) && NearV(p->vertices[i].normal, m->vertices[i].normal);
        }
        CHECK(same);
    }
}

RM_TEST(FbxSkin_BindPoseIsNotTheFilePose) {
    // ファイルの姿勢 (腰が上に 2、背骨が 30 度) は、バインドしたとき (TransformLink) と違う。
    Character c;
    c.hips = {0, 0, 12};
    c.spine = 30;
    const auto m = Read(c);
    CHECK(m.has_value() && m->skin.has_value());
    if (!m || !m->skin) return;
    const Skeleton& s = m->skin->skeleton;
    // バインドしたときの骨の行列でスキニングすると、読んだ頂点 (バインドしたときの形)。
    std::vector<Mat4> bind(s.bones.size());
    for (size_t b = 0; b < s.bones.size(); ++b) bind[b] = Inv(m->skin->inverseBind[b]);
    bind[static_cast<size_t>(s.Find("腰"))] = Conj(1, HipsWorld({0, 0, 10}));
    bind[static_cast<size_t>(s.Find("背骨"))] = Conj(1, SpineWorld({0, 0, 10}, 0));
    bind[static_cast<size_t>(s.Find("Head"))] = Conj(1, HeadWorld({0, 0, 10}, 0));
    CHECK(SamePositions(Skinned(*m, bind), *m));
    // 休みの姿勢 (ファイルの値) では動いている。骨の休みの姿勢はファイルの値のもの。
    CHECK(!SamePositions(Skinned(*m, ModelMatrices(s, RestPose(s))), *m, 0.01f));
    CHECK(NearM(TRSMatrix(s.bones[static_cast<size_t>(s.Find("背骨"))].rest), Conj(1, SpineLocal(30))));
    CHECK(NearV(s.bones[static_cast<size_t>(s.Find("腰"))].rest.t, {0, 1.2f, 0}));
}

RM_TEST(FbxSkin_TakeIsBakedToSamples) {
    for (const float sign : {1.f, -1.f}) {
        Character c;
        c.coordSign = sign;
        c.take = true;
        const auto m = Read(c);
        CHECK(m.has_value() && m->skin.has_value() && m->clips.size() == 1 && m->warnings.empty());
        if (!m || !m->skin || m->clips.size() != 1) continue;
        const Clip& clip = m->clips[0];
        const Skeleton& s = m->skin->skeleton;
        CHECK(clip.name == "歩く" && clip.rate == 24.f && std::abs(clip.duration - 1.f) < 1e-6f && clip.frames == 25);
        // 動く骨だけにトラック (曲線のある腰と背骨)。腰は回らず大きさも変わらない: その列は無い。
        CHECK(clip.tracks.size() == 2);
        const BoneTrack* hips = Track(clip, "腰");
        const BoneTrack* spine = Track(clip, "背骨");
        CHECK(hips && spine);
        if (!hips || !spine) continue;
        CHECK(hips->t.size() == 25 && hips->r.empty() && hips->s.empty());
        CHECK(spine->r.size() == 25 && spine->t.size() == 25 && spine->s.empty());
        // 腰の位置: X は直線 (4 x 時刻)、Z は 0.5 秒で 10 -> 20 (一定のキー)。ファイルの (x, y, z) -> 0.1 x (±x, z, -y)。
        CHECK(NearV(hips->t[6], {0.1f * sign, 1, 0}));                     // 0.25 秒
        CHECK(NearV(hips->t[11], {0.1f * sign * 4 * 11 / 24.f, 1, 0}));    // 0.458 秒: まだ 10
        CHECK(NearV(hips->t[12], {0.2f * sign, 2, 0}));                    // 0.5 秒: 20
        CHECK(NearV(hips->t[24], {0.4f * sign, 2, 0}));
        // 背骨: 前の回転 90 + 曲線 45 (0.5 秒)、回転のピボット (1, 0, 0) が位置を動かす。
        const Mat4 want = Conj(sign, SpineLocal(45));
        CHECK(NearM(QuatMatrix(spine->r[12]), RotationOnly(want)));
        CHECK(NearV(spine->t[12], {want.m[3][0], want.m[3][1], want.m[3][2]}));
        // 回転のサンプルは同じ側の四元数 (隣どうしで符号が飛ばない)。
        bool smooth = true;
        for (size_t f = 1; f < spine->r.size(); ++f) smooth = smooth && QuatDot(spine->r[f - 1], spine->r[f]) > 0;
        CHECK(smooth);
        // コマ 0 (バインドしたときの値) でスキニングすると読んだ頂点。
        CHECK(SamePositions(Skinned(*m, ModelMatrices(s, ClipPose(s, clip, 0))), *m));
        // コマ 12 は、その値を書いた (テイクの無い) ファイルの休みの姿勢と同じ形になる。
        Character still = c;
        still.take = false;
        still.hips = {2, 0, 20};
        still.spine = 45;
        const auto st = Read(still);
        CHECK(st.has_value() && st->skin.has_value());
        if (!st || !st->skin) continue;
        const std::vector<Vec3> a = Skinned(*m, ModelMatrices(s, ClipPose(s, clip, 12)));
        const std::vector<Vec3> b = Skinned(*st, ModelMatrices(st->skin->skeleton, RestPose(st->skin->skeleton)));
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i) same = NearV(a[i], b[i]);
        CHECK(same);
    }
}

RM_TEST(FbxSkin_RateAndRangeFromTheFile) {
    Character c;
    c.take = true;
    c.localRange = false;  // 長さはキーの範囲 (0 .. 1 秒)
    c.timeMode = -1;       // 刻みが書かれていない: 30
    auto m = Read(c);
    CHECK(m && m->clips.size() == 1 && m->clips[0].rate == kDefaultClipRate && m->clips[0].frames == 31 && std::abs(m->clips[0].duration - 1) < 1e-6f);
    c.timeMode = 14;  // 自分で決めた刻み (120) は上限 60 に
    c.customRate = 120;
    m = Read(c);
    CHECK(m && m->clips.size() == 1 && m->clips[0].rate == kMaxClipRate && m->clips[0].frames == 61);
    // レイヤーが 2 つ: 最初のものを読んで、警告。
    c.extraLayer = true;
    m = Read(c);
    CHECK(m && m->clips.size() == 1 && m->clips[0].tracks.size() == 2 && m->warnings.size() == 1 &&
          m->warnings[0].find("レイヤー") != std::string::npos);
}

RM_TEST(FbxSkin_FileWithoutDeformersIsUnchanged) {
    // テイクがあっても、スキンの無いファイルは骨もクリップも作らない (今までと全く同じメッシュ)。
    Character c;
    c.skin = false;
    c.take = true;
    const auto m = Read(c);
    c.take = false;
    const auto n = Read(c);
    CHECK(m && n && !m->skin && m->clips.empty() && m->warnings.empty());
    if (!m || !n) return;
    bool same = m->vertices.size() == n->vertices.size() && m->indices == n->indices && m->parts == n->parts;
    for (size_t i = 0; same && i < m->vertices.size(); ++i) {
        same = m->vertices[i].position == n->vertices[i].position && m->vertices[i].normal == n->vertices[i].normal && m->vertices[i].uv == n->vertices[i].uv;
    }
    CHECK(same);
}

RM_TEST(FbxSkin_MoreThanFourBonesAndUnweightedPoints) {
    // 根に 5 本の骨。点 0 は 5 本 (0.1 .. 0.5)、点 1 は骨 1 だけ、点 2 はウェイト無し (メッシュのモデルの骨へ)。
    std::string s = Header("");
    s += "Objects:  {\n\tGeometry: 1, \"Geometry::Tri\", \"Mesh\" {\n" + Arr("Vertices", {0, 0, 0, 100, 0, 0, 0, 100, 0}) +
         Arr("PolygonVertexIndex", {0, 1, -3}) + "\t}\n";
    s += Model(2, "Tri", "Mesh", "");
    s += "\tDeformer: 3, \"Deformer::\", \"Skin\" {\n\t}\n";
    for (int b = 1; b <= 5; ++b) {
        s += Model(10 + b, std::format("骨{}", b), "LimbNode", P3("Lcl Translation", {static_cast<float>(b), 0, 0}));
        s += std::format("\tDeformer: {}, \"SubDeformer::\", \"Cluster\" {{\n", 20 + b) + Arr("Indexes", b == 1 ? std::vector<double>{0, 1} : std::vector<double>{0}) +
             Arr("Weights", b == 1 ? std::vector<double>{0.1, 1} : std::vector<double>{0.1 * b}) + "\t}\n";
    }
    s += "}\nConnections:  {\n\tC: \"OO\",2,0\n\tC: \"OO\",1,2\n\tC: \"OO\",3,1\n";
    for (int b = 1; b <= 5; ++b) s += std::format("\tC: \"OO\",{},0\n\tC: \"OO\",{},3\n\tC: \"OO\",{},{}\n", 10 + b, 20 + b, 10 + b, 20 + b);
    s += "}\n";
    std::string error;
    const auto m = ParseFbx(Bytes(s), &error);
    CHECK(m.has_value() && m->skin.has_value());
    if (!m || !m->skin) return;
    const Skeleton& sk = m->skin->skeleton;
    CHECK(sk.bones.size() == 6 && sk.Find("骨1") >= 0 && sk.Find("骨5") >= 0 && sk.Find("Tri") >= 0);
    CHECK(m->warnings.size() == 1 && m->warnings[0].find("1 個") != std::string::npos);
    const SkinVertex& v0 = m->skin->vertices[0];
    int sum = 0;
    bool lightDropped = true;
    for (int k = 0; k < 4; ++k) {
        sum += v0.weight[k];
        if (v0.weight[k] > 0 && v0.bone[k] == sk.Find("骨1")) lightDropped = false;  // 0.1 の骨 1 は捨てた
    }
    CHECK(sum == 65535 && lightDropped && v0.bone[0] == sk.Find("骨5"));
    CHECK(m->skin->vertices[1].bone[0] == sk.Find("骨1") && m->skin->vertices[1].weight[0] == 65535);
    CHECK(m->skin->vertices[2].bone[0] == sk.Find("Tri") && m->skin->vertices[2].weight[0] == 65535);
    CHECK(SamePositions(Skinned(*m, ModelMatrices(sk, RestPose(sk))), *m));
}

RM_TEST(FbxSkin_TooManyBonesIsToldWhy) {
    std::string s = Header("");
    s += "Objects:  {\n\tGeometry: 1, \"Geometry::Tri\", \"Mesh\" {\n" + Arr("Vertices", {0, 0, 0, 1, 0, 0, 0, 1, 0}) + Arr("PolygonVertexIndex", {0, 1, -3}) +
         "\t}\n";
    s += Model(2, "Tri", "Mesh", "");
    s += "\tDeformer: 3, \"Deformer::\", \"Skin\" {\n\t}\n";
    std::string links = "}\nConnections:  {\n\tC: \"OO\",2,0\n\tC: \"OO\",1,2\n\tC: \"OO\",3,1\n";
    const int bones = static_cast<int>(kMaxBones) + 1;
    for (int b = 0; b < bones; ++b) {
        s += std::format("\tModel: {}, \"Model::b{}\", \"LimbNode\" {{\n\t}}\n", 10000 + b, b);
        s += std::format("\tDeformer: {}, \"SubDeformer::\", \"Cluster\" {{\n", 20000 + b) + Arr("Indexes", {0, 1, 2}) + Arr("Weights", {1, 1, 1}) + "\t}\n";
        links += std::format("\tC: \"OO\",{},0\n\tC: \"OO\",{},3\n\tC: \"OO\",{},{}\n", 10000 + b, 20000 + b, 10000 + b, 20000 + b);
    }
    std::string error;
    CHECK(!ParseFbx(Bytes(s + links + "}\n"), &error));
    CHECK(error.find("骨") != std::string::npos && error.find(std::to_string(kMaxBones)) != std::string::npos);
}
