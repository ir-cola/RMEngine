#include "rm_engine/space3d/runtime/MeshBake.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace rm::space3d {

namespace {

constexpr char kMagic[8] = {'R', 'M', 'M', 'E', 'S', 'H', 0, 0};

class Out {
public:
    std::string bytes;
    template <class T>
    void Put(T v) {
        char b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T));
        bytes.append(b, sizeof(T));
    }
    void Str(std::string_view s) {
        Put<uint32_t>(static_cast<uint32_t>(s.size()));
        bytes.append(s);
    }
    void Color4(const Color& c) {
        Put(c.r);
        Put(c.g);
        Put(c.b);
        Put(c.a);
    }
    void V3(Vec3 v) {
        Put(v.x);
        Put(v.y);
        Put(v.z);
    }
    void Q(Quat q) {
        Put(q.x);
        Put(q.y);
        Put(q.z);
        Put(q.w);
    }
    // 1 つのチャンク: タグ、サイズ、そして `fill` が書くもの。
    template <class F>
    void Chunk(const char (&tag)[5], F fill) {
        bytes.append(tag, 4);
        const size_t at = bytes.size();
        Put<uint64_t>(0);
        fill();
        const uint64_t size = bytes.size() - at - 8;
        std::memcpy(bytes.data() + at, &size, 8);
    }
};

class In {
public:
    explicit In(std::string_view b) : b_(b) {}
    template <class T>
    bool Get(T& v) {
        if (b_.size() < sizeof(T)) return false;
        std::memcpy(&v, b_.data(), sizeof(T));
        b_.remove_prefix(sizeof(T));
        return true;
    }
    bool Str(std::string& s) {
        uint32_t n = 0;
        if (!Get(n) || n > b_.size()) return false;
        s.assign(b_.substr(0, n));
        b_.remove_prefix(n);
        return true;
    }
    bool Color4(Color& c) { return Get(c.r) && Get(c.g) && Get(c.b) && Get(c.a); }
    bool V3(Vec3& v) { return Get(v.x) && Get(v.y) && Get(v.z); }
    bool Q(Quat& q) { return Get(q.x) && Get(q.y) && Get(q.z) && Get(q.w); }
    // 少なくとも 1 つ `each` バイトある項目の個数 (残りのバイトに収まる数を超えない)。
    bool Count(uint32_t& n, size_t each) { return Get(n) && static_cast<uint64_t>(n) * each <= b_.size(); }
    std::string_view Rest() const { return b_; }

private:
    std::string_view b_;
};

}  // namespace

std::string WriteBakedMesh(const MeshData& mesh, const std::vector<BakedPictures>& pictures) {
    Out o;
    o.bytes.assign(kMagic, kMagic + 8);
    o.Put<uint32_t>(kBakedMeshFormat);
    o.Chunk("BNDS", [&] {
        for (const float v : {mesh.min.x, mesh.min.y, mesh.min.z, mesh.max.x, mesh.max.y, mesh.max.z}) o.Put(v);
    });
    o.Chunk("VERT", [&] {
        o.Put<uint32_t>(static_cast<uint32_t>(mesh.vertices.size()));
        for (const MeshVertex& v : mesh.vertices) {
            for (const float f : {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y}) o.Put(f);
        }
    });
    o.Chunk("INDX", [&] {
        o.Put<uint32_t>(static_cast<uint32_t>(mesh.indices.size()));
        for (const uint32_t i : mesh.indices) o.Put(i);
    });
    o.Chunk("PART", [&] {
        o.Put<uint32_t>(static_cast<uint32_t>(mesh.parts.size()));
        for (const MeshPart& p : mesh.parts) {
            o.Put(p.firstIndex);
            o.Put(p.indexCount);
            o.Put<int32_t>(p.material);
        }
    });
    o.Chunk("MATL", [&] {
        o.Put<uint32_t>(static_cast<uint32_t>(mesh.materials.size()));
        for (size_t i = 0; i < mesh.materials.size(); ++i) {
            const MeshMaterial& m = mesh.materials[i];
            o.Str(m.name);
            o.Color4(m.baseColor);
            o.Put<uint8_t>(static_cast<uint8_t>(m.alpha));
            o.Put(m.alphaCutoff);
            o.Put(m.metallic);
            o.Put(m.roughness);
            o.Put(m.normalScale);
            o.Color4(m.emissive);
            for (size_t s = 0; s < 4; ++s) o.Str(i < pictures.size() ? pictures[i].path[s] : std::string());
        }
    });
    o.Chunk("SHRP", [&] {
        o.Put<uint32_t>(static_cast<uint32_t>(mesh.materials.size()));
        for (size_t i = 0; i < mesh.materials.size(); ++i) {
            for (size_t s = 0; s < 4; ++s) o.Put<uint8_t>(i < pictures.size() ? PackSampling(pictures[i].sampling[s]) : 0);
        }
    });
    if (mesh.skin) {
        const SkinData& skin = *mesh.skin;
        const size_t bones = skin.skeleton.bones.size();
        o.Chunk("SKEL", [&] {
            o.Put<uint32_t>(static_cast<uint32_t>(bones));
            for (const Bone& b : skin.skeleton.bones) {
                o.Str(b.name);
                o.Put<int32_t>(b.parent);
                o.V3(b.rest.t);
                o.Q(b.rest.r);
                o.V3(b.rest.s);
            }
        });
        o.Chunk("SKIN", [&] {
            o.Put<uint32_t>(static_cast<uint32_t>(skin.vertices.size()));
            for (const SkinVertex& v : skin.vertices) {
                for (const uint16_t b : v.bone) o.Put(b);
                for (const uint16_t w : v.weight) o.Put(w);
            }
        });
        o.Chunk("IBND", [&] {
            o.Put<uint32_t>(static_cast<uint32_t>(bones));
            for (size_t i = 0; i < bones; ++i) {
                const Mat4 m = i < skin.inverseBind.size() ? skin.inverseBind[i] : Mat4{};
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) o.Put(m.m[c][r]);
                }
            }
        });
        o.Chunk("BBOX", [&] {
            o.Put<uint32_t>(static_cast<uint32_t>(bones));
            for (size_t i = 0; i < bones; ++i) {
                o.V3(i < skin.boundsMin.size() ? skin.boundsMin[i] : Vec3{1e30f, 1e30f, 1e30f});
                o.V3(i < skin.boundsMax.size() ? skin.boundsMax[i] : Vec3{-1e30f, -1e30f, -1e30f});
            }
        });
    }
    if (!mesh.clips.empty()) {
        o.Chunk("ANIM", [&] {
            o.Put<uint32_t>(static_cast<uint32_t>(mesh.clips.size()));
            for (const Clip& clip : mesh.clips) {
                o.Str(clip.name);
                o.Put(clip.duration);
                o.Put(clip.rate);
                o.Put(clip.frames);
                o.Put<uint32_t>(static_cast<uint32_t>(clip.tracks.size()));
                for (const BoneTrack& t : clip.tracks) {
                    o.Str(t.bone);
                    o.Put<uint32_t>(static_cast<uint32_t>(t.t.size()));
                    for (const Vec3& v : t.t) o.V3(v);
                    o.Put<uint32_t>(static_cast<uint32_t>(t.r.size()));
                    for (const Quat& q : t.r) o.Q(q);
                    o.Put<uint32_t>(static_cast<uint32_t>(t.s.size()));
                    for (const Vec3& v : t.s) o.V3(v);
                }
            }
        });
        if (std::any_of(mesh.clips.begin(), mesh.clips.end(), [](const Clip& c) { return !c.events.empty(); })) {
            o.Chunk("EVNT", [&] {
                o.Put<uint32_t>(static_cast<uint32_t>(mesh.clips.size()));
                for (const Clip& clip : mesh.clips) {
                    o.Put<uint32_t>(static_cast<uint32_t>(clip.events.size()));
                    for (const ClipEvent& ev : clip.events) {
                        o.Put(ev.time);
                        o.Str(ev.name);
                        o.Put(ev.number);
                        o.Str(ev.text);
                    }
                }
            });
        }
    }
    return o.bytes;
}

std::optional<MeshData> ReadBakedMesh(std::string_view bytes, std::string* error, std::vector<TextureSampling>* samplingOfImage) {
    auto fail = [&](std::string msg) -> std::optional<MeshData> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), kMagic, 8) != 0) return fail("焼いたモデルではありません");
    In in(bytes.substr(8));
    uint32_t format = 0;
    in.Get(format);
    if (format > kBakedMeshFormat) return fail(std::format("新しい版で焼いたモデルです (形式 {} / この版は {} まで)", format, kBakedMeshFormat));
    MeshData m;
    std::vector<std::array<uint8_t, 4>> sharp;  // マテリアルごと (SHRP)
    // 骨 (SKEL SKIN IBND BBOX): 全部読んでから確かめて、MeshData::skin にする。
    std::optional<Skeleton> skel;
    std::optional<std::vector<SkinVertex>> skinVertices;
    std::optional<std::vector<Mat4>> inverseBind;
    std::vector<Vec3> boundsMin, boundsMax;
    std::string_view rest = in.Rest();
    while (!rest.empty()) {
        if (rest.size() < 12) return fail("焼いたモデルが壊れています (途中で切れています)");
        const std::string_view tag = rest.substr(0, 4);
        uint64_t size = 0;
        std::memcpy(&size, rest.data() + 4, 8);
        if (size > rest.size() - 12) return fail("焼いたモデルが壊れています (途中で切れています)");
        In c(rest.substr(12, static_cast<size_t>(size)));
        rest.remove_prefix(12 + static_cast<size_t>(size));
        bool ok = true;
        uint32_t n = 0;
        if (tag == "BNDS") {
            ok = c.Get(m.min.x) && c.Get(m.min.y) && c.Get(m.min.z) && c.Get(m.max.x) && c.Get(m.max.y) && c.Get(m.max.z);
        } else if (tag == "VERT") {
            ok = c.Count(n, 32);
            m.vertices.resize(ok ? n : 0);
            for (MeshVertex& v : m.vertices) {
                ok = ok && c.Get(v.position.x) && c.Get(v.position.y) && c.Get(v.position.z) && c.Get(v.normal.x) && c.Get(v.normal.y) &&
                     c.Get(v.normal.z) && c.Get(v.uv.x) && c.Get(v.uv.y);
            }
        } else if (tag == "INDX") {
            ok = c.Count(n, 4);
            m.indices.resize(ok ? n : 0);
            for (uint32_t& i : m.indices) ok = ok && c.Get(i);
        } else if (tag == "PART") {
            ok = c.Count(n, 12);
            m.parts.resize(ok ? n : 0);
            for (MeshPart& p : m.parts) {
                int32_t mat = -1;
                ok = ok && c.Get(p.firstIndex) && c.Get(p.indexCount) && c.Get(mat);
                p.material = mat;
            }
        } else if (tag == "MATL") {
            ok = c.Count(n, 4);
            m.materials.resize(ok ? n : 0);
            for (MeshMaterial& mat : m.materials) {
                uint8_t alpha = 0;
                ok = ok && c.Str(mat.name) && c.Color4(mat.baseColor) && c.Get(alpha) && c.Get(mat.alphaCutoff) && c.Get(mat.metallic) &&
                     c.Get(mat.roughness) && c.Get(mat.normalScale) && c.Color4(mat.emissive);
                mat.alpha = alpha <= static_cast<uint8_t>(AlphaMode::Blend) ? static_cast<AlphaMode>(alpha) : AlphaMode::Opaque;
                int* slots[4] = {&mat.image, &mat.metallicRoughnessImage, &mat.normalImage, &mat.emissiveImage};
                for (int* slot : slots) {
                    std::string path;
                    ok = ok && c.Str(path);
                    *slot = -1;
                    if (!ok || path.empty()) continue;
                    // パスごとに画像 1 つ。
                    int found = -1;
                    for (size_t i = 0; i < m.images.size(); ++i) {
                        if (m.images[i].file == path) found = static_cast<int>(i);
                    }
                    if (found < 0) {
                        found = static_cast<int>(m.images.size());
                        m.images.push_back({path, {}, path});
                    }
                    *slot = found;
                }
            }
        } else if (tag == "SHRP") {
            ok = c.Count(n, 4);
            sharp.resize(ok ? n : 0);
            for (auto& sm : sharp) {
                for (uint8_t& b : sm) ok = ok && c.Get(b);
            }
        } else if (tag == "SKEL") {
            ok = c.Count(n, 4 + 4 + 40) && n <= kMaxBones;
            skel.emplace();
            skel->bones.resize(ok ? n : 0);
            for (Bone& b : skel->bones) {
                int32_t parent = -1;
                ok = ok && c.Str(b.name) && c.Get(parent) && c.V3(b.rest.t) && c.Q(b.rest.r) && c.V3(b.rest.s);
                b.parent = parent;
            }
        } else if (tag == "SKIN") {
            ok = c.Count(n, 16);
            skinVertices.emplace(ok ? n : 0);
            for (SkinVertex& v : *skinVertices) {
                for (uint16_t& b : v.bone) ok = ok && c.Get(b);
                for (uint16_t& w : v.weight) ok = ok && c.Get(w);
            }
        } else if (tag == "IBND") {
            ok = c.Count(n, 64);
            inverseBind.emplace(ok ? n : 0);
            for (Mat4& mat : *inverseBind) {
                for (int col = 0; col < 4; ++col) {
                    for (int r = 0; r < 4; ++r) ok = ok && c.Get(mat.m[col][r]);
                }
            }
        } else if (tag == "BBOX") {
            ok = c.Count(n, 24);
            boundsMin.resize(ok ? n : 0);
            boundsMax.resize(ok ? n : 0);
            for (size_t i = 0; ok && i < n; ++i) ok = c.V3(boundsMin[i]) && c.V3(boundsMax[i]);
        } else if (tag == "ANIM") {
            ok = c.Count(n, 4 + 12 + 4);
            m.clips.resize(ok ? n : 0);
            for (Clip& clip : m.clips) {
                uint32_t tracks = 0;
                ok = ok && c.Str(clip.name) && c.Get(clip.duration) && c.Get(clip.rate) && c.Get(clip.frames) && c.Count(tracks, 4 + 12);
                ok = ok && clip.frames >= 1 && clip.rate > 0 && clip.rate <= kMaxClipRate && clip.duration >= 0;
                clip.tracks.resize(ok ? tracks : 0);
                for (BoneTrack& t : clip.tracks) {
                    // 各列の数は 0・1・frames のどれか。
                    auto count = [&](uint32_t& k, size_t each) { return c.Count(k, each) && (k <= 1 || k == clip.frames); };
                    uint32_t k = 0;
                    ok = ok && c.Str(t.bone) && count(k, 12);
                    t.t.resize(ok ? k : 0);
                    for (Vec3& v : t.t) ok = ok && c.V3(v);
                    ok = ok && count(k, 16);
                    t.r.resize(ok ? k : 0);
                    for (Quat& q : t.r) ok = ok && c.Q(q);
                    ok = ok && count(k, 12);
                    t.s.resize(ok ? k : 0);
                    for (Vec3& v : t.s) ok = ok && c.V3(v);
                }
            }
        } else if (tag == "EVNT") {
            // (ANIM の後に書く。クリップの数が合わなければ壊れている)
            ok = c.Count(n, 4) && n == m.clips.size();
            for (size_t k = 0; ok && k < n; ++k) {
                uint32_t events = 0;
                ok = c.Count(events, 4 + 4 + 8 + 4);
                m.clips[k].events.resize(ok ? events : 0);
                for (ClipEvent& ev : m.clips[k].events) ok = ok && c.Get(ev.time) && c.Str(ev.name) && c.Get(ev.number) && c.Str(ev.text);
            }
        }  // (このバージョンが知らないタグ: 飛ばす)
        if (!ok) return fail(std::format("焼いたモデルの {} が壊れています", std::string(tag)));
    }
    if (m.parts.empty() && !m.indices.empty()) m.parts.push_back({0, static_cast<uint32_t>(m.indices.size()), -1});
    std::string why;
    if (!IndicesInRange(m, &why)) return fail("焼いたモデルが壊れています: " + why);
    for (const MeshPart& p : m.parts) {
        if (static_cast<uint64_t>(p.firstIndex) + p.indexCount > m.indices.size() || p.material >= static_cast<int>(m.materials.size())) {
            return fail("焼いたモデルの部分が壊れています");
        }
    }
    if (skel || skinVertices || inverseBind) {
        if (!skel || !skinVertices || !inverseBind) return fail("焼いたモデルの骨が壊れています (SKEL / SKIN / IBND のどれかがありません)");
        const size_t bones = skel->bones.size();
        if (!ParentsFirst(*skel) || inverseBind->size() != bones || skinVertices->size() != m.vertices.size()) {
            return fail("焼いたモデルの骨が壊れています (親の番号か、数が合いません)");
        }
        for (const SkinVertex& v : *skinVertices) {
            for (size_t k = 0; k < 4; ++k) {
                if (v.weight[k] > 0 && v.bone[k] >= bones) return fail("焼いたモデルの骨が壊れています (頂点が無い骨を指しています)");
            }
        }
        SkinData skin;
        skin.skeleton = std::move(*skel);
        skin.vertices = std::move(*skinVertices);
        skin.inverseBind = std::move(*inverseBind);
        if (boundsMin.size() == bones) {
            skin.boundsMin = std::move(boundsMin);
            skin.boundsMax = std::move(boundsMax);
        } else {
            std::vector<Vec3> positions(m.vertices.size());
            for (size_t i = 0; i < positions.size(); ++i) positions[i] = m.vertices[i].position;
            ComputeBoneBounds(skin, positions);
        }
        m.skin = std::move(skin);
    }
    if (samplingOfImage) {
        samplingOfImage->assign(m.images.size(), TextureSampling{});
        for (size_t i = 0; i < m.materials.size() && i < sharp.size(); ++i) {
            const MeshMaterial& mat = m.materials[i];
            const int slots[4] = {mat.image, mat.metallicRoughnessImage, mat.normalImage, mat.emissiveImage};
            for (size_t s = 0; s < 4; ++s) {
                if (slots[s] >= 0 && sharp[i][s]) (*samplingOfImage)[static_cast<size_t>(slots[s])] = UnpackSampling(sharp[i][s]);
            }
        }
    }
    return m;
}

}  // namespace rm::space3d
