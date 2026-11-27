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

// モデルの置き方を決める値 (Properties70 のもの。アニメーションでは t / r / s を曲線の値に変える)。
struct LocalValues {
    Vec3 t{}, r{}, s{1, 1, 1};
    Vec3 preRotation{}, postRotation{}, rotationPivot{}, scalingPivot{}, rotationOffset{}, scalingOffset{};
    int64_t order = 0;
};

// モデルの、親の下での行列 (FBX SDK: T * Roff * Rp * Rpre * R * Rpost^-1 * Rp^-1 * Soff * Sp * S * Sp^-1)。
// メッシュを置くのも、骨の休みの姿勢も、テイクのサンプルも、この 1 本の式で作る。
Mat4 FbxLocalMatrix(const LocalValues& v) {
    return Translation(v.t) * Translation(v.rotationOffset) * Translation(v.rotationPivot) * Euler(v.preRotation, 0) * Euler(v.r, v.order) *
           Inv(Euler(v.postRotation, 0)) * Translation(v.rotationPivot * -1.f) * Translation(v.scalingOffset) * Translation(v.scalingPivot) *
           Scaling(v.s) * Translation(v.scalingPivot * -1.f);
}

float& Component(Vec3& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

// "Model::Hips" -> "Hips" (クラスの前置きを取る)。
std::string WithoutClass(const std::string& s, std::string_view cls) {
    return s.starts_with(cls) && s.size() >= cls.size() + 2 && s.compare(cls.size(), 2, "::") == 0 ? s.substr(cls.size() + 2) : s;
}

// --- テイク (アニメーション) -----------------------------------------------------------------

constexpr double kTicksPerSecond = 46186158000.0;  // FBX の時刻 (KTime) の 1 秒

// 曲線 1 本 (AnimationCurve)。時刻は秒、昇順。
struct Curve {
    std::vector<double> time;
    std::vector<float> value;
    std::vector<uint8_t> step;  // キーごと: 0 = 次のキーへ直線、1 = 次のキーまで変えない、2 = 次のキーの値のまま
};

float Evaluate(const Curve& c, double t) {
    if (c.value.empty()) return 0;
    if (t <= c.time.front()) return c.value.front();
    if (t >= c.time.back()) return c.value.back();
    const size_t i = static_cast<size_t>(std::upper_bound(c.time.begin(), c.time.end(), t) - c.time.begin()) - 1;
    if (c.step[i] == 1) return c.value[i];
    if (c.step[i] == 2) return c.value[i + 1];
    const double span = c.time[i + 1] - c.time[i];
    const double f = span > 0 ? (t - c.time[i]) / span : 0.0;
    return ToFloat(c.value[i] + (static_cast<double>(c.value[i + 1]) - c.value[i]) * f);
}

// 1 つのモデルの、テイクで動く T / R / S (Lcl Translation / Lcl Rotation / Lcl Scaling)。
struct Channels {
    bool node[3] = {};                                         // その T / R / S に曲線ノードがある
    int curve[3][3] = {{-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};  // [T R S][x y z] -> Take::curves の番号
    float value[3][3] = {};                                    // 曲線の無い成分の値 (曲線ノードの d|X ...)
};

struct Take {
    std::string name;
    double start = 0, stop = 0;  // 秒
    std::unordered_map<int64_t, Channels> models;
    std::vector<Curve> curves;
    std::vector<std::pair<int, Curve>> morphs;  // ブレンドシェイプの形の番号と、その DeformPercent の曲線 (0..100)
};

// ある時刻のテイクの姿勢 (モデルのワールド行列を覚える: 同じ親を何度も計算しない)。
struct Pose {
    const Take* take = nullptr;
    double time = 0;
    std::unordered_map<int64_t, Mat4> world;
};

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
            if (o.props.empty()) continue;
            byId_[o.props[0].integer] = &o;
            objectOrder_.emplace(o.props[0].integer, objectOrder_.size());
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
        axes_ = Axes();
        axesInv_ = Inv(axes_);
        std::vector<MeshRef> meshes;
        for (const Link& l : links_) {
            if (l.kind != "OO") continue;
            if (!Object(l.child, "Geometry") || !Object(l.parent, "Model")) continue;
            meshes.push_back({l.child, l.parent});
        }
        if (meshes.empty()) {
            Fail(error_, "FBX にメッシュがありません (FBX 6 より前の形は読めません)");
            return std::nullopt;
        }
        // スキン (Deformer) が 1 つも無ければ、骨もテイクも読まない (今までと同じメッシュ)。
        std::vector<GeoSkin> skins;
        if (!PlanSkin(meshes, skins)) return std::nullopt;
        for (size_t i = 0; i < meshes.size(); ++i) {
            const GeoSkin* skin = skins.empty() ? nullptr : &skins[i];
            // スキンのあるメッシュは、バインドしたときの置き方 (クラスターの Transform) で置く。
            const Mat4 place = skin ? skin->placement : World(meshes[i].model);
            const FbxNode& model = *Object(meshes[i].model, "Model");
            if (!Geometry(*Object(meshes[i].geometry, "Geometry"), meshes[i].model, axes_ * place * Geometric(model), skin)) return std::nullopt;
        }
        if (out_.indices.empty()) {
            Fail(error_, "FBX に三角形の面がありません");
            return std::nullopt;
        }
        if (out_.skin) {
            if (out_.skin->vertices.size() != out_.vertices.size()) {
                Fail(error_, "FBX のスキンの頂点の数がメッシュと合いません");
                return std::nullopt;
            }
            std::vector<Vec3> positions;
            positions.reserve(out_.vertices.size());
            for (const MeshVertex& v : out_.vertices) positions.push_back(v.position);
            ComputeBoneBounds(*out_.skin, positions);
            if (!ReadTakes()) return std::nullopt;
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
    struct MeshRef {
        int64_t geometry, model;
    };
    struct GeoSkin {
        Mat4 placement;                  // 頂点を置く行列 (ファイルのワールド。オフセットの前)
        std::vector<SkinVertex> points;  // コントロール点ごと (空: スキンの無いメッシュ - 全部 rigid)
        SkinVertex rigid;
    };
    struct ClusterRef {
        const FbxNode* node;
        int64_t link;  // 骨のモデル
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

    static LocalValues ModelValues(const FbxNode& model) {
        LocalValues v;
        v.t = PVec(&model, "Lcl Translation", {});
        v.r = PVec(&model, "Lcl Rotation", {});
        v.s = PVec(&model, "Lcl Scaling", {1, 1, 1});
        v.preRotation = PVec(&model, "PreRotation", {});
        v.postRotation = PVec(&model, "PostRotation", {});
        v.rotationPivot = PVec(&model, "RotationPivot", {});
        v.scalingPivot = PVec(&model, "ScalingPivot", {});
        v.rotationOffset = PVec(&model, "RotationOffset", {});
        v.scalingOffset = PVec(&model, "ScalingOffset", {});
        v.order = ToInteger(PNumber(&model, "RotationOrder", 0));
        return v;
    }

    // モデルのワールドでの行列 (ファイルの軸と単位のまま)。`pose`: テイクのある時刻 (null: ファイルの値)。
    Mat4 World(int64_t id, Pose* pose = nullptr, int depth = 0) {
        const FbxNode* model = Object(id, "Model");
        if (!model || depth > kMaxDepth) return Mat4{};
        if (pose) {
            if (const auto it = pose->world.find(id); it != pose->world.end()) return it->second;
        }
        LocalValues v = ModelValues(*model);
        if (pose && pose->take) {
            if (const auto ch = pose->take->models.find(id); ch != pose->take->models.end()) {
                Vec3* trs[3] = {&v.t, &v.r, &v.s};
                for (int k = 0; k < 3; ++k) {
                    if (!ch->second.node[k]) continue;
                    for (int a = 0; a < 3; ++a) {
                        const int c = ch->second.curve[k][a];
                        Component(*trs[k], a) = c >= 0 ? Evaluate(pose->take->curves[static_cast<size_t>(c)], pose->time) : ch->second.value[k][a];
                    }
                }
            }
        }
        const Mat4 local = FbxLocalMatrix(v);
        const auto parent = parentOf_.find(id);
        const Mat4 world = parent != parentOf_.end() ? World(parent->second, pose, depth + 1) * local : local;
        if (pose) pose->world[id] = world;
        return world;
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

    // `skin`: スキンのあるファイルでは、どの頂点にも骨とウェイトを付ける (null: スキンの無いファイル)。
    bool Geometry(const FbxNode& geo, int64_t modelId, const Mat4& world, const GeoSkin* skin) {
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
        if (skin && !budget_.Take(0, uint64_t{corners.size()} * sizeof(SkinVertex) * 2, error_)) return false;

        MeshData part;
        std::vector<uint32_t> partPoint;  // 角ごとのコントロール点 (ブレンドシェイプの差を広げる)
        std::vector<SkinVertex> partSkin;
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
            partPoint.push_back(static_cast<uint32_t>(cp));
            if (skin) partSkin.push_back(cp < skin->points.size() ? skin->points[cp] : skin->rigid);
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
        const uint32_t base = static_cast<uint32_t>(out_.vertices.size());
        AppendPlaced(out_, part, world);  // 頂点は角の順のまま足される (partSkin と同じ順)
        if (!geo.props.empty() && !Shapes(geo.props[0].integer, partPoint, base, world)) return false;
        if (skin) out_.skin->vertices.insert(out_.skin->vertices.end(), partSkin.begin(), partSkin.end());
        triangleMaterial_.insert(triangleMaterial_.end(), partMaterial.begin(), partMaterial.end());
        return true;
    }

    // --- ブレンドシェイプ (docs/ANIMATION.md 7-2-2) ---------------------------------------------------
    //   Geometry <- Deformer (BlendShape) <- Deformer (SubDeformer::BlendShapeChannel) <- Geometry (Shape)
    // Shape の Indexes (コントロール点の番号)・Vertices・Normals (どちらも差) を、角ごとの頂点へ広げる (`points`:
    // 角のコントロール点。モデルの頂点 `base` から)。重みはチャンネルの DeformPercent (0..100)。中間のシェイプ
    // (1 つのチャンネルに 2 つ以上の Shape: FullWeights) は最後の形だけ使い、警告する (6 章の 25)。
    bool Shapes(int64_t geometry, const std::vector<uint32_t>& points, uint32_t base, const Mat4& world) {
        const Mat4 nm = Inv(world);  // (法線の差は逆転置で)
        bool ok = true;
        ForChildren(geometry, [&](const Link& bl) {
            const FbxNode* bs = bl.kind == "OO" ? Object(bl.child, "Deformer") : nullptr;
            if (!bs || bs->props.size() < 3 || bs->props[2].text != "BlendShape") return true;
            ForChildren(bl.child, [&](const Link& cl) {
                const FbxNode* ch = cl.kind == "OO" ? Object(cl.child, "Deformer") : nullptr;
                if (!ch || ch->props.size() < 3 || ch->props[2].text != "BlendShapeChannel") return true;
                if (!budget_.Step(error_)) return ok = false;
                std::vector<const FbxNode*> shapes;
                ForChildren(cl.child, [&](const Link& sl) {
                    const FbxNode* s = sl.kind == "OO" ? Object(sl.child, "Geometry") : nullptr;
                    if (s && s->props.size() >= 3 && s->props[2].text == "Shape") shapes.push_back(s);
                    return true;
                });
                if (shapes.empty()) return true;
                std::string name = WithoutClass(ch->props.size() > 1 ? ch->props[1].text : std::string(), "SubDeformer");
                if (name.empty()) name = std::format("形 {}", out_.morphs.size());
                if (shapes.size() > 1) {
                    out_.warnings.push_back(std::format("FBX のブレンドシェイプ「{}」に中間の形が {} 個あります。最後の形だけを使います", name, shapes.size() - 1));
                }
                int at = FindMorph(out_.morphs, name);
                if (at < 0) {
                    if (out_.morphs.size() >= kMaxMorphTargets) return true;
                    at = static_cast<int>(out_.morphs.size());
                    out_.morphs.push_back({name, {}, {}, {}});
                    out_.morphDefaults.push_back(std::clamp(ToFloat(PNumber(ch, "DeformPercent", 0) / 100.0), 0.f, 1.f));
                }
                channelMorph_[cl.child] = at;
                const FbxNode& shape = *shapes.back();
                const FbxNode* idx = shape.Child("Indexes");
                const FbxNode* dv = shape.Child("Vertices");
                const FbxNode* dn = shape.Child("Normals");
                if (!idx || idx->props.empty() || !dv || dv->props.empty()) return true;
                const std::vector<double>& ix = idx->props[0].numbers;
                const std::vector<double>& vs = dv->props[0].numbers;
                const std::vector<double>* ns = dn && !dn->props.empty() ? &dn->props[0].numbers : nullptr;
                if (!budget_.Take(0, uint64_t{points.size()} * (sizeof(uint32_t) + 2 * sizeof(Vec3)) + ix.size() * 16, error_)) return ok = false;
                std::unordered_map<uint32_t, size_t> byPoint;  // コントロール点 -> Indexes の何番目
                for (size_t k = 0; k < ix.size() && k * 3 + 2 < vs.size(); ++k) {
                    const int64_t p = ToInteger(ix[k], -1);
                    if (p >= 0 && p <= std::numeric_limits<uint32_t>::max()) byPoint.emplace(static_cast<uint32_t>(p), k);
                }
                MorphTarget& m = out_.morphs[static_cast<size_t>(at)];
                const bool normals = ns != nullptr || !m.normal.empty();
                if (normals && m.normal.size() < m.vertices.size()) m.normal.resize(m.vertices.size());
                for (size_t c = 0; c < points.size(); ++c) {
                    const auto it = byPoint.find(points[c]);
                    if (it == byPoint.end() || m.vertices.size() >= kMaxMorphVertices) continue;
                    const size_t k = it->second;
                    const Vec3 d{ToFloat(vs[k * 3]), ToFloat(vs[k * 3 + 1]), ToFloat(vs[k * 3 + 2])};
                    m.vertices.push_back(base + static_cast<uint32_t>(c));
                    m.position.push_back(TransformDirection(world, d));
                    if (normals) {
                        Vec3 e{};
                        if (ns && k * 3 + 2 < ns->size()) e = {ToFloat((*ns)[k * 3]), ToFloat((*ns)[k * 3 + 1]), ToFloat((*ns)[k * 3 + 2])};
                        m.normal.push_back({nm.m[0][0] * e.x + nm.m[0][1] * e.y + nm.m[0][2] * e.z, nm.m[1][0] * e.x + nm.m[1][1] * e.y + nm.m[1][2] * e.z,
                                            nm.m[2][0] * e.x + nm.m[2][1] * e.y + nm.m[2][2] * e.z});
                    }
                }
                return true;
            });
            return ok;
        });
        return ok;
    }

    // --- スキン ---------------------------------------------------------------------------------
    //   Geometry <- Deformer (Skin) <- Deformer (SubDeformer::Cluster) <- Model (骨: LimbNode / Null / Root)
    //   クラスター: Indexes / Weights (コントロール点ごと)、Transform (バインドしたときのメッシュの行列)、
    //   TransformLink (バインドしたときの骨の行列)。どちらもファイルの軸と単位のワールド。
    // FBX SDK の式: 頂点 (ワールド) = 骨の今の行列 x TransformLink^-1 x Transform x ジオメトリのオフセット x 点。
    // ここでは頂点を「Transform x オフセット」で置き (バインドしたときの形)、逆バインド = TransformLink^-1 に
    // する (軸の行列 C で挟む: C x M x C^-1)。骨の行列も同じく C で挟むので、
    //   C 骨 C^-1 x C TL^-1 C^-1 x C TM G 点 = C (骨 TL^-1 TM G 点)   (SDK と同じものを、このエンジンの軸で)。
    // スキンのあるファイルでスキンの無いメッシュ (手に持った剣) と、ウェイトの無い点は、そのメッシュの
    // モデルを骨にして、ウェイト 1 で付ける (そのモデルの親の骨と一緒に動く)。


    static bool SkeletonType(const FbxNode& model) {
        const std::string_view type = model.props.size() > 2 ? std::string_view(model.props[2].text) : std::string_view();
        return type == "LimbNode" || type == "Limb" || type == "Root" || type == "Null";
    }
    static const std::vector<double>* Numbers(const FbxNode& n, std::string_view child) {
        const FbxNode* c = n.Child(child);
        return c && !c->props.empty() ? &c->props[0].numbers : nullptr;
    }
    // 16 個の数の行列 (FbxAMatrix の並び: 4 つ目の 4 個が移動 = Mat4 の m[列][行] と同じ)。
    static std::optional<Mat4> Matrix(const FbxNode& n, std::string_view child) {
        const std::vector<double>* v = Numbers(n, child);
        if (!v || v->size() != 16) return std::nullopt;
        Mat4 m;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                const double x = (*v)[static_cast<size_t>(c * 4 + r)];
                if (!std::isfinite(x)) return std::nullopt;
                m.m[c][r] = ToFloat(x);
            }
        }
        return m;
    }
    std::string ModelName(int64_t id) const {
        const FbxNode* m = Object(id, "Model");
        return m && m->props.size() > 1 ? WithoutClass(m->props[1].text, "Model") : std::string();
    }
    // ファイルのワールドの行列を、このエンジンの軸と単位へ。
    Mat4 Conjugate(const Mat4& m) const { return axes_ * m * axesInv_; }

    bool PlanSkin(const std::vector<MeshRef>& meshes, std::vector<GeoSkin>& skins) {
        std::vector<std::vector<ClusterRef>> clusters(meshes.size());
        bool any = false;
        for (size_t i = 0; i < meshes.size(); ++i) {
            int64_t skinId = 0;
            ForChildren(meshes[i].geometry, [&](const Link& l) {
                const FbxNode* d = l.kind == "OO" ? Object(l.child, "Deformer") : nullptr;
                if (!d || d->props.size() < 3 || d->props[2].text != "Skin") return true;
                skinId = l.child;
                return false;
            });
            if (skinId == 0) continue;
            bool ok = true;
            ForChildren(skinId, [&](const Link& l) {
                const FbxNode* c = l.kind == "OO" ? Object(l.child, "Deformer") : nullptr;
                if (!c || c->props.size() < 3 || c->props[2].text != "Cluster") return true;
                if (!budget_.Step(error_)) return ok = false;
                int64_t link = 0;
                ForChildren(l.child, [&](const Link& m) {
                    if (m.kind != "OO" || !Object(m.child, "Model")) return true;
                    link = m.child;
                    return false;
                });
                if (link != 0) clusters[i].push_back({c, link});
                return true;
            });
            if (!ok) return false;
            any = any || !clusters[i].empty();
        }
        if (!any) return true;

        // 骨: クラスターの骨のモデルと、その祖先の骨らしいもの (LimbNode / Null / Root。つなぐため)、
        // スキンの無いメッシュのモデル、ウェイトの無い点があるメッシュのモデル。
        std::vector<int64_t> ids;
        std::unordered_map<int64_t, int> index;
        const auto add = [&](int64_t id) {
            if (index.emplace(id, -1).second) ids.push_back(id);
        };
        for (const auto& list : clusters) {
            for (const ClusterRef& c : list) {
                add(c.link);
                int64_t at = c.link;
                for (int d = 0; d < kMaxDepth; ++d) {
                    const auto p = parentOf_.find(at);
                    if (p == parentOf_.end()) break;
                    const FbxNode* pm = Object(p->second, "Model");
                    if (!pm || !SkeletonType(*pm)) break;
                    add(p->second);
                    at = p->second;
                }
            }
        }
        std::vector<bool> ownBone(meshes.size(), false);
        for (size_t i = 0; i < meshes.size(); ++i) {
            const FbxNode& geo = *Object(meshes[i].geometry, "Geometry");
            const std::vector<double>* pos = Numbers(geo, "Vertices");
            if (!pos || !Numbers(geo, "PolygonVertexIndex")) continue;  // ポリゴンメッシュではない
            const size_t points = pos->size() / 3;
            if (clusters[i].empty()) {
                ownBone[i] = true;
            } else {
                std::vector<bool> weighted(points, false);
                for (const ClusterRef& c : clusters[i]) {
                    const std::vector<double>* ix = Numbers(*c.node, "Indexes");
                    const std::vector<double>* w = Numbers(*c.node, "Weights");
                    if (!ix || !w) continue;
                    for (size_t k = 0; k < std::min(ix->size(), w->size()); ++k) {
                        if ((*ix)[k] >= 0 && (*ix)[k] < static_cast<double>(points) && ToFloat((*w)[k]) > 0) weighted[static_cast<size_t>((*ix)[k])] = true;
                    }
                }
                ownBone[i] = std::find(weighted.begin(), weighted.end(), false) != weighted.end();
            }
            if (ownBone[i]) add(meshes[i].model);
        }
        if (ids.size() > kMaxBones) {
            return Fail(error_, std::format("FBX のスキンの骨が {} 本あります (1 つのモデルで読めるのは {} 本までです)", ids.size(), kMaxBones));
        }

        // 親が先に並ぶように: 親 = 骨として登録された一番近い祖先。根から、ファイルに書かれた順にたどる。
        const auto byFile = [&](int64_t a, int64_t b) { return objectOrder_.at(a) < objectOrder_.at(b); };
        std::sort(ids.begin(), ids.end(), byFile);
        std::unordered_map<int64_t, int64_t> parentBone;  // 骨 -> その親の骨 (無ければ入れない)
        std::unordered_map<int64_t, std::vector<int64_t>> childBones;
        for (const int64_t id : ids) {
            int64_t at = id;
            for (int d = 0; d < kMaxDepth; ++d) {
                const auto p = parentOf_.find(at);
                if (p == parentOf_.end() || p->second == id) break;
                if (index.contains(p->second)) {
                    parentBone[id] = p->second;
                    childBones[p->second].push_back(id);
                    break;
                }
                at = p->second;
            }
        }
        std::vector<int64_t> order;
        std::vector<int> parentIndex;
        const auto walk = [&](int64_t root) {
            std::vector<std::pair<int64_t, int>> stack{{root, -1}};
            while (!stack.empty()) {
                const auto [id, parent] = stack.back();
                stack.pop_back();
                if (index.at(id) >= 0) continue;
                index[id] = static_cast<int>(order.size());
                order.push_back(id);
                parentIndex.push_back(parent);
                const auto ch = childBones.find(id);
                if (ch == childBones.end()) continue;
                for (auto it = ch->second.rbegin(); it != ch->second.rend(); ++it) stack.push_back({*it, index[id]});
            }
        };
        for (const int64_t id : ids) {
            if (!parentBone.contains(id)) walk(id);
        }
        for (const int64_t id : ids) {
            if (index.at(id) < 0) walk(id);  // 親がぐるりと回る (壊れたファイル): ここを根にする
        }

        out_.skin.emplace();
        SkinData& skin = *out_.skin;
        std::vector<Mat4> rest(order.size());
        for (size_t b = 0; b < order.size(); ++b) {
            rest[b] = Conjugate(World(order[b]));
            Bone bone;
            bone.name = ModelName(order[b]);
            bone.parent = parentIndex[b];
            bone.rest = DecomposeTRS(bone.parent >= 0 ? Inv(rest[static_cast<size_t>(bone.parent)]) * rest[b] : rest[b]);
            skin.skeleton.bones.push_back(std::move(bone));
        }
        boneIds_ = order;
        // 逆バインド: クラスターのもの。クラスターの無い骨 (つなぐだけの骨) は休みの姿勢の逆。
        skin.inverseBind.resize(order.size());
        std::vector<bool> bound(order.size(), false);
        bool differs = false;
        std::string differsName;
        const auto bind = [&](size_t b, const Mat4& m) {
            if (!bound[b]) {
                skin.inverseBind[b] = m;
                bound[b] = true;
                return;
            }
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    const float a = skin.inverseBind[b].m[c][r], x = m.m[c][r];
                    if (std::fabs(a - x) > 1e-4f * std::max({1.f, std::fabs(a), std::fabs(x)}) && !differs) {
                        differs = true;
                        differsName = skin.skeleton.bones[b].name;
                    }
                }
            }
        };

        skins.resize(meshes.size());
        uint64_t droppedPoints = 0;
        for (size_t i = 0; i < meshes.size(); ++i) {
            GeoSkin& gs = skins[i];
            const int64_t modelId = meshes[i].model;
            const auto own = index.find(modelId);
            const uint32_t ownIndex = own != index.end() && ownBone[i] ? static_cast<uint32_t>(own->second) : 0;
            gs.rigid = PackInfluences({}, ownIndex);
            gs.placement = World(modelId);
            if (clusters[i].empty()) {
                if (ownBone[i]) bind(ownIndex, Inv(Conjugate(gs.placement)));
                continue;
            }
            // 置き方: 最初のクラスターの Transform (無ければ今のメッシュの行列)。
            for (const ClusterRef& c : clusters[i]) {
                if (const auto tm = Matrix(*c.node, "Transform")) {
                    gs.placement = *tm;
                    break;
                }
            }
            if (ownBone[i]) bind(ownIndex, Inv(Conjugate(gs.placement)));
            const FbxNode& geo = *Object(meshes[i].geometry, "Geometry");
            const size_t points = Numbers(geo, "Vertices")->size() / 3;
            if (!budget_.Take(0, uint64_t{points} * (sizeof(SkinVertex) + sizeof(std::vector<std::pair<uint32_t, float>>)), error_)) return false;
            std::vector<std::vector<std::pair<uint32_t, float>>> influences(points);
            for (const ClusterRef& c : clusters[i]) {
                const size_t b = static_cast<size_t>(index.at(c.link));
                const Mat4 link = Matrix(*c.node, "TransformLink").value_or(World(c.link));
                const auto tm = Matrix(*c.node, "Transform");
                // ふつうは Transform が置き方そのもの (そのときは掛けない: 誤差を足さない)。
                Mat4 m = Inv(link);
                if (tm && std::memcmp(&*tm, &gs.placement, sizeof(Mat4)) != 0) m = m * *tm * Inv(gs.placement);
                bind(b, axes_ * m * axesInv_);
                const std::vector<double>* ix = Numbers(*c.node, "Indexes");
                const std::vector<double>* w = Numbers(*c.node, "Weights");
                if (!ix || !w) continue;
                const size_t n = std::min(ix->size(), w->size());
                if (!budget_.Take(0, uint64_t{n} * sizeof(std::pair<uint32_t, float>), error_)) return false;
                for (size_t k = 0; k < n; ++k) {
                    const double p = (*ix)[k], weight = (*w)[k];
                    if (!(p >= 0 && p < static_cast<double>(points)) || !std::isfinite(weight)) continue;
                    influences[static_cast<size_t>(p)].push_back({static_cast<uint32_t>(b), ToFloat(weight)});
                }
            }
            gs.points.reserve(points);
            for (auto& list : influences) {
                bool dropped = false;
                gs.points.push_back(PackInfluences(std::move(list), ownIndex, &dropped));
                if (dropped) ++droppedPoints;
            }
        }
        for (size_t b = 0; b < order.size(); ++b) {
            if (!bound[b]) skin.inverseBind[b] = Inv(rest[b]);
        }
        if (droppedPoints > 0) {
            out_.warnings.push_back(std::format("FBX: 骨が 5 本以上付いた頂点が {} 個あります。重い 4 本だけを使います", droppedPoints));
        }
        if (differs) {
            out_.warnings.push_back(std::format("FBX: 骨「{}」のバインドの姿勢が、スキンごとに違います。最初のものを使います", differsName));
        }
        return true;
    }

    // --- テイク -> クリップ ------------------------------------------------------------------------
    //   AnimationStack (テイク) <- AnimationLayer <- AnimationCurveNode (T / R / S。モデルの Lcl Translation
    //   などのプロパティにつながる) <- AnimationCurve (d|X / d|Y / d|Z。KeyTime と KeyValueFloat)
    // FBX の回転はオイラー角の曲線で、ピボットや前 / 後の回転、回転の順も掛かる。だから刻みごとに曲線の
    // 値をモデルに入れ、メッシュと同じ式 (FbxLocalMatrix) で骨の行列を作って、位置・回転・大きさに分ける。

    float FileRate() const {
        double rate = 0;
        switch (ToInteger(PNumber(settings_, "TimeMode", 0))) {
            case 1: rate = 120; break;
            case 2: rate = 100; break;
            case 3: rate = 60; break;
            case 4: rate = 50; break;
            case 5: rate = 48; break;
            case 6: case 7: rate = 30; break;
            case 8: case 9: rate = 29.97; break;
            case 10: rate = 25; break;
            case 11: rate = 24; break;
            case 12: rate = 1000; break;
            case 13: rate = 23.976; break;
            case 14: rate = PNumber(settings_, "CustomFrameRate", 0); break;
            case 15: rate = 96; break;
            case 16: rate = 72; break;
            case 17: rate = 59.94; break;
            case 18: rate = 119.88; break;
            default: break;
        }
        if (!(rate > 0) || !std::isfinite(rate)) rate = kDefaultClipRate;
        return static_cast<float>(std::min(rate, static_cast<double>(kMaxClipRate)));
    }

    // 曲線 1 本。false: 読む量の上限を超えた。
    bool ReadCurve(const FbxNode& n, Curve& c) {
        const std::vector<double>* times = Numbers(n, "KeyTime");
        const std::vector<double>* values = Numbers(n, "KeyValueFloat");
        if (!values) values = Numbers(n, "KeyValueDouble");
        if (!times || !values) return true;
        const size_t count = std::min(times->size(), values->size());
        if (!budget_.Take(0, uint64_t{count} * (sizeof(double) + sizeof(float) + 2), error_)) return false;
        // 補間 (KeyAttrFlags。KeyAttrRefCount が、それぞれを続くいくつのキーに使うかを言う):
        // 0x2 = 一定 (0x100 も立っていれば次のキーの値)。直線と曲線 (0x8) は直線として読む。
        std::vector<uint8_t> step(count, 0);
        const std::vector<double>* flags = Numbers(n, "KeyAttrFlags");
        const std::vector<double>* refs = Numbers(n, "KeyAttrRefCount");
        const auto stepOf = [](double f) -> uint8_t {
            const int64_t bits = ToInteger(f);
            return (bits & 0x2) == 0 ? 0 : (bits & 0x100) != 0 ? 2 : 1;
        };
        if (flags && refs && refs->size() == flags->size()) {
            size_t k = 0;
            for (size_t j = 0; j < flags->size() && k < count; ++j) {
                const int64_t run = std::clamp<int64_t>(ToInteger((*refs)[j]), 0, static_cast<int64_t>(count - k));
                for (int64_t r = 0; r < run; ++r) step[k++] = stepOf((*flags)[j]);
            }
        } else if (flags && flags->size() == 1) {
            std::fill(step.begin(), step.end(), stepOf((*flags)[0]));
        }
        std::vector<size_t> keys;
        for (size_t k = 0; k < count; ++k) {
            if (std::isfinite((*times)[k]) && std::isfinite((*values)[k])) keys.push_back(k);
        }
        std::stable_sort(keys.begin(), keys.end(), [&](size_t a, size_t b) { return (*times)[a] < (*times)[b]; });
        for (const size_t k : keys) {
            c.time.push_back((*times)[k] / kTicksPerSecond);
            c.value.push_back(ToFloat((*values)[k]));
            c.step.push_back(step[k]);
        }
        return true;
    }

    bool ReadTakes() {
        // 曲線ノード -> それが動かすモデルとプロパティ。
        std::unordered_map<int64_t, std::pair<int64_t, std::string>> target;
        std::unordered_map<int64_t, int> morphOf;  // 曲線ノード -> ブレンドシェイプの形 (チャンネルの DeformPercent)
        for (const Link& l : links_) {
            if (l.kind == "OP" && Object(l.child, "AnimationCurveNode") && Object(l.parent, "Model")) target.emplace(l.child, std::pair{l.parent, l.property});
            if (l.kind == "OP" && l.property == "DeformPercent" && Object(l.child, "AnimationCurveNode")) {
                if (const auto c = channelMorph_.find(l.parent); c != channelMorph_.end()) morphOf.emplace(l.child, c->second);
            }
        }
        const float rate = FileRate();
        SkinData& skin = *out_.skin;
        for (const FbxNode& stack : objects_->children) {
            if (stack.name != "AnimationStack" || stack.props.empty()) continue;
            if (!budget_.Step(error_)) return false;
            Take take;
            take.name = WithoutClass(stack.props.size() > 1 ? stack.props[1].text : std::string(), "AnimStack");
            if (take.name.empty()) take.name = std::format("テイク {}", out_.clips.size() + 1);
            std::vector<int64_t> layers;
            ForChildren(stack.props[0].integer, [&](const Link& l) {
                if (l.kind == "OO" && Object(l.child, "AnimationLayer")) layers.push_back(l.child);
                return true;
            });
            if (layers.empty()) continue;
            if (layers.size() > 1) {
                out_.warnings.push_back(
                    std::format("FBX のテイク「{}」にはアニメーションのレイヤーが {} 個あります。最初のレイヤーだけを読みます", take.name, layers.size()));
            }
            bool ok = true;
            ForChildren(layers[0], [&](const Link& l) {
                if (l.kind != "OO" || !Object(l.child, "AnimationCurveNode")) return true;
                if (!budget_.Step(error_)) return ok = false;
                if (const auto mo = morphOf.find(l.child); mo != morphOf.end()) {
                    // ブレンドシェイプの重み (DeformPercent)。
                    ForChildren(l.child, [&](const Link& cl) {
                        if (cl.kind != "OP" || !Object(cl.child, "AnimationCurve")) return true;
                        Curve curve;
                        if (!ReadCurve(*Object(cl.child, "AnimationCurve"), curve)) return ok = false;
                        if (!curve.value.empty()) take.morphs.emplace_back(mo->second, std::move(curve));
                        return false;  // (1 本だけ)
                    });
                    return ok;
                }
                const auto t = target.find(l.child);
                if (t == target.end()) return true;
                const int k = t->second.second == "Lcl Translation" ? 0 : t->second.second == "Lcl Rotation" ? 1 : t->second.second == "Lcl Scaling" ? 2 : -1;
                if (k < 0) return true;
                const FbxNode* node = Object(l.child, "AnimationCurveNode");
                Channels& ch = take.models[t->second.first];
                ch.node[k] = true;
                // 曲線の無い成分: 曲線ノードの値 (d|X ...)、それも無ければモデルの値。
                const LocalValues base = ModelValues(*Object(t->second.first, "Model"));
                const Vec3 fallback = k == 0 ? base.t : k == 1 ? base.r : base.s;
                const char* names[3] = {"d|X", "d|Y", "d|Z"};
                for (int a = 0; a < 3; ++a) {
                    Vec3 f = fallback;
                    ch.value[k][a] = ToFloat(PNumber(node, names[a], Component(f, a)));
                }
                ForChildren(l.child, [&](const Link& cl) {
                    if (cl.kind != "OP" || !Object(cl.child, "AnimationCurve")) return true;
                    const int a = cl.property == "d|X" ? 0 : cl.property == "d|Y" ? 1 : cl.property == "d|Z" ? 2 : -1;
                    if (a < 0) return true;
                    Curve curve;
                    if (!ReadCurve(*Object(cl.child, "AnimationCurve"), curve)) return ok = false;
                    if (curve.value.empty()) return true;
                    ch.curve[k][a] = static_cast<int>(take.curves.size());
                    take.curves.push_back(std::move(curve));
                    return true;
                });
                return ok;
            });
            if (!ok) return false;

            // 動く骨: その骨から親の骨までの間のモデル (根の骨はその上の全部) のどれかに曲線がある。
            std::vector<size_t> moving;
            for (size_t b = 0; b < boneIds_.size(); ++b) {
                const int parent = skin.skeleton.bones[b].parent;
                const int64_t stop = parent >= 0 ? boneIds_[static_cast<size_t>(parent)] : 0;
                int64_t at = boneIds_[b];
                for (int d = 0; d <= kMaxDepth; ++d) {
                    if (take.models.contains(at)) {
                        moving.push_back(b);
                        break;
                    }
                    const auto p = parentOf_.find(at);
                    if (p == parentOf_.end() || (parent >= 0 && p->second == stop)) break;
                    at = p->second;
                }
            }
            if (moving.empty() && take.morphs.empty()) continue;

            // 長さ: テイクの LocalStart / LocalStop。無ければキーの範囲。
            double first = std::numeric_limits<double>::infinity(), last = -first;
            for (const Curve& c : take.curves) {
                first = std::min(first, c.time.front());
                last = std::max(last, c.time.back());
            }
            for (const auto& [m, c] : take.morphs) {
                first = std::min(first, c.time.front());
                last = std::max(last, c.time.back());
            }
            const double localStart = PNumber(&stack, "LocalStart", std::nan("")) / kTicksPerSecond;
            const double localStop = PNumber(&stack, "LocalStop", std::nan("")) / kTicksPerSecond;
            if (std::isfinite(localStart) && std::isfinite(localStop) && localStop > localStart) {
                take.start = localStart, take.stop = localStop;
            } else if (first <= last) {
                take.start = first, take.stop = last;
            }
            const double duration = std::max(0.0, take.stop - take.start);
            const double framesWanted = std::round(duration * rate) + 1;
            const double bytes = framesWanted * static_cast<double>(moving.size()) * (sizeof(Vec3) * 2 + sizeof(Quat));
            if (!budget_.Take(0, bytes < 1e18 ? static_cast<uint64_t>(bytes) : uint64_t{1'000'000'000'000'000'000}, error_)) return false;

            Clip clip;
            clip.name = take.name;
            clip.duration = static_cast<float>(duration);
            clip.rate = rate;
            clip.frames = ClipFrames(clip.duration, clip.rate);
            std::vector<BoneTrack> tracks(moving.size());
            for (size_t m = 0; m < moving.size(); ++m) {
                tracks[m].bone = skin.skeleton.bones[moving[m]].name;
                tracks[m].t.reserve(clip.frames), tracks[m].r.reserve(clip.frames), tracks[m].s.reserve(clip.frames);
            }
            for (uint32_t f = 0; f < clip.frames; ++f) {
                if (!budget_.Step(error_)) return false;
                Pose pose{&take, take.start + std::min(static_cast<double>(f) / rate, duration), {}};
                for (size_t m = 0; m < moving.size(); ++m) {
                    const size_t b = moving[m];
                    const int parent = skin.skeleton.bones[b].parent;
                    const Mat4 world = Conjugate(World(boneIds_[b], &pose));
                    const Mat4 local = parent >= 0 ? Inv(Conjugate(World(boneIds_[static_cast<size_t>(parent)], &pose))) * world : world;
                    BoneTRS p = DecomposeTRS(local);
                    // 前のサンプルと同じ側の四元数に (q と -q は同じ回転。混ぜるときに遠回りしない)。
                    if (!tracks[m].r.empty() && QuatDot(tracks[m].r.back(), p.r) < 0) p.r = {-p.r.x, -p.r.y, -p.r.z, -p.r.w};
                    tracks[m].t.push_back(p.t);
                    tracks[m].r.push_back(p.r);
                    tracks[m].s.push_back(p.s);
                }
            }
            // 変わらない列は 1 つに、休みの姿勢のままなら 0 個に。
            for (size_t m = 0; m < moving.size(); ++m) {
                const BoneTRS& rest = skin.skeleton.bones[moving[m]].rest;
                BoneTrack& tr = tracks[m];
                Squeeze(tr.t, rest.t);
                Squeeze(tr.r, rest.r);
                Squeeze(tr.s, rest.s);
                if (!tr.t.empty() || !tr.r.empty() || !tr.s.empty()) clip.tracks.push_back(std::move(tr));
            }
            // ブレンドシェイプの重みの曲線 (0..100 -> 0..1)。
            for (const auto& [m, c] : take.morphs) {
                if (m < 0 || static_cast<size_t>(m) >= out_.morphs.size()) continue;
                MorphTrack track;
                track.target = out_.morphs[static_cast<size_t>(m)].name;
                for (uint32_t f = 0; f < clip.frames; ++f) {
                    track.weights.push_back(Evaluate(c, take.start + std::min(static_cast<double>(f) / rate, duration)) / 100.f);
                }
                Squeeze(track.weights, static_cast<size_t>(m) < out_.morphDefaults.size() ? out_.morphDefaults[static_cast<size_t>(m)] : 0.f);
                if (!track.weights.empty()) clip.morphs.push_back(std::move(track));
            }
            out_.clips.push_back(std::move(clip));
        }
        return true;
    }

    static bool Near(float a, float b) { return std::fabs(a - b) <= 1e-5f * std::max({1.f, std::fabs(a), std::fabs(b)}); }
    static bool Near(Vec3 a, Vec3 b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }
    static bool Near(Quat a, Quat b) {
        if (QuatDot(a, b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
        return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z) && Near(a.w, b.w);
    }
    template <class T>
    static void Squeeze(std::vector<T>& v, const T& rest) {
        if (v.empty()) return;
        for (const T& x : v) {
            if (!Near(x, v.front())) return;
        }
        if (Near(v.front(), rest)) {
            v.clear();
        } else {
            v.resize(1);
        }
    }

    const FbxNode* objects_ = nullptr;
    const FbxNode* connections_ = nullptr;
    const FbxNode* settings_ = nullptr;
    std::unordered_map<int64_t, const FbxNode*> byId_;
    std::vector<Link> links_;
    std::unordered_map<int64_t, std::vector<size_t>> childrenOf_;  // 親 -> そのリンク (links_ の中の)
    std::unordered_map<int64_t, int64_t> parentOf_;                // モデル -> その親のモデル
    std::unordered_map<int64_t, int> materials_, images_;
    std::unordered_map<int64_t, size_t> objectOrder_;  // オブジェクト -> Objects の中の何番目
    Mat4 axes_, axesInv_;                              // ファイルの軸と単位 -> このエンジンの (とその逆)
    std::vector<int64_t> boneIds_;                     // スキンの骨 (Skeleton::bones と同じ順) のモデル
    std::unordered_map<int64_t, int> channelMorph_;    // ブレンドシェイプのチャンネル -> out_.morphs の番号
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
