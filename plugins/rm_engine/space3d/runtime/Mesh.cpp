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

float Number(const json::Value& arr, size_t i, float def) {
    return arr.IsArray() && i < arr.Size() && arr.At(i).IsNumber() ? static_cast<float>(arr.At(i).AsDouble()) : def;
}

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
        if (!LoadBuffers()) return std::nullopt;

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
        const json::Value& views = doc_.Get("bufferViews");
        const int64_t vi = viewIndex->AsInt(-1);
        if (vi < 0 || static_cast<size_t>(vi) >= views.Size()) return Fail(error_, std::format("バッファービュー {} がありません", vi));
        const json::Value& v = views.At(static_cast<size_t>(vi));
        const int64_t bi = v.Get("buffer").AsInt(-1);
        if (bi < 0 || static_cast<size_t>(bi) >= buffers_.size()) return Fail(error_, std::format("バッファー {} がありません", bi));
        const std::vector<uint8_t>& buffer = buffers_[static_cast<size_t>(bi)];
        const int64_t viewOffset = v.Get("byteOffset").AsInt(0), viewLength = v.Get("byteLength").AsInt(-1);
        const int64_t stride = v.Get("byteStride").AsInt(0);
        const int64_t offset = a.Get("byteOffset").AsInt(0);
        if (viewOffset < 0 || viewLength < 0 || offset < 0 || stride < 0 || static_cast<uint64_t>(viewOffset) + static_cast<uint64_t>(viewLength) > buffer.size()) {
            return Fail(error_, std::format("バッファービュー {} がバッファーからはみ出しています", vi));
        }
        out.stride = stride > 0 ? static_cast<size_t>(stride) : element;
        if (out.count > 0) {
            const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(out.count - 1) * out.stride + element;
            if (end > static_cast<uint64_t>(viewLength)) return Fail(error_, std::format("アクセサー {} がバッファービューからはみ出しています", index));
        }
        out.base = buffer.data() + viewOffset + offset;
        return true;
    }

    const json::Value& doc_;
    std::span<const uint8_t> glbBin_;
    const ReadRelated& read_;
    std::string* error_;
    std::vector<std::vector<uint8_t>> buffers_;
    MeshData out_;
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

std::optional<MeshData> ParseObj(std::string_view text, std::string* error) {
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    MeshData mesh;
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
            for (size_t i = 2; i < poly.size(); ++i) mesh.indices.insert(mesh.indices.end(), {poly[0], poly[i - 1], poly[i]});
        }
        // Everything else (o, g, s, usemtl, mtllib, l, p ...) is not needed for the shape.
    }
    if (mesh.indices.empty()) {
        Fail(error, "三角形の面 (f) がありません");
        return std::nullopt;
    }
    if (missingNormal) ComputeNormals(mesh);
    ComputeBounds(mesh);
    return mesh;
}

std::optional<MeshData> ParseMesh(std::string_view extension, std::span<const uint8_t> bytes, const ReadRelated& read, std::string* error) {
    if (SameIgnoringCase(extension, ".gltf") || SameIgnoringCase(extension, ".glb")) return ParseGltf(bytes, read, error);
    if (SameIgnoringCase(extension, ".obj")) return ParseObj({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, error);
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
