#include "rm_engine/space3d/runtime/Fbx.h"

#include "rm_engine/space3d/runtime/Inflate.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>

namespace rm::space3d {

const FbxNode* FbxNode::Child(std::string_view n) const {
    for (const FbxNode& c : children) {
        if (c.name == n) return &c;
    }
    return nullptr;
}

namespace {

constexpr int kMaxDepth = 64;
constexpr uint64_t kMaxArrayBytes = 1ull << 30;
constexpr size_t kMaxCorners = 50'000'000;

bool Fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

// ファイルから来た数はここを通す: NaN や巨大な数が、答えの無いキャスト (それらを整数や float の範囲の外へ
// 変換するのは C++ では未定義) に届かないようにする。
int64_t ToInteger(double d, int64_t def = 0) {
    return std::isfinite(d) && std::fabs(d) < 9.0e18 ? static_cast<int64_t>(d) : def;
}
float ToFloat(double d) {
    if (std::isnan(d)) return 0.f;
    return static_cast<float>(std::clamp(d, -static_cast<double>(std::numeric_limits<float>::max()), static_cast<double>(std::numeric_limits<float>::max())));
}

// バイナリの名前は "Name\0\1Class"、ASCII は "Class::Name" と書く。どちらも後者にそろえる。
std::string ClassName(std::string_view s) {
    const size_t sep = s.find(std::string_view("\0\1", 2));
    if (sep == std::string_view::npos) return std::string(s);
    return std::string(s.substr(sep + 2)) + "::" + std::string(s.substr(0, sep));
}

// --- バイナリ -------------------------------------------------------------------------------

constexpr char kBinaryMagic[] = "Kaydara FBX Binary  ";  // + '\0', 0x1A, 0x00、そのあとに版

class BinaryReader {
public:
    BinaryReader(std::span<const uint8_t> b, uint32_t version, std::string* error, ReadBudget& budget)
        : b_(b), wide_(version >= 7500), error_(error), budget_(budget) {}

    std::optional<std::vector<FbxNode>> Read() {
        std::vector<FbxNode> top;
        size_t at = 27;
        while (at + Header() <= b_.size()) {
            FbxNode n;
            bool end = false;
            if (!Node(at, 0, n, end)) return std::nullopt;
            if (end) break;
            top.push_back(std::move(n));
        }
        return top;
    }

private:
    size_t Header() const { return wide_ ? 25 : 13; }

    template <class T>
    T Get(size_t at) const {
        T v;
        std::memcpy(&v, b_.data() + at, sizeof(T));
        return v;
    }

    bool Node(size_t& at, int depth, FbxNode& out, bool& end) {
        if (depth > kMaxDepth) return Fail(error_, "FBX のノードの入れ子が深すぎます");
        if (!budget_.Step(error_)) return false;
        if (at + Header() > b_.size()) return Fail(error_, "FBX が途中で切れています");
        const uint64_t endAt = wide_ ? Get<uint64_t>(at) : Get<uint32_t>(at);
        const uint64_t count = wide_ ? Get<uint64_t>(at + 8) : Get<uint32_t>(at + 4);
        const uint64_t listLen = wide_ ? Get<uint64_t>(at + 16) : Get<uint32_t>(at + 8);
        const size_t nameLen = b_[at + Header() - 1];
        if (endAt == 0) {  // リストを終わらせる空のレコード
            at += Header();
            end = true;
            return true;
        }
        const size_t nameAt = at + Header();
        const size_t propsAt = nameAt + nameLen;
        if (endAt > b_.size() || endAt <= at || propsAt > endAt || listLen > endAt - propsAt) {
            return Fail(error_, std::format("FBX のノードがファイルからはみ出しています ({} バイト目)", at));
        }
        out.name.assign(reinterpret_cast<const char*>(b_.data() + nameAt), nameLen);
        size_t p = propsAt;
        const size_t propsEnd = propsAt + static_cast<size_t>(listLen);
        for (uint64_t i = 0; i < count; ++i) {
            FbxProperty prop;
            if (!budget_.Take(0, sizeof(FbxProperty), error_) || !Property(p, propsEnd, prop)) return false;
            out.props.push_back(std::move(prop));
        }
        size_t c = propsEnd;
        while (c < endAt) {
            FbxNode child;
            bool childEnd = false;
            if (!Node(c, depth + 1, child, childEnd)) return false;
            if (childEnd) break;
            out.children.push_back(std::move(child));
        }
        at = static_cast<size_t>(endAt);
        return true;
    }

    bool Property(size_t& p, size_t end, FbxProperty& out) {
        if (p >= end) return Fail(error_, "FBX のプロパティが足りません");
        out.type = static_cast<char>(b_[p++]);
        auto need = [&](size_t n) { return n <= end - p; };
        auto scalar = [&](auto v) {
            out.number = static_cast<double>(v);
            out.integer = ToInteger(out.number);
            if constexpr (std::is_same_v<decltype(v), int64_t>) out.integer = v;  // 正確 (id)
        };
        switch (out.type) {
            case 'Y': if (!need(2)) break; scalar(Get<int16_t>(p)); p += 2; return true;
            case 'C': if (!need(1)) break; scalar(static_cast<int>(b_[p])); p += 1; return true;
            case 'I': if (!need(4)) break; scalar(Get<int32_t>(p)); p += 4; return true;
            case 'F': if (!need(4)) break; scalar(Get<float>(p)); p += 4; return true;
            case 'D': if (!need(8)) break; scalar(Get<double>(p)); p += 8; return true;
            case 'L': if (!need(8)) break; scalar(Get<int64_t>(p)); p += 8; return true;
            case 'S':
            case 'R': {
                if (!need(4)) break;
                const uint32_t len = Get<uint32_t>(p);
                p += 4;
                if (!need(len)) break;
                const std::string_view raw(reinterpret_cast<const char*>(b_.data() + p), len);
                out.text = out.type == 'S' ? ClassName(raw) : std::string(raw);
                p += len;
                return true;
            }
            case 'f': case 'd': case 'i': case 'l': case 'b': {
                if (!need(12)) break;
                const uint32_t count = Get<uint32_t>(p), encoding = Get<uint32_t>(p + 4), stored = Get<uint32_t>(p + 8);
                p += 12;
                if (!need(stored)) break;
                const size_t elem = out.type == 'd' || out.type == 'l' ? 8 : out.type == 'b' ? 1 : 4;
                const uint64_t bytes = uint64_t{count} * elem;
                // double で持つ (ファイルの型によらず 1 つ 8 バイト)。圧縮されていれば先に展開する。
                const uint64_t kept = uint64_t{count} * sizeof(double);
                if (kept > kMaxArrayBytes) return Fail(error_, "FBX の配列が大きすぎます");
                if (encoding == 0 && stored != bytes) return Fail(error_, std::format("FBX の配列 ({} バイト目) の長さが合いません", p));
                if (!budget_.Take(0, kept + (encoding == 1 ? bytes : 0), error_)) return false;
                std::span<const uint8_t> data = b_.subspan(p, stored);
                std::optional<std::vector<uint8_t>> unpacked;
                if (encoding == 1) {
                    unpacked = InflateZlib(data, static_cast<size_t>(bytes));
                    if (!unpacked || unpacked->size() != bytes) return Fail(error_, std::format("FBX の圧縮された配列 ({} バイト目) を解けません", p));
                    data = *unpacked;
                } else if (encoding != 0 || stored != bytes) {
                    return Fail(error_, std::format("FBX の配列 ({} バイト目) の長さが合いません", p));
                }
                out.numbers.resize(count);
                for (size_t i = 0; i < count; ++i) {
                    const uint8_t* at = data.data() + i * elem;
                    double v = 0;
                    switch (out.type) {
                        case 'f': { float f; std::memcpy(&f, at, 4); v = f; break; }
                        case 'd': std::memcpy(&v, at, 8); break;
                        case 'i': { int32_t k; std::memcpy(&k, at, 4); v = k; break; }
                        case 'l': { int64_t k; std::memcpy(&k, at, 8); v = static_cast<double>(k); break; }
                        default: v = *at; break;
                    }
                    out.numbers[i] = v;
                }
                p += stored;
                return true;
            }
            default:
                return Fail(error_, std::format("FBX に知らない型「{}」のプロパティがあります ({} バイト目)", out.type, p - 1));
        }
        return Fail(error_, std::format("FBX のプロパティがノードからはみ出しています ({} バイト目)", p));
    }

    std::span<const uint8_t> b_;
    bool wide_;
    std::string* error_;
    ReadBudget& budget_;
};

// --- ASCII ----------------------------------------------------------------------------------
//   Name: value, value, ... { children }      (値: 数、"text"、T / Y のような裸の単語)
//   Vertices: *24 { a: 1,2,3, ... }          (配列: 1 つの配列プロパティになる)
//   ; コメント

class AsciiReader {
public:
    AsciiReader(std::string_view s, std::string* error, ReadBudget& budget) : s_(s), error_(error), budget_(budget) {}

    std::optional<std::vector<FbxNode>> Read() {
        std::vector<FbxNode> top;
        if (!List(0, top)) return std::nullopt;
        return top;
    }

private:
    bool AtEnd() const { return i_ >= s_.size(); }
    void SkipBlank(bool lines) {
        while (!AtEnd()) {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i_;
            } else if (c == '\n' && lines) {
                ++i_;
                ++line_;
            } else if (c == ';' && lines) {
                while (!AtEnd() && s_[i_] != '\n') ++i_;
            } else {
                break;
            }
        }
    }
    bool Bad(std::string_view what) { return Fail(error_, std::format("FBX (テキスト) の {} 行目: {}", line_, what)); }

    bool List(int depth, std::vector<FbxNode>& out) {
        if (depth > kMaxDepth) return Bad("入れ子が深すぎます");
        for (;;) {
            SkipBlank(true);
            if (AtEnd()) return depth == 0 ? true : Bad("「}」が足りません");
            if (s_[i_] == '}') {
                if (depth == 0) return Bad("余分な「}」があります");
                ++i_;
                return true;
            }
            const size_t start = i_;
            while (!AtEnd() && s_[i_] != ':' && s_[i_] != '\n' && s_[i_] != '{' && s_[i_] != '}') ++i_;
            if (AtEnd() || s_[i_] != ':') return Bad("「名前:」の形ではありません");
            if (!budget_.Step(error_)) return false;
            FbxNode n;
            n.name = std::string(s_.substr(start, i_ - start));
            ++i_;
            bool isArray = false;
            for (;;) {
                SkipBlank(false);
                if (AtEnd() || s_[i_] == '\n' || s_[i_] == ';') break;
                const char c = s_[i_];
                if (c == '{') {
                    ++i_;
                    if (!List(depth + 1, n.children)) return false;
                    break;
                }
                if (c == '}') break;  // "a: 1,2,3 }" が 1 行に: ここで値のリストが終わる
                if (c == ',') {
                    ++i_;
                    SkipBlank(true);  // 値のリストはカンマのあと、次の行へ続く
                    continue;
                }
                FbxProperty p;
                if (c == '"') {
                    const size_t close = s_.find('"', i_ + 1);
                    if (close == std::string_view::npos) return Bad("「\"」が閉じていません");
                    p.type = 'S';
                    p.text = std::string(s_.substr(i_ + 1, close - i_ - 1));
                    i_ = close + 1;
                } else if (c == '*') {
                    ++i_;  // "*24": 続く { a: ... } の配列の個数
                    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
                    isArray = true;
                    continue;
                } else {
                    const size_t t = i_;
                    while (!AtEnd() && s_[i_] != ',' && s_[i_] != '\n' && s_[i_] != '\r' && s_[i_] != '{' && s_[i_] != '}' && s_[i_] != ' ' &&
                           s_[i_] != '\t') {
                        ++i_;
                    }
                    const std::string_view tok = s_.substr(t, i_ - t);
                    if (!Number(tok, p)) {
                        p.type = 'S';
                        p.text = std::string(tok);  // 裸の単語 (Shading: T)
                    }
                }
                if (!budget_.Take(0, sizeof(FbxProperty), error_)) return false;
                n.props.push_back(std::move(p));
            }
            if (isArray) {
                // 値は子の "a" に入る: バイナリのファイルと同じく、1 つの配列プロパティ。
                FbxProperty arr;
                arr.type = 'd';
                if (const FbxNode* a = n.Child("a")) {
                    arr.numbers.reserve(a->props.size());
                    for (const FbxProperty& v : a->props) arr.numbers.push_back(v.number);
                }
                n.props.clear();
                n.props.push_back(std::move(arr));
                n.children.clear();
            }
            out.push_back(std::move(n));
        }
    }

    static bool Number(std::string_view t, FbxProperty& p) {
        if (t.empty()) return false;
        const bool whole = t.find_first_of(".eE") == std::string_view::npos;
        if (whole) {
            int64_t k = 0;
            const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), k);
            if (ec != std::errc{} || end != t.data() + t.size()) return false;
            p.type = 'L';
            p.integer = k;
            p.number = static_cast<double>(k);
            return true;
        }
        double d = 0;
        const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), d);
        if (ec != std::errc{} || end != t.data() + t.size()) return false;
        p.type = 'D';
        p.number = d;
        p.integer = ToInteger(d);
        return true;
    }

    std::string_view s_;
    size_t i_ = 0;
    int line_ = 1;
    std::string* error_;
    ReadBudget& budget_;
};

// --- ツリーからメッシュへ ----------------------------------------------------------------------

Mat4 Translation(Vec3 t) {
    Mat4 m;
    m.m[3][0] = t.x, m.m[3][1] = t.y, m.m[3][2] = t.z;
    return m;
}
Mat4 Scaling(Vec3 s) {
    Mat4 m;
    m.m[0][0] = s.x, m.m[1][1] = s.y, m.m[2][2] = s.z;
    return m;
}
Mat4 Inv(const Mat4& m) {
    const auto i = Inverse(m);
    return i ? *i : Mat4{};
}

// FBX の回転の順 (0 = XYZ: X が先、次に Y、次に Z - Transform と同じ)。
Mat4 Euler(Vec3 d, int64_t order) {
    const Mat4 x = RotationMatrix({d.x, 0, 0}), y = RotationMatrix({0, d.y, 0}), z = RotationMatrix({0, 0, d.z});
    switch (order) {
        case 1: return y * z * x;  // XZY
        case 2: return x * z * y;  // YZX
        case 3: return z * x * y;  // YXZ
        case 4: return y * x * z;  // ZXY
        case 5: return x * y * z;  // ZYX
        default: return z * y * x;  // XYZ (と spheric XYZ)
    }
}

class FbxScene {
public:
    FbxScene(const std::vector<FbxNode>& top, std::string* error, ReadBudget& budget, bool fileAxes, bool fileUnits)
        : error_(error), budget_(budget), fileAxes_(fileAxes), fileUnits_(fileUnits) {
        for (const FbxNode& n : top) {
            if (n.name == "Objects") objects_ = &n;
            if (n.name == "Connections") connections_ = &n;
            if (n.name == "GlobalSettings") settings_ = &n;
        }
    }

    std::optional<MeshData> Read() {
        if (!objects_) {
            Fail(error_, "FBX に Objects がありません");
            return std::nullopt;
        }
        for (const FbxNode& o : objects_->children) {
            if (!o.props.empty()) byId_[o.props[0].integer] = &o;
        }
        if (connections_) {
            // 同じリンクが 2 回書かれていても 1 回と数える (同じメッシュを 2 回入れてしまうから)。
            std::set<std::tuple<std::string, int64_t, int64_t, std::string>> seen;
            for (const FbxNode& c : connections_->children) {
                if (c.name != "C" || c.props.size() < 3) continue;
                if (!budget_.Step(error_)) return std::nullopt;
                Link l{c.props[0].text, c.props[1].integer, c.props[2].integer, c.props.size() > 3 ? c.props[3].text : std::string()};
                if (!seen.emplace(l.kind, l.child, l.parent, l.property).second) continue;
                childrenOf_[l.parent].push_back(links_.size());
                if (l.kind == "OO" && Object(l.parent, "Model") && !parentOf_.contains(l.child)) parentOf_[l.child] = l.parent;
                links_.push_back(std::move(l));
            }
        }
        const Mat4 axes = Axes();
        int meshes = 0;
        for (const Link& l : links_) {
            if (l.kind != "OO") continue;
            const FbxNode* geo = Object(l.child, "Geometry");
            const FbxNode* model = Object(l.parent, "Model");
            if (!geo || !model) continue;
            ++meshes;
            if (!Geometry(*geo, l.parent, axes * World(l.parent) * Geometric(*model))) return std::nullopt;
        }
        if (meshes == 0) {
            Fail(error_, "FBX にメッシュがありません (FBX 6 より前の形は読めません)");
            return std::nullopt;
        }
        if (out_.indices.empty()) {
            Fail(error_, "FBX に三角形の面がありません");
            return std::nullopt;
        }
        SortByMaterial(out_, triangleMaterial_);
        ComputeBounds(out_);
        return std::move(out_);
    }

private:
    struct Link {
        std::string kind;  // "OO": オブジェクトからオブジェクト、"OP": オブジェクトから、あるオブジェクトのプロパティ
        int64_t child, parent;
        std::string property;
    };

    // `id` へのリンク (つながっているもの: そのマテリアル、その画像 ...)。
    template <class F>
    void ForChildren(int64_t id, F&& f) const {
        const auto it = childrenOf_.find(id);
        if (it == childrenOf_.end()) return;
        for (const size_t k : it->second) {
            if (!f(links_[k])) return;
        }
    }

    const FbxNode* Object(int64_t id, std::string_view name) const {
        const auto it = byId_.find(id);
        return it != byId_.end() && it->second->name == name ? it->second : nullptr;
    }

    // Properties70 の中のプロパティ (P: "name", "type", "type2", "flags", values...)。
    static const FbxNode* P(const FbxNode* obj, std::string_view name) {
        const FbxNode* props = obj ? obj->Child("Properties70") : nullptr;
        if (!props) return nullptr;
        for (const FbxNode& p : props->children) {
            if (p.name == "P" && !p.props.empty() && p.props[0].text == name) return &p;
        }
        return nullptr;
    }
    static double PNumber(const FbxNode* obj, std::string_view name, double def) {
        const FbxNode* p = P(obj, name);
        return p && p->props.size() > 4 ? p->props[4].number : def;
    }
    static Vec3 PVec(const FbxNode* obj, std::string_view name, Vec3 def) {
        const FbxNode* p = P(obj, name);
        if (!p || p->props.size() < 7) return def;
        return {ToFloat(p->props[4].number), ToFloat(p->props[5].number), ToFloat(p->props[6].number)};
    }

    // ファイルの軸と単位を、このエンジンのものへ (Y が上、X が右、Z が見る人の方、メートル)。
    Mat4 Axes() const {
        const auto axis = [&](std::string_view n, int64_t def) {
            return static_cast<int>(std::clamp<int64_t>(ToInteger(PNumber(settings_, n, static_cast<double>(def)), def), -8, 8));
        };
        const int up = axis("UpAxis", 1), front = axis("FrontAxis", 2), coord = axis("CoordAxis", 0);
        const float upSign = axis("UpAxisSign", 1) < 0 ? -1.f : 1.f, frontSign = axis("FrontAxisSign", 1) < 0 ? -1.f : 1.f,
                    coordSign = axis("CoordAxisSign", 1) < 0 ? -1.f : 1.f;
        const float scale = ToFloat(PNumber(settings_, "UnitScaleFactor", 1.0) / 100.0);  // 1 単位あたりのセンチメートル
        Mat4 m;
        if (fileAxes_ && up >= 0 && up < 3 && front >= 0 && front < 3 && coord >= 0 && coord < 3 && up != front && up != coord && front != coord) {
            m = {};
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) m.m[c][r] = 0;
            }
            m.m[coord][0] = coordSign;  // こちらの X <- ファイルの右
            m.m[up][1] = upSign;        // こちらの Y <- ファイルの上
            m.m[front][2] = frontSign;  // こちらの Z <- ファイルの前
            m.m[3][3] = 1;
        }
        const float s = fileUnits_ && scale > 0 && std::isfinite(scale) ? scale : 1.f;
        return Scaling({s, s, s}) * m;
    }

    // モデルの、親の下での位置 (FBX SDK: T * Roff * Rp * Rpre * R * Rpost^-1 * Rp^-1 * Soff * Sp * S * Sp^-1)。
    Mat4 World(int64_t id, int depth = 0) {
        const FbxNode* model = Object(id, "Model");
        if (!model || depth > kMaxDepth) return Mat4{};
        const int64_t order = ToInteger(PNumber(model, "RotationOrder", 0));
        const Vec3 rp = PVec(model, "RotationPivot", {}), sp = PVec(model, "ScalingPivot", {});
        const Mat4 local = Translation(PVec(model, "Lcl Translation", {})) * Translation(PVec(model, "RotationOffset", {})) * Translation(rp) *
                           Euler(PVec(model, "PreRotation", {}), 0) * Euler(PVec(model, "Lcl Rotation", {}), order) *
                           Inv(Euler(PVec(model, "PostRotation", {}), 0)) * Translation(rp * -1.f) * Translation(PVec(model, "ScalingOffset", {})) *
                           Translation(sp) * Scaling(PVec(model, "Lcl Scaling", {1, 1, 1})) * Translation(sp * -1.f);
        const auto parent = parentOf_.find(id);
        return parent != parentOf_.end() ? World(parent->second, depth + 1) * local : local;
    }

    // メッシュをそのモデルの中だけで動かす (子には伝えない)。
    static Mat4 Geometric(const FbxNode& model) {
        return Translation(PVec(&model, "GeometricTranslation", {})) * Euler(PVec(&model, "GeometricRotation", {}), 0) *
               Scaling(PVec(&model, "GeometricScaling", {1, 1, 1}));
    }

    // レイヤーの 1 つの値 (法線、UV、マテリアル): ポリゴン頂点ごと / コントロール点ごと / ポリゴンごと /
    // 全部同じ、のどれかで、直接か、添字のリスト (`indexName`。空: 使わない) を通して。値が無ければ false。
    static bool LayerValue(const FbxNode& layer, std::string_view dataName, std::string_view indexName, int comps, size_t polyVertex, size_t point,
                           size_t polygon, float* out) {
        const FbxNode* data = layer.Child(dataName);
        if (!data || data->props.empty()) return false;
        const auto text = [&](std::string_view n) {
            const FbxNode* c = layer.Child(n);
            return c && !c->props.empty() ? std::string_view(c->props[0].text) : std::string_view();
        };
        const std::string_view mapping = text("MappingInformationType"), reference = text("ReferenceInformationType");
        size_t k = polyVertex;
        if (mapping == "ByVertice" || mapping == "ByVertex" || mapping == "ByControlPoint") k = point;
        else if (mapping == "ByPolygon") k = polygon;
        else if (mapping == "AllSame") k = 0;
        // (マテリアルも IndexToDirect と言うが、その数がすでに添字なので、添字のリストは無い。)
        const std::vector<double>& v = data->props[0].numbers;
        const size_t values = v.size() / static_cast<size_t>(comps);  // 指す先がいくつあるか
        if (!indexName.empty() && (reference == "IndexToDirect" || reference == "Index")) {
            const FbxNode* index = layer.Child(indexName);
            if (!index || index->props.empty() || k >= index->props[0].numbers.size()) return false;
            // 先に double として比べる: NaN、負の数、巨大な数は何も指さない。
            const double ix = index->props[0].numbers[k];
            if (!(ix >= 0 && ix < static_cast<double>(values))) return false;
            k = static_cast<size_t>(ix);
        }
        if (k >= values) return false;
        for (int c = 0; c < comps; ++c) out[c] = ToFloat(v[k * static_cast<size_t>(comps) + static_cast<size_t>(c)]);
        return true;
    }

    // モデルのマテリアル。つないだ順 (LayerElementMaterial が数えるもの)。
    std::vector<int> ModelMaterials(int64_t modelId) {
        std::vector<int> list;
        ForChildren(modelId, [&](const Link& l) {
            if (l.kind == "OO" && Object(l.child, "Material")) list.push_back(Material(l.child));
            return true;
        });
        return list;
    }

    int Material(int64_t id) {
        if (const auto it = materials_.find(id); it != materials_.end()) return it->second;
        const FbxNode* m = Object(id, "Material");
        MeshMaterial mat;
        if (m && m->props.size() > 1) {
            const std::string& n = m->props[1].text;
            mat.name = n.starts_with("Material::") ? n.substr(10) : n;
        }
        const Vec3 diffuse = PVec(m, "DiffuseColor", PVec(m, "Diffuse", {0.8f, 0.8f, 0.8f}));
        const float factor = ToFloat(PNumber(m, "DiffuseFactor", 1.0));
        const float opacity = ToFloat(std::clamp(PNumber(m, "Opacity", 1.0 - PNumber(m, "TransparencyFactor", 0.0)), 0.0, 1.0));
        mat.baseColor = {std::clamp(diffuse.x * factor, 0.f, 1.f), std::clamp(diffuse.y * factor, 0.f, 1.f), std::clamp(diffuse.z * factor, 0.f, 1.f), opacity};
        if (opacity < 0.999f) mat.alpha = AlphaMode::Blend;
        // 自分で出す光 (リニア。1 より大きくてもよい)。
        const Vec3 emissive = PVec(m, "EmissiveColor", PVec(m, "Emissive", {0, 0, 0})) * ToFloat(PNumber(m, "EmissiveFactor", 1.0));
        mat.emissive = {std::max(emissive.x, 0.f), std::max(emissive.y, 0.f), std::max(emissive.z, 0.f), 1};
        // 拡散色に付いた画像 (どれもそう言っていなければ、付いているどれか)。
        int64_t texture = 0;
        ForChildren(id, [&](const Link& l) {
            if (!Object(l.child, "Texture")) return true;
            const bool diffuseSlot = l.property.find("Diffuse") != std::string::npos || l.property.find("BaseColor") != std::string::npos ||
                                     l.property.find("base_color") != std::string::npos || l.property.find("baseColor") != std::string::npos;
            if (diffuseSlot || texture == 0) texture = l.child;
            return !diffuseSlot;
        });
        if (texture != 0) mat.image = Image(texture);
        const int index = static_cast<int>(out_.materials.size());
        out_.materials.push_back(std::move(mat));
        materials_[id] = index;
        return index;
    }

    int Image(int64_t textureId) {
        if (const auto it = images_.find(textureId); it != images_.end()) return it->second;
        const FbxNode* t = Object(textureId, "Texture");
        const auto text = [&](std::string_view n) {
            const FbxNode* c = t ? t->Child(n) : nullptr;
            return c && !c->props.empty() ? c->props[0].text : std::string();
        };
        MeshImage img;
        std::string file = text("RelativeFilename");
        std::replace(file.begin(), file.end(), '\\', '/');
        const bool absolute = file.empty() || file[0] == '/' || (file.size() > 1 && file[1] == ':');
        if (absolute) {
            // 絶対パスだけ (他の人の PC のもの): 画像はモデルの隣から探す。
            std::string full = text("FileName").empty() ? file : text("FileName");
            std::replace(full.begin(), full.end(), '\\', '/');
            file = full.substr(full.find_last_of('/') + 1);
        }
        // ファイルの中に書かれた画像 (Video の Content) は、ファイルより優先する。
        ForChildren(textureId, [&](const Link& l) {
            const FbxNode* video = Object(l.child, "Video");
            const FbxNode* content = video ? video->Child("Content") : nullptr;
            if (!content || content->props.empty() || content->props[0].text.empty()) return true;
            img.bytes.assign(content->props[0].text.begin(), content->props[0].text.end());
            return false;
        });
        if (img.bytes.empty()) img.file = file;
        img.name = file.empty() ? std::format("画像 {}", out_.images.size() + 1) : file.substr(file.find_last_of('/') + 1);
        const int index = static_cast<int>(out_.images.size());
        out_.images.push_back(std::move(img));
        images_[textureId] = index;
        return index;
    }

    bool Geometry(const FbxNode& geo, int64_t modelId, const Mat4& world) {
        const FbxNode* verts = geo.Child("Vertices");
        const FbxNode* poly = geo.Child("PolygonVertexIndex");
        if (!verts || verts->props.empty() || !poly || poly->props.empty()) return true;  // ポリゴンメッシュではない (NURBS、線 ...)
        const std::vector<double>& pos = verts->props[0].numbers;
        const std::vector<double>& corners = poly->props[0].numbers;
        if (corners.size() > kMaxCorners) return Fail(error_, "FBX のメッシュの頂点が多すぎます");
        const size_t points = pos.size() / 3;
        const FbxNode* normals = geo.Child("LayerElementNormal");
        const FbxNode* uvs = geo.Child("LayerElementUV");
        const FbxNode* mats = geo.Child("LayerElementMaterial");
        const std::vector<int> modelMaterials = ModelMaterials(modelId);
        // 1 つの角に頂点 1 つ (パートにもモデルにも)。三角形は角ごとに多くて 1 つ。
        if (!budget_.Take(corners.size(), uint64_t{corners.size()} * (sizeof(MeshVertex) * 2 + sizeof(uint32_t) * 6 + sizeof(int) * 2), error_)) {
            return false;
        }

        MeshData part;
        std::vector<int> partMaterial;
        bool missingNormal = normals == nullptr;
        size_t polygon = 0, first = 0;
        for (size_t k = 0; k < corners.size(); ++k) {
            const double raw = corners[k];
            const bool last = raw < 0;
            const int64_t number = ToInteger(raw, std::numeric_limits<int64_t>::min());
            const int64_t point = number == std::numeric_limits<int64_t>::min() ? -1 : last ? -number - 1 : number;
            if (point < 0 || static_cast<size_t>(point) >= points) {
                return Fail(error_, std::format("FBX の面の頂点の番号 {} が頂点の数 ({}) を超えています", point, points));
            }
            const size_t cp = static_cast<size_t>(point);
            MeshVertex v;
            v.position = {ToFloat(pos[cp * 3]), ToFloat(pos[cp * 3 + 1]), ToFloat(pos[cp * 3 + 2])};
            float n[3] = {0, 1, 0}, uv[2] = {0, 0};
            if (normals && !LayerValue(*normals, "Normals", "NormalsIndex", 3, k, cp, polygon, n)) missingNormal = true;
            if (uvs) LayerValue(*uvs, "UV", "UVIndex", 2, k, cp, polygon, uv);
            v.normal = {n[0], n[1], n[2]};
            v.uv = {uv[0], 1.f - uv[1]};  // FBX の V は上向き
            part.vertices.push_back(v);
            if (!last) continue;
            // ポリゴンがそろった: 最初の角から扇形に。
            const uint32_t base = static_cast<uint32_t>(first);
            const size_t count = k + 1 - first;
            int material = -1;
            float m = 0;
            if (mats && LayerValue(*mats, "Materials", "", 1, k, cp, polygon, &m) && m >= 0 && m < static_cast<float>(modelMaterials.size())) {
                material = modelMaterials[static_cast<size_t>(m)];
            } else if (!mats && !modelMaterials.empty()) {
                material = modelMaterials[0];
            }
            for (size_t c = 2; c < count; ++c) {
                part.indices.insert(part.indices.end(), {base, base + static_cast<uint32_t>(c - 1), base + static_cast<uint32_t>(c)});
                partMaterial.push_back(material);
            }
            first = k + 1;
            ++polygon;
        }
        if (part.indices.empty()) return true;
        if (missingNormal) ComputeNormals(part);
        AppendPlaced(out_, part, world);
        triangleMaterial_.insert(triangleMaterial_.end(), partMaterial.begin(), partMaterial.end());
        return true;
    }

    const FbxNode* objects_ = nullptr;
    const FbxNode* connections_ = nullptr;
    const FbxNode* settings_ = nullptr;
    std::unordered_map<int64_t, const FbxNode*> byId_;
    std::vector<Link> links_;
    std::unordered_map<int64_t, std::vector<size_t>> childrenOf_;  // 親 -> そのリンク (links_ の中の)
    std::unordered_map<int64_t, int64_t> parentOf_;                // モデル -> その親のモデル
    std::unordered_map<int64_t, int> materials_, images_;
    MeshData out_;
    std::vector<int> triangleMaterial_;
    std::string* error_;
    ReadBudget& budget_;
    bool fileAxes_ = true, fileUnits_ = true;
};

}  // namespace

std::optional<std::vector<FbxNode>> ParseFbxNodes(std::span<const uint8_t> bytes, int* version, std::string* error, ReadBudget* budget) {
    ReadBudget own;
    ReadBudget& b = budget ? *budget : own;
    if (bytes.size() >= 27 && std::memcmp(bytes.data(), kBinaryMagic, 21) == 0) {
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + 23, 4);
        if (version) *version = static_cast<int>(v);
        if (v < 7000) {
            Fail(error, std::format("FBX {} は古い形なので読めません (7.0 以降を読めます)", v));
            return std::nullopt;
        }
        return BinaryReader(bytes, v, error, b).Read();
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.find("FBXHeaderExtension") == std::string_view::npos && text.find("Objects:") == std::string_view::npos) {
        Fail(error, "FBX のファイルではありません");
        return std::nullopt;
    }
    if (version) {
        *version = 0;
        if (const size_t at = text.find("FBXVersion:"); at != std::string_view::npos) {
            std::string_view rest = text.substr(at + 11);
            while (!rest.empty() && rest[0] == ' ') rest.remove_prefix(1);
            std::from_chars(rest.data(), rest.data() + rest.size(), *version);
        }
    }
    return AsciiReader(text, error, b).Read();
}

std::optional<MeshData> ParseFbx(std::span<const uint8_t> bytes, std::string* error, ReadBudget* budget, bool fileAxes, bool fileUnits) {
    ReadBudget own;
    ReadBudget& b = budget ? *budget : own;
    int version = 0;
    const auto nodes = ParseFbxNodes(bytes, &version, error, &b);
    if (!nodes) return std::nullopt;
    if (version != 0 && version < 7000) {
        Fail(error, std::format("FBX {} は古い形なので読めません (7.0 以降を読めます)", version));
        return std::nullopt;
    }
    return FbxScene(*nodes, error, b, fileAxes, fileUnits).Read();
}

}  // namespace rm::space3d
