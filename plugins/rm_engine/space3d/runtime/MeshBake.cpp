#include "rm_engine/space3d/runtime/MeshBake.h"

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
    // One chunk: its tag, its size, then what `fill` writes.
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
    // A count of items of at least `each` bytes (never more than the bytes left could hold).
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
    std::vector<std::array<uint8_t, 4>> sharp;  // per material (SHRP)
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
                    // One image per path.
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
        }  // (a tag this version does not know: skipped)
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
