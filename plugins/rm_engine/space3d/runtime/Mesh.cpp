#include "rm_engine/space3d/runtime/Mesh.h"

#include "base/Json.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_map>

namespace rm::space3d {

namespace {

// More than this many vertices or indices in one accessor / file is taken as a broken file (an
// accessor without data can ask for any count, and nothing would stop it filling the memory).
constexpr size_t kMaxCount = 50'000'000;
constexpr int kMaxNodeDepth = 256;

bool Fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

char Lower(char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; }

bool SameIgnoringCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (Lower(a[i]) != Lower(b[i])) return false;
    }
    return true;
}

// --- glTF -------------------------------------------------------------------------------------

constexpr uint32_t kGlbMagic = 0x46546C67;  // "glTF"
constexpr uint32_t kChunkJson = 0x4E4F534A;  // "JSON"
constexpr uint32_t kChunkBin = 0x004E4942;   // "BIN\0"

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;  // little-endian, as Windows on x64 / ARM64
}

int ComponentSize(int64_t componentType) {
    switch (componentType) {
        case 5120: case 5121: return 1;  // byte, unsigned byte
        case 5122: case 5123: return 2;  // short, unsigned short
        case 5125: case 5126: return 4;  // unsigned int, float
        default: return 0;
    }
}

int ComponentCount(std::string_view type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    return 0;  // matrices: never used for what is read here
}

// Where the numbers of one accessor are (base null: all zero, an accessor without a buffer view).
struct Accessor {
    const uint8_t* base = nullptr;
    size_t stride = 0;
    size_t count = 0;
    int components = 0;
    int64_t componentType = 5126;
    bool normalized = false;

    float Float(size_t i, int c) const {
        if (!base) return 0.f;
        const uint8_t* p = base + i * stride + static_cast<size_t>(c) * ComponentSize(componentType);
        switch (componentType) {
            case 5126: { float v; std::memcpy(&v, p, 4); return v; }
            case 5120: { int8_t v; std::memcpy(&v, p, 1); return normalized ? std::max(v / 127.f, -1.f) : v; }
            case 5121: { uint8_t v = *p; return normalized ? v / 255.f : v; }
            case 5122: { int16_t v; std::memcpy(&v, p, 2); return normalized ? std::max(v / 32767.f, -1.f) : v; }
            case 5123: { uint16_t v; std::memcpy(&v, p, 2); return normalized ? v / 65535.f : v; }
            case 5125: { uint32_t v; std::memcpy(&v, p, 4); return static_cast<float>(v); }
            default: return 0.f;
        }
    }
    uint32_t Index(size_t i) const {
        if (!base) return 0;
        const uint8_t* p = base + i * stride;
        switch (componentType) {
            case 5121: return *p;
            case 5123: { uint16_t v; std::memcpy(&v, p, 2); return v; }
            case 5125: { uint32_t v; std::memcpy(&v, p, 4); return v; }
            default: return 0;
        }
    }
};

Mat4 FromQuaternion(float x, float y, float z, float w) {
    const float len = std::sqrt(x * x + y * y + z * z + w * w);
    if (len > 0) x /= len, y /= len, z /= len, w /= len;
    Mat4 r;
    r.m[0][0] = 1 - 2 * (y * y + z * z), r.m[0][1] = 2 * (x * y + z * w), r.m[0][2] = 2 * (x * z - y * w);
    r.m[1][0] = 2 * (x * y - z * w), r.m[1][1] = 1 - 2 * (x * x + z * z), r.m[1][2] = 2 * (y * z + x * w);
    r.m[2][0] = 2 * (x * z + y * w), r.m[2][1] = 2 * (y * z - x * w), r.m[2][2] = 1 - 2 * (x * x + y * y);
    return r;
}

// arr[i] as a number (scalar: the value itself), or def.
float Number(const json::Value& arr, size_t i, float def, bool scalar = false) {
    if (scalar) return arr.IsNumber() ? static_cast<float>(arr.AsDouble()) : def;
    return arr.IsArray() && i < arr.Size() && arr.At(i).IsNumber() ? static_cast<float>(arr.At(i).AsDouble()) : def;
}

float Unit(float v) { return std::clamp(v, 0.f, 1.f); }

class GltfReader {
public:
    GltfReader(const json::Value& doc, std::span<const uint8_t> glbBin, const ReadRelated& read, std::string* error)
        : doc_(doc), glbBin_(glbBin), read_(read), error_(error) {}

    std::optional<MeshData> Read() {
        const json::Value& asset = doc_.Get("asset");
        const std::string& version = asset.Get("version").AsString();
        if (!version.empty() && version[0] != '2') {
            Fail(error_, std::format("glTF {} には対応していません (2.0 だけ読めます)", version));
            return std::nullopt;
        }
        if (!LoadBuffers() || !LoadMaterials()) return std::nullopt;

        const json::Value& scenes = doc_.Get("scenes");
        const json::Value& nodes = doc_.Get("nodes");
        if (scenes.IsArray() && scenes.Size() > 0) {
            const int64_t which = doc_.Get("scene").AsInt(0);
            if (which < 0 || static_cast<size_t>(which) >= scenes.Size()) {
                Fail(error_, std::format("シーン {} がありません", which));
                return std::nullopt;
            }
            for (const json::Value& n : scenes.At(static_cast<size_t>(which)).Get("nodes").Items()) {
                if (!Node(n.AsInt(-1), Mat4{}, 0)) return std::nullopt;
            }
        } else if (nodes.IsArray() && nodes.Size() > 0) {
            // No scene: every node that is nobody's child.
            std::vector<bool> child(nodes.Size(), false);
            for (const json::Value& n : nodes.Items()) {
                for (const json::Value& c : n.Get("children").Items()) {
                    const int64_t k = c.AsInt(-1);
                    if (k >= 0 && static_cast<size_t>(k) < child.size()) child[static_cast<size_t>(k)] = true;
                }
            }
            for (size_t i = 0; i < nodes.Size(); ++i) {
                if (!child[i] && !Node(static_cast<int64_t>(i), Mat4{}, 0)) return std::nullopt;
            }
        } else {
            // Not even nodes: the meshes as they are.
            for (size_t i = 0; i < doc_.Get("meshes").Size(); ++i) {
                if (!Mesh(static_cast<int64_t>(i), Mat4{})) return std::nullopt;
            }
        }
        if (out_.indices.empty()) {
            Fail(error_, skipped_ > 0 ? "三角形の面がありません (点や線だけのモデルは描けません)" : "メッシュがありません");
            return std::nullopt;
        }
        ComputeBounds(out_);
        SortByMaterial(out_, triangleMaterial_);
        return std::move(out_);
    }

private:
    bool LoadBuffers() {
        const json::Value& buffers = doc_.Get("buffers");
        for (size_t i = 0; i < buffers.Size(); ++i) {
            const json::Value& b = buffers.At(i);
            const size_t length = static_cast<size_t>(std::max<int64_t>(b.Get("byteLength").AsInt(0), 0));
            const json::Value* uri = b.Find("uri");
            std::vector<uint8_t> data;
            if (!uri) {
                if (i != 0 || glbBin_.empty()) return Fail(error_, std::format("バッファー {} の中身がありません", i));
                data.assign(glbBin_.begin(), glbBin_.end());
            } else {
                const std::string& u = uri->AsString();
                if (u.starts_with("data:")) {
                    const size_t comma = u.find(',');
                    if (comma == std::string::npos || u.substr(0, comma).find(";base64") == std::string::npos) {
                        return Fail(error_, std::format("バッファー {} の data: URI が base64 ではありません", i));
                    }
                    auto decoded = DecodeBase64(std::string_view(u).substr(comma + 1));
                    if (!decoded) return Fail(error_, std::format("バッファー {} の base64 が壊れています", i));
                    data = std::move(*decoded);
                } else {
                    const std::string name = DecodePercent(u);
                    auto file = read_ ? read_(name) : std::nullopt;
                    if (!file) return Fail(error_, std::format("バッファーのファイル「{}」を読めませんでした", name));
                    data = std::move(*file);
                }
            }
            if (data.size() < length) {
                return Fail(error_, std::format("バッファー {} が短すぎます ({} バイトのはずが {} バイト)", i, length, data.size()));
            }
            buffers_.push_back(std::move(data));
        }
        return true;
    }

    // Every material's colour and picture (pbrMetallicRoughness: baseColorFactor x baseColorTexture).
    bool LoadMaterials() {
        const json::Value& materials = doc_.Get("materials");
        for (size_t i = 0; i < materials.Size(); ++i) {
            const json::Value& m = materials.At(i);
            MeshMaterial mat;
            mat.name = m.Get("name").AsString();
            if (mat.name.empty()) mat.name = std::format("材質 {}", i);
            const json::Value& pbr = m.Get("pbrMetallicRoughness");
            const json::Value& f = pbr.Get("baseColorFactor");
            // Linear light, as glTF writes it (the renderer works in linear light).
            mat.baseColor = {Unit(Number(f, 0, 1)), Unit(Number(f, 1, 1)), Unit(Number(f, 2, 1)), Unit(Number(f, 3, 1))};
            // glTF's defaults: a rough metal (most files write both).
            mat.metallic = Unit(Number(pbr.Get("metallicFactor"), 0, 1.f, true));
            mat.roughness = Unit(Number(pbr.Get("roughnessFactor"), 0, 1.f, true));
            const json::Value& e = m.Get("emissiveFactor");
            // KHR_materials_emissive_strength: brighter than 1.
            const float strength = std::max(0.f, Number(m.Get("extensions").Get("KHR_materials_emissive_strength").Get("emissiveStrength"), 0, 1.f, true));
            mat.emissive = {std::max(0.f, Number(e, 0, 0)) * strength, std::max(0.f, Number(e, 1, 0)) * strength,
                            std::max(0.f, Number(e, 2, 0)) * strength, 1};
            const std::string& mode = m.Get("alphaMode").AsString();
            mat.alpha = mode == "MASK" ? AlphaMode::Mask : mode == "BLEND" ? AlphaMode::Blend : AlphaMode::Opaque;
            if (const json::Value* cut = m.Find("alphaCutoff"); cut && cut->IsNumber()) mat.alphaCutoff = static_cast<float>(cut->AsDouble());
            if (!TextureImage(pbr.Find("baseColorTexture"), mat.image) || !TextureImage(pbr.Find("metallicRoughnessTexture"), mat.metallicRoughnessImage) ||
                !TextureImage(m.Find("normalTexture"), mat.normalImage) || !TextureImage(m.Find("emissiveTexture"), mat.emissiveImage)) {
                return false;
            }
            if (const json::Value* n = m.Find("normalTexture")) mat.normalScale = Number(n->Get("scale"), 0, 1.f, true);
            out_.materials.push_back(std::move(mat));
        }
        return true;
    }

    static constexpr int kBroken = -2;

    // A material's textureInfo ({"index": texture}) -> one of out_.images in `out` (left as it is when
    // there is none, or the texture has no picture). False: told why.
    bool TextureImage(const json::Value* info, int& out) {
        if (!info) return true;
        const int64_t t = info->Get("index").AsInt(-1);
        const json::Value& textures = doc_.Get("textures");
        if (t < 0 || static_cast<size_t>(t) >= textures.Size()) return Fail(error_, std::format("テクスチャ {} がありません", t));
        if (const json::Value* source = textures.At(static_cast<size_t>(t)).Find("source")) {
            const int image = Image(source->AsInt(-1));
            if (image == kBroken) return false;
            out = image;
        }
        return true;
    }

    // glTF image `index` as one of out_.images (each read once); -1: it has no picture; kBroken: told why.
    int Image(int64_t index) {
        const json::Value& images = doc_.Get("images");
        if (index < 0 || static_cast<size_t>(index) >= images.Size()) {
            Fail(error_, std::format("画像 {} がありません", index));
            return kBroken;
        }
        if (imageOf_.size() < images.Size()) imageOf_.resize(images.Size(), kBroken);
        int& known = imageOf_[static_cast<size_t>(index)];
        if (known != kBroken) return known;
        const json::Value& im = images.At(static_cast<size_t>(index));
        MeshImage out;
        out.name = im.Get("name").AsString();
        if (const json::Value* uri = im.Find("uri")) {
            const std::string& u = uri->AsString();
            if (u.starts_with("data:")) {
                const size_t comma = u.find(',');
                auto decoded = comma == std::string::npos || u.substr(0, comma).find(";base64") == std::string::npos
                                   ? std::nullopt
                                   : DecodeBase64(std::string_view(u).substr(comma + 1));
                if (!decoded) {
                    Fail(error_, std::format("画像 {} の data: URI が読めません (base64 ではないか、壊れています)", index));
                    return kBroken;
                }
                out.bytes = std::move(*decoded);
            } else {
                out.file = DecodePercent(u);
                if (out.name.empty()) out.name = out.file;
            }
        } else if (const json::Value* view = im.Find("bufferView")) {
            const auto bytes = View(view->AsInt(-1));
            if (!bytes) return kBroken;
            out.bytes.assign(bytes->begin(), bytes->end());
        } else {
            known = -1;  // nothing to paint with
            return known;
        }
        if (out.name.empty()) out.name = std::format("画像 {}", index);
        known = static_cast<int>(out_.images.size());
        out_.images.push_back(std::move(out));
        return known;
    }

    // The bytes of a buffer view, checked to lie inside its buffer.
    std::optional<std::span<const uint8_t>> View(int64_t vi) {
        const json::Value& views = doc_.Get("bufferViews");
        if (vi < 0 || static_cast<size_t>(vi) >= views.Size()) {
            Fail(error_, std::format("バッファービュー {} がありません", vi));
            return std::nullopt;
        }
        const json::Value& v = views.At(static_cast<size_t>(vi));
        const int64_t bi = v.Get("buffer").AsInt(-1);
        if (bi < 0 || static_cast<size_t>(bi) >= buffers_.size()) {
            Fail(error_, std::format("バッファー {} がありません", bi));
            return std::nullopt;
        }
        const std::vector<uint8_t>& buffer = buffers_[static_cast<size_t>(bi)];
        const int64_t offset = v.Get("byteOffset").AsInt(0), length = v.Get("byteLength").AsInt(-1);
        if (offset < 0 || length < 0 || static_cast<uint64_t>(offset) + static_cast<uint64_t>(length) > buffer.size()) {
            Fail(error_, std::format("バッファービュー {} がバッファーからはみ出しています", vi));
            return std::nullopt;
        }
        return std::span<const uint8_t>(buffer.data() + offset, static_cast<size_t>(length));
    }

    bool Node(int64_t index, const Mat4& parent, int depth) {
        const json::Value& nodes = doc_.Get("nodes");
        if (index < 0 || static_cast<size_t>(index) >= nodes.Size()) return Fail(error_, std::format("ノード {} がありません", index));
        if (depth > kMaxNodeDepth) return Fail(error_, "ノードの親子が深すぎるか、輪になっています");
        const json::Value& n = nodes.At(static_cast<size_t>(index));
        Mat4 local;
        if (const json::Value* m = n.Find("matrix"); m && m->Size() == 16) {
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) local.m[c][r] = Number(*m, static_cast<size_t>(c * 4 + r), c == r ? 1.f : 0.f);
            }
        } else {
            const json::Value& t = n.Get("translation");
            const json::Value& q = n.Get("rotation");
            const json::Value& s = n.Get("scale");
            Mat4 tm, sm;
            tm.m[3][0] = Number(t, 0, 0), tm.m[3][1] = Number(t, 1, 0), tm.m[3][2] = Number(t, 2, 0);
            sm.m[0][0] = Number(s, 0, 1), sm.m[1][1] = Number(s, 1, 1), sm.m[2][2] = Number(s, 2, 1);
            local = tm * FromQuaternion(Number(q, 0, 0), Number(q, 1, 0), Number(q, 2, 0), Number(q, 3, 1)) * sm;
        }
        const Mat4 world = parent * local;
        if (const json::Value* mesh = n.Find("mesh")) {
            if (!Mesh(mesh->AsInt(-1), world)) return false;
        }
        for (const json::Value& c : n.Get("children").Items()) {
            if (!Node(c.AsInt(-1), world, depth + 1)) return false;
        }
        return true;
    }

    bool Mesh(int64_t index, const Mat4& world) {
        const json::Value& meshes = doc_.Get("meshes");
        if (index < 0 || static_cast<size_t>(index) >= meshes.Size()) return Fail(error_, std::format("メッシュ {} がありません", index));
        for (const json::Value& prim : meshes.At(static_cast<size_t>(index)).Get("primitives").Items()) {
            if (!Primitive(prim, world)) return false;
        }
        return true;
    }

    bool Primitive(const json::Value& prim, const Mat4& world) {
        const int64_t mode = prim.Get("mode").AsInt(4);
        if (mode != 4 && mode != 5 && mode != 6) {
            ++skipped_;  // points and lines: nothing to fill
            return true;
        }
        int material = -1;
        if (const json::Value* m = prim.Find("material")) {
            const int64_t k = m->AsInt(-1);
            if (k < 0 || static_cast<size_t>(k) >= out_.materials.size()) return Fail(error_, std::format("材質 {} がありません", k));
            material = static_cast<int>(k);
        }
        const json::Value& attrs = prim.Get("attributes");
        const json::Value* posIndex = attrs.Find("POSITION");
        if (!posIndex) return Fail(error_, "頂点の位置 (POSITION) のないプリミティブがあります");
        Accessor pos;
        if (!Open(posIndex->AsInt(-1), 3, pos)) return false;
        MeshData part;
        part.vertices.resize(pos.count);
        for (size_t i = 0; i < pos.count; ++i) part.vertices[i].position = {pos.Float(i, 0), pos.Float(i, 1), pos.Float(i, 2)};
        bool hasNormals = false;
        if (const json::Value* n = attrs.Find("NORMAL")) {
            Accessor nrm;
            if (!Open(n->AsInt(-1), 3, nrm)) return false;
            if (nrm.count < pos.count) return Fail(error_, "法線 (NORMAL) の数が頂点より少なすぎます");
            for (size_t i = 0; i < pos.count; ++i) part.vertices[i].normal = {nrm.Float(i, 0), nrm.Float(i, 1), nrm.Float(i, 2)};
            hasNormals = true;
        }
        if (const json::Value* t = attrs.Find("TEXCOORD_0")) {
            Accessor uv;
            if (!Open(t->AsInt(-1), 2, uv)) return false;
            if (uv.count < pos.count) return Fail(error_, "UV (TEXCOORD_0) の数が頂点より少なすぎます");
            for (size_t i = 0; i < pos.count; ++i) part.vertices[i].uv = {uv.Float(i, 0), uv.Float(i, 1)};
        }

        // The corners in order, then cut into triangles by the mode.
        std::vector<uint32_t> corners;
        if (const json::Value* ind = prim.Find("indices")) {
            Accessor ix;
            if (!Open(ind->AsInt(-1), 1, ix)) return false;
            if (ix.componentType != 5121 && ix.componentType != 5123 && ix.componentType != 5125) {
                return Fail(error_, "インデックスの型が正しくありません");
            }
            corners.resize(ix.count);
            for (size_t i = 0; i < ix.count; ++i) {
                corners[i] = ix.Index(i);
                if (corners[i] >= pos.count) return Fail(error_, std::format("インデックス {} が頂点の数 ({}) を超えています", corners[i], pos.count));
            }
        } else {
            corners.resize(pos.count);
            for (size_t i = 0; i < pos.count; ++i) corners[i] = static_cast<uint32_t>(i);
        }
        if (mode == 4) {
            corners.resize(corners.size() / 3 * 3);
            part.indices = std::move(corners);
        } else {
            for (size_t i = 2; i < corners.size(); ++i) {
                if (mode == 5) {  // strip: every other triangle turns the other way
                    const bool odd = (i % 2) == 1;
                    part.indices.insert(part.indices.end(), {corners[i - 2], odd ? corners[i] : corners[i - 1], odd ? corners[i - 1] : corners[i]});
                } else {  // fan
                    part.indices.insert(part.indices.end(), {corners[0], corners[i - 1], corners[i]});
                }
            }
        }
        if (!hasNormals) ComputeNormals(part);
        Append(part, world);
        triangleMaterial_.resize(out_.indices.size() / 3, material);
        return true;
    }

    // The node's place is put into the vertices (one mesh in the model's space). A mirroring place
    // turns the triangles over, so they are turned back (the front stays counter-clockwise).
    void Append(const MeshData& part, const Mat4& world) {
        const auto inv = Inverse(world);
        const Mat4 normalM = inv ? *inv : Mat4{};
        const Vec3 x = TransformDirection(world, {1, 0, 0}), y = TransformDirection(world, {0, 1, 0}), z = TransformDirection(world, {0, 0, 1});
        const bool mirrored = Dot(Cross(x, y), z) < 0;
        const uint32_t base = static_cast<uint32_t>(out_.vertices.size());
        for (MeshVertex v : part.vertices) {
            v.position = TransformPoint(world, v.position);
            // Normals go by the inverse transpose (stays at right angles to a surface scaled unevenly).
            const Vec3 n = v.normal;
            v.normal = Normalize({normalM.m[0][0] * n.x + normalM.m[0][1] * n.y + normalM.m[0][2] * n.z,
                                  normalM.m[1][0] * n.x + normalM.m[1][1] * n.y + normalM.m[1][2] * n.z,
                                  normalM.m[2][0] * n.x + normalM.m[2][1] * n.y + normalM.m[2][2] * n.z});
            out_.vertices.push_back(v);
        }
        for (size_t i = 0; i + 2 < part.indices.size(); i += 3) {
            const uint32_t a = part.indices[i], b = part.indices[i + 1], c = part.indices[i + 2];
            out_.indices.insert(out_.indices.end(), {base + a, base + (mirrored ? c : b), base + (mirrored ? b : c)});
        }
    }

    // An accessor's numbers, checked to lie inside its buffer.
    bool Open(int64_t index, int wantComponents, Accessor& out) {
        const json::Value& accessors = doc_.Get("accessors");
        if (index < 0 || static_cast<size_t>(index) >= accessors.Size()) return Fail(error_, std::format("アクセサー {} がありません", index));
        const json::Value& a = accessors.At(static_cast<size_t>(index));
        if (a.Find("sparse")) return Fail(error_, "疎なアクセサー (sparse) にはまだ対応していません");
        out.componentType = a.Get("componentType").AsInt(0);
        out.components = ComponentCount(a.Get("type").AsString());
        out.normalized = a.Get("normalized").AsBool(false);
        const int64_t count = a.Get("count").AsInt(-1);
        const int size = ComponentSize(out.componentType);
        if (size == 0 || out.components == 0 || count < 0 || static_cast<size_t>(count) > kMaxCount) {
            return Fail(error_, std::format("アクセサー {} の型か数が正しくありません", index));
        }
        if (out.components < wantComponents) return Fail(error_, std::format("アクセサー {} の成分が足りません", index));
        out.count = static_cast<size_t>(count);
        const size_t element = static_cast<size_t>(size) * static_cast<size_t>(out.components);
        const json::Value* viewIndex = a.Find("bufferView");
        if (!viewIndex) return true;  // all zero
        const int64_t vi = viewIndex->AsInt(-1);
        const auto view = View(vi);
        if (!view) return false;
        const int64_t stride = doc_.Get("bufferViews").At(static_cast<size_t>(vi)).Get("byteStride").AsInt(0);
        const int64_t offset = a.Get("byteOffset").AsInt(0);
        if (offset < 0 || stride < 0) return Fail(error_, std::format("アクセサー {} がバッファービューからはみ出しています", index));
        out.stride = stride > 0 ? static_cast<size_t>(stride) : element;
        if (out.count > 0) {
            const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(out.count - 1) * out.stride + element;
            if (end > view->size()) return Fail(error_, std::format("アクセサー {} がバッファービューからはみ出しています", index));
        }
        out.base = view->data() + offset;
        return true;
    }

    const json::Value& doc_;
    std::span<const uint8_t> glbBin_;
    const ReadRelated& read_;
    std::string* error_;
    std::vector<std::vector<uint8_t>> buffers_;
    MeshData out_;
    std::vector<int> triangleMaterial_;  // one per triangle of out_
    std::vector<int> imageOf_;           // glTF image -> out_.images (kBroken: not yet)
    int skipped_ = 0;
};

// --- OBJ --------------------------------------------------------------------------------------

std::string_view NextToken(std::string_view& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
    const std::string_view t = s.substr(i, j - i);
    s.remove_prefix(j);
    return t;
}

bool ParseFloat(std::string_view t, float& out) {
    if (!t.empty() && t[0] == '+') t.remove_prefix(1);
    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), out);
    return ec == std::errc{} && end == t.data() + t.size();
}

bool ParseInt(std::string_view t, int64_t& out) {
    if (t.empty()) return false;
    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), out);
    return ec == std::errc{} && end == t.data() + t.size();
}

// The rest of the line without the spaces around it (names with spaces: "newmtl red brick").
std::string_view Rest(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// A path written in a model's text file, with '/' between folders (Windows tools write '\').
std::string Slashes(std::string_view s) {
    std::string out(s);
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

// The folder part of a relative path, with its '/' ("mats/a.mtl" -> "mats/"; "a.mtl" -> "").
std::string_view FolderOf(std::string_view path) {
    const size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash + 1);
}

struct Corner {
    int64_t v = 0, t = -1, n = -1;  // 0-based, -1: none
    bool operator==(const Corner&) const = default;
};
struct CornerHash {
    size_t operator()(const Corner& c) const {
        return std::hash<int64_t>()(c.v) ^ (std::hash<int64_t>()(c.t) * 31) ^ (std::hash<int64_t>()(c.n) * 1000003);
    }
};

}  // namespace

std::optional<MeshData> ParseGltf(std::span<const uint8_t> bytes, const ReadRelated& read, std::string* error) {
    std::string_view jsonText;
    std::span<const uint8_t> bin;
    if (bytes.size() >= 4 && ReadU32(bytes.data()) == kGlbMagic) {
        if (bytes.size() < 20) {
            Fail(error, "GLB のファイルが短すぎます");
            return std::nullopt;
        }
        const uint32_t version = ReadU32(bytes.data() + 4);
        if (version != 2) {
            Fail(error, std::format("GLB のバージョン {} には対応していません (2 だけ読めます)", version));
            return std::nullopt;
        }
        const size_t total = std::min<size_t>(ReadU32(bytes.data() + 8), bytes.size());
        size_t at = 12;
        while (at + 8 <= total) {
            const size_t length = ReadU32(bytes.data() + at);
            const uint32_t type = ReadU32(bytes.data() + at + 4);
            at += 8;
            if (length > total - at) {
                Fail(error, "GLB のチャンクがファイルからはみ出しています");
                return std::nullopt;
            }
            if (type == kChunkJson && jsonText.empty()) {
                jsonText = {reinterpret_cast<const char*>(bytes.data() + at), length};
            } else if (type == kChunkBin && bin.empty()) {
                bin = bytes.subspan(at, length);
            }
            at += (length + 3) & ~size_t{3};
        }
        if (jsonText.empty()) {
            Fail(error, "GLB に JSON のチャンクがありません");
            return std::nullopt;
        }
    } else {
        jsonText = {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
    json::ParseError pe;
    const auto doc = json::Parse(jsonText, &pe);
    if (!doc || !doc->IsObject()) {
        Fail(error, doc ? "glTF の JSON がオブジェクトではありません" : std::format("glTF の JSON を読めません ({} 行 {} 列: {})", pe.line, pe.column, pe.message));
        return std::nullopt;
    }
    return GltfReader(*doc, bin, read, error).Read();
}

std::optional<MeshData> ParseObj(std::string_view text, const ReadRelated& read, std::string* error) {
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    MeshData mesh;
    // Materials: the names used (usemtl), in order, and what the .mtl files say about each name.
    std::vector<std::string> used;
    int current = -1;  // into `used`; -1 before the first usemtl
    std::vector<int> triangleMaterial;
    std::vector<MeshMaterial> library;
    std::unordered_map<Corner, uint32_t, CornerHash> made;
    bool missingNormal = false;
    size_t lineNo = 0;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        const std::string_view key = NextToken(line);
        auto numbers = [&](int need, int max, float* out) {
            int n = 0;
            for (std::string_view t = NextToken(line); !t.empty(); t = NextToken(line)) {
                if (n < max && !ParseFloat(t, out[n])) return false;
                ++n;
            }
            return n >= need;
        };
        if (key == "v") {
            float p[3] = {0, 0, 0};
            if (!numbers(3, 3, p)) {
                Fail(error, std::format("{} 行目: 頂点 (v) の数字が読めません", lineNo));
                return std::nullopt;
            }
            positions.push_back({p[0], p[1], p[2]});
        } else if (key == "vt") {
            float t[2] = {0, 0};
            if (!numbers(1, 2, t)) {
                Fail(error, std::format("{} 行目: UV (vt) の数字が読めません", lineNo));
                return std::nullopt;
            }
            uvs.push_back({t[0], 1.f - t[1]});  // OBJ's V goes up
        } else if (key == "vn") {
            float n[3] = {0, 0, 0};
            if (!numbers(3, 3, n)) {
                Fail(error, std::format("{} 行目: 法線 (vn) の数字が読めません", lineNo));
                return std::nullopt;
            }
            normals.push_back(Normalize({n[0], n[1], n[2]}));
        } else if (key == "f") {
            std::vector<uint32_t> poly;
            for (std::string_view t = NextToken(line); !t.empty(); t = NextToken(line)) {
                // a, a/b, a//c, a/b/c (1-based; negative counts back from the last so far)
                Corner c;
                std::string_view parts[3];
                int count = 0;
                while (count < 3) {
                    const size_t slash = t.find('/');
                    parts[count++] = t.substr(0, slash);
                    if (slash == std::string_view::npos) break;
                    t.remove_prefix(slash + 1);
                }
                auto resolve = [&](std::string_view s, size_t size, int64_t& out, bool required) {
                    if (s.empty()) return !required;
                    int64_t k = 0;
                    if (!ParseInt(s, k) || k == 0) return false;
                    k = k > 0 ? k - 1 : static_cast<int64_t>(size) + k;
                    if (k < 0 || static_cast<size_t>(k) >= size) return false;
                    out = k;
                    return true;
                };
                if (!resolve(parts[0], positions.size(), c.v, true) || (count > 1 && !resolve(parts[1], uvs.size(), c.t, false)) ||
                    (count > 2 && !resolve(parts[2], normals.size(), c.n, false))) {
                    Fail(error, std::format("{} 行目: 面 (f) の頂点の番号が正しくありません", lineNo));
                    return std::nullopt;
                }
                if (c.n < 0) missingNormal = true;
                auto [it, added] = made.try_emplace(c, static_cast<uint32_t>(mesh.vertices.size()));
                if (added) {
                    if (mesh.vertices.size() >= kMaxCount) {
                        Fail(error, "頂点が多すぎます");
                        return std::nullopt;
                    }
                    MeshVertex v;
                    v.position = positions[static_cast<size_t>(c.v)];
                    if (c.t >= 0) v.uv = uvs[static_cast<size_t>(c.t)];
                    if (c.n >= 0) v.normal = normals[static_cast<size_t>(c.n)];
                    mesh.vertices.push_back(v);
                }
                poly.push_back(it->second);
            }
            // A fan from the first corner (polygons in OBJ files are flat and convex).
            for (size_t i = 2; i < poly.size(); ++i) {
                mesh.indices.insert(mesh.indices.end(), {poly[0], poly[i - 1], poly[i]});
                triangleMaterial.push_back(current);
            }
        } else if (key == "usemtl") {
            const std::string name(Rest(line));
            const auto it = std::find(used.begin(), used.end(), name);
            current = static_cast<int>(it - used.begin());
            if (it == used.end()) used.push_back(name);
        } else if (key == "mtllib") {
            // One file, or several separated by spaces (a name with spaces is tried whole first).
            const std::string_view rest = Rest(line);
            std::vector<std::string> files{Slashes(rest)};
            if (rest.find(' ') != std::string_view::npos) {
                for (std::string_view t = NextToken(line); !t.empty(); t = NextToken(line)) files.push_back(Slashes(t));
            }
            for (size_t k = 0; k < files.size(); ++k) {
                auto bytes = read ? read(files[k]) : std::nullopt;
                if (!bytes) {
                    if (k == 0 && files.size() > 1) continue;  // not one name with spaces: the names one by one
                    mesh.warnings.push_back(std::format("材質のファイル「{}」を読めませんでした (色なしで描きます)", files[k]));
                    continue;
                }
                auto mats = ParseMtl({reinterpret_cast<const char*>(bytes->data()), bytes->size()}, FolderOf(files[k]), mesh.images);
                library.insert(library.end(), std::make_move_iterator(mats.begin()), std::make_move_iterator(mats.end()));
                if (k == 0) break;  // the whole line was one file
            }
        }
        // Everything else (o, g, s, l, p ...) is not needed for the shape.
    }
    if (mesh.indices.empty()) {
        Fail(error, "三角形の面 (f) がありません");
        return std::nullopt;
    }
    if (missingNormal) ComputeNormals(mesh);
    ComputeBounds(mesh);
    // The materials used, by name (a name no .mtl has is drawn white).
    for (const std::string& name : used) {
        const auto it = std::find_if(library.begin(), library.end(), [&](const MeshMaterial& m) { return m.name == name; });
        if (it != library.end()) {
            mesh.materials.push_back(*it);
        } else {
            MeshMaterial m;
            m.name = name;
            mesh.materials.push_back(std::move(m));
            if (!library.empty()) mesh.warnings.push_back(std::format("材質「{}」が .mtl にありません (色なしで描きます)", name));
        }
    }
    SortByMaterial(mesh, triangleMaterial);
    return mesh;
}

std::vector<MeshMaterial> ParseMtl(std::string_view text, std::string_view folder, std::vector<MeshImage>& images) {
    std::vector<MeshMaterial> out;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        const std::string_view key = NextToken(line);
        if (key == "newmtl") {
            MeshMaterial m;
            m.name = std::string(Rest(line));
            out.push_back(std::move(m));
            continue;
        }
        if (out.empty()) continue;  // before the first material: nothing to give it to
        MeshMaterial& m = out.back();
        if (key == "Kd") {
            float rgb[3] = {1, 1, 1};
            int n = 0;
            for (std::string_view t = NextToken(line); !t.empty() && n < 3; t = NextToken(line)) {
                if (!ParseFloat(t, rgb[n])) break;
                ++n;
            }
            if (n == 1) rgb[1] = rgb[2] = rgb[0];  // one number: grey
            // The screen's colours (as in every paint tool) into linear light.
            if (n == 1 || n == 3) m.baseColor = {ScreenToLinear(rgb[0]), ScreenToLinear(rgb[1]), ScreenToLinear(rgb[2]), m.baseColor.a};
        } else if (key == "Ke") {
            float rgb[3] = {0, 0, 0};
            int n = 0;
            for (std::string_view t = NextToken(line); !t.empty() && n < 3; t = NextToken(line)) {
                if (!ParseFloat(t, rgb[n])) break;
                ++n;
            }
            if (n == 1) rgb[1] = rgb[2] = rgb[0];
            if (n == 1 || n == 3) m.emissive = {ScreenToLinear(rgb[0]), ScreenToLinear(rgb[1]), ScreenToLinear(rgb[2]), 1};
        } else if (key == "Ns") {
            // The shininess (0 .. 1000) as a roughness, as Blender reads it back (it writes (1 - roughness)² x 1000).
            float ns = 0;
            if (ParseFloat(NextToken(line), ns)) m.roughness = 1.f - std::sqrt(std::clamp(ns, 0.f, 1000.f) / 1000.f);
        } else if (key == "Pr" || key == "Pm") {
            // The PBR additions to .mtl: roughness / metallic.
            float v = 0;
            if (ParseFloat(NextToken(line), v)) (key == "Pr" ? m.roughness : m.metallic) = std::clamp(v, 0.f, 1.f);
        } else if (key == "d" || key == "Tr") {
            float v = 1;
            if (ParseFloat(NextToken(line), v)) {
                m.baseColor.a = std::clamp(key == "d" ? v : 1.f - v, 0.f, 1.f);
                m.alpha = m.baseColor.a < 1.f ? AlphaMode::Blend : AlphaMode::Opaque;
            }
        } else if (key == "map_Kd" || key == "map_Ke" || key == "norm" || key == "map_Bump" || key == "bump") {
            // Options (-s 1 1 1, -clamp on, -bm 0.5 ...) come before the file's name, which is the rest.
            // A bump map is taken as a normal map (what Blender writes for one); -bm is its strength.
            std::string_view rest = Rest(line);
            float bm = 1.f;
            while (rest.starts_with("-")) {
                std::string_view probe = rest;
                const std::string_view option = NextToken(probe);
                const bool numbers = option == "-o" || option == "-s" || option == "-t";  // 1 to 3 numbers
                int args = numbers ? 3 : option == "-mm" ? 2 : 1;
                for (; args > 0; --args) {
                    std::string_view look = probe;
                    const std::string_view t = NextToken(look);
                    float value = 0;
                    if (t.empty() || (numbers && !ParseFloat(t, value))) break;
                    if (option == "-bm") ParseFloat(t, bm);
                    probe = look;
                }
                rest = Rest(probe);
            }
            if (rest.empty()) continue;
            const std::string file = std::string(folder) + Slashes(rest);
            const auto it = std::find_if(images.begin(), images.end(), [&](const MeshImage& im) { return im.file == file; });
            const int image = static_cast<int>(it - images.begin());
            if (it == images.end()) {
                MeshImage im;
                im.file = file;
                im.name = file;
                images.push_back(std::move(im));
            }
            if (key == "map_Kd") {
                m.image = image;
            } else if (key == "map_Ke") {
                m.emissiveImage = image;
                if (m.emissive.r == 0 && m.emissive.g == 0 && m.emissive.b == 0) m.emissive = {1, 1, 1, 1};  // the picture as it is
            } else {
                m.normalImage = image;
                m.normalScale = bm;
            }
        }
    }
    return out;
}

float ScreenToLinear(float screen) {
    const float c = std::clamp(screen, 0.f, 1.f);
    if (c >= 1.f) return 1.f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float LinearToScreen(float linear) {
    const float c = std::clamp(linear, 0.f, 1.f);
    if (c >= 1.f) return 1.f;  // exactly (the curve's float sum may miss it by a hair)
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

void SortByMaterial(MeshData& mesh, std::span<const int> materialOfTriangle) {
    const size_t triangles = mesh.indices.size() / 3;
    auto materialOf = [&](size_t t) {
        const int m = t < materialOfTriangle.size() ? materialOfTriangle[t] : -1;
        return m >= 0 && static_cast<size_t>(m) < mesh.materials.size() ? m : -1;
    };
    // Each material's place in the order they first appear (slot 0: no material).
    std::vector<int> rank(mesh.materials.size() + 1, -1);
    std::vector<int> order;
    for (size_t t = 0; t < triangles; ++t) {
        int& r = rank[static_cast<size_t>(materialOf(t) + 1)];
        if (r < 0) {
            r = static_cast<int>(order.size());
            order.push_back(materialOf(t));
        }
    }
    // Counted, then each triangle to its run (the order inside a run stays).
    std::vector<uint32_t> start(order.size() + 1, 0);
    for (size_t t = 0; t < triangles; ++t) start[static_cast<size_t>(rank[static_cast<size_t>(materialOf(t) + 1)]) + 1] += 3;
    for (size_t k = 1; k < start.size(); ++k) start[k] += start[k - 1];
    mesh.parts.clear();
    for (size_t k = 0; k < order.size(); ++k) mesh.parts.push_back({start[k], start[k + 1] - start[k], order[k]});
    std::vector<uint32_t> sorted(triangles * 3);
    for (size_t t = 0; t < triangles; ++t) {
        uint32_t& at = start[static_cast<size_t>(rank[static_cast<size_t>(materialOf(t) + 1)])];
        for (size_t k = 0; k < 3; ++k) sorted[at + k] = mesh.indices[t * 3 + k];
        at += 3;
    }
    mesh.indices = std::move(sorted);
}

std::optional<MeshData> ParseMesh(std::string_view extension, std::span<const uint8_t> bytes, const ReadRelated& read, std::string* error) {
    if (SameIgnoringCase(extension, ".gltf") || SameIgnoringCase(extension, ".glb")) return ParseGltf(bytes, read, error);
    if (SameIgnoringCase(extension, ".obj")) return ParseObj({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, read, error);
    Fail(error, std::format("「{}」のモデルは読めません (.gltf / .glb / .obj が読めます)", extension));
    return std::nullopt;
}

void ComputeNormals(MeshData& mesh) {
    for (MeshVertex& v : mesh.vertices) v.normal = {0, 0, 0};
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        MeshVertex& a = mesh.vertices[mesh.indices[i]];
        MeshVertex& b = mesh.vertices[mesh.indices[i + 1]];
        MeshVertex& c = mesh.vertices[mesh.indices[i + 2]];
        const Vec3 n = Cross(b.position - a.position, c.position - a.position);  // twice the area long
        a.normal = a.normal + n, b.normal = b.normal + n, c.normal = c.normal + n;
    }
    for (MeshVertex& v : mesh.vertices) {
        v.normal = Normalize(v.normal);
        if (Length(v.normal) == 0) v.normal = {0, 1, 0};
    }
}

void ComputeBounds(MeshData& mesh) {
    if (mesh.vertices.empty()) {
        mesh.min = mesh.max = {0, 0, 0};
        return;
    }
    mesh.min = mesh.max = mesh.vertices[0].position;
    for (const MeshVertex& v : mesh.vertices) {
        const Vec3 p = v.position;
        mesh.min = {std::min(mesh.min.x, p.x), std::min(mesh.min.y, p.y), std::min(mesh.min.z, p.z)};
        mesh.max = {std::max(mesh.max.x, p.x), std::max(mesh.max.y, p.y), std::max(mesh.max.z, p.z)};
    }
}

std::optional<std::vector<uint8_t>> DecodeBase64(std::string_view text) {
    auto value = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+' || ch == '-') return 62;
        if (ch == '/' || ch == '_') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    bool ended = false;
    for (const char ch : text) {
        if (ch == '=') {
            ended = true;
            continue;
        }
        if (ch == '\r' || ch == '\n' || ch == ' ') continue;
        const int v = value(ch);
        if (v < 0 || ended) return std::nullopt;  // not base64, or something after the padding
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    if (bits >= 6) return std::nullopt;  // one character alone cannot make a byte
    return out;
}

std::string DecodePercent(std::string_view uri) {
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2])));
            i += 2;
        } else {
            out.push_back(uri[i]);
        }
    }
    return out;
}

}  // namespace rm::space3d
