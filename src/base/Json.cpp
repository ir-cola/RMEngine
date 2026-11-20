#include "base/Json.h"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace rm::json {

// ---------------------------------------------------------------------------
// 値 (Value)
// ---------------------------------------------------------------------------

Value Value::MakeArray() {
    Value v;
    v.type_ = Type::Array;
    return v;
}

Value Value::MakeObject() {
    Value v;
    v.type_ = Type::Object;
    return v;
}

int64_t Value::AsInt(int64_t def) const {
    if (type_ == Type::Int) return i_;
    if (type_ == Type::Double && std::isfinite(d_) && d_ == std::floor(d_) && std::fabs(d_) < 9.2e18) {
        return static_cast<int64_t>(d_);
    }
    return def;
}

double Value::AsDouble(double def) const {
    if (type_ == Type::Double) return d_;
    if (type_ == Type::Int) return static_cast<double>(i_);
    return def;
}

const std::string& Value::AsString() const {
    static const std::string kEmpty;
    return type_ == Type::String ? s_ : kEmpty;
}

size_t Value::Size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

void Value::Push(Value v) {
    if (type_ == Type::Null) type_ = Type::Array;
    if (type_ != Type::Array) return;
    arr_.push_back(std::move(v));
}

size_t Value::IndexOf(std::string_view key) const {
    if (type_ != Type::Object) return kNotFound;
    if (obj_.size() < kIndexFrom) {
        for (size_t i = 0; i < obj_.size(); ++i) {
            if (obj_[i].first == key) return i;
        }
        return kNotFound;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!index_.built || index_.size != obj_.size()) {
            index_.Forget();
            index_.at = std::make_unique<std::unordered_map<std::string, size_t, KeyHash, std::equal_to<>>>();
            index_.at->reserve(obj_.size());
            for (size_t i = 0; i < obj_.size(); ++i) index_.at->try_emplace(obj_[i].first, i);  // (キーの最初のもの。検索で見つかる通り)
            index_.size = obj_.size();
            index_.built = true;
        }
        const auto it = index_.at->find(key);
        if (it == index_.at->end()) return kNotFound;
        if (it->second < obj_.size() && obj_[it->second].first == key) return it->second;
        index_.built = false;  // 一覧が外から変えられた: 次に 1 回作り直す
    }
    return kNotFound;
}

const Value* Value::Find(std::string_view key) const {
    const size_t i = IndexOf(key);
    return i == kNotFound ? nullptr : &obj_[i].second;
}

Value* Value::Find(std::string_view key) {
    const size_t i = IndexOf(key);
    return i == kNotFound ? nullptr : &obj_[i].second;
}

const Value& Value::Get(std::string_view key) const {
    static const Value kNull;
    const Value* v = Find(key);
    return v ? *v : kNull;
}

Value& Value::Set(std::string_view key, Value v) {
    if (type_ == Type::Null) type_ = Type::Object;
    if (Value* existing = Find(key)) {
        *existing = std::move(v);
        return *existing;
    }
    obj_.emplace_back(std::string(key), std::move(v));
    if (index_.built && index_.size + 1 == obj_.size()) {  // 最新の索引は最新のまま保つ
        index_.at->try_emplace(obj_.back().first, obj_.size() - 1);
        index_.size = obj_.size();
    }
    return obj_.back().second;
}

bool Value::Erase(std::string_view key) {
    const size_t i = IndexOf(key);
    if (i == kNotFound) return false;
    obj_.erase(obj_.begin() + static_cast<std::ptrdiff_t>(i));
    index_.Forget();  // これより後ろのキーが動いた
    return true;
}

bool Value::operator==(const Value& o) const {
    if (type_ != o.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return b_ == o.b_;
        case Type::Int: return i_ == o.i_;
        case Type::Double: return d_ == o.d_;
        case Type::String: return s_ == o.s_;
        case Type::Array: return arr_ == o.arr_;
        case Type::Object: return obj_ == o.obj_;
    }
    return false;
}

// ---------------------------------------------------------------------------
// パーサー (Parser)
// ---------------------------------------------------------------------------

namespace {

constexpr int kMaxDepth = 256;

void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    bool ParseDocument(Value& out) {
        if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF) {
            p_ = 3;
        }
        SkipWs();
        if (!ParseValue(out, 0)) return false;
        SkipWs();
        if (p_ != s_.size()) return Fail("値の後ろに余分な文字があります");
        return true;
    }

    void FillError(ParseError& e) const {
        e.offset = errPos_;
        e.message = err_;
        e.line = 1;
        e.column = 1;
        for (size_t i = 0; i < errPos_ && i < s_.size(); ++i) {
            if (s_[i] == '\n') {
                ++e.line;
                e.column = 1;
            } else {
                ++e.column;
            }
        }
    }

private:
    bool Fail(const char* msg) {
        if (err_.empty()) {
            err_ = msg;
            errPos_ = p_;
        }
        return false;
    }

    bool AtEnd() const { return p_ >= s_.size(); }
    char Peek() const { return AtEnd() ? '\0' : s_[p_]; }

    void SkipWs() {
        while (!AtEnd()) {
            const char c = s_[p_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++p_;
            } else {
                break;
            }
        }
    }

    bool Literal(std::string_view word) {
        if (s_.substr(p_, word.size()) != word) return Fail("不明な値です");
        p_ += word.size();
        return true;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) return Fail("入れ子が深すぎます");
        switch (Peek()) {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"': {
                std::string s;
                if (!ParseString(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't':
                if (!Literal("true")) return false;
                out = Value(true);
                return true;
            case 'f':
                if (!Literal("false")) return false;
                out = Value(false);
                return true;
            case 'n':
                if (!Literal("null")) return false;
                out = Value();
                return true;
            case '\0':
                if (AtEnd()) return Fail("値がありません (ファイルが途中で終わっています)");
                return Fail("不明な文字です");
            default:
                if (Peek() == '-' || (Peek() >= '0' && Peek() <= '9')) return ParseNumber(out);
                return Fail("不明な文字です");
        }
    }

    bool ParseObject(Value& out, int depth) {
        ++p_;  // {
        out = Value::MakeObject();
        SkipWs();
        if (Peek() == '}') {
            ++p_;
            return true;
        }
        for (;;) {
            SkipWs();
            if (Peek() != '"') return Fail("キー (文字列) が必要です");
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (Peek() != ':') return Fail("':' が必要です");
            ++p_;
            SkipWs();
            Value v;
            if (!ParseValue(v, depth + 1)) return false;
            out.Set(key, std::move(v));
            SkipWs();
            if (Peek() == ',') {
                ++p_;
                SkipWs();
                if (Peek() == '}') return Fail("最後の要素の後ろに ',' があります");
                continue;
            }
            if (Peek() == '}') {
                ++p_;
                return true;
            }
            return Fail("',' か '}' が必要です");
        }
    }

    bool ParseArray(Value& out, int depth) {
        ++p_;  // [
        out = Value::MakeArray();
        SkipWs();
        if (Peek() == ']') {
            ++p_;
            return true;
        }
        for (;;) {
            SkipWs();
            Value v;
            if (!ParseValue(v, depth + 1)) return false;
            out.Push(std::move(v));
            SkipWs();
            if (Peek() == ',') {
                ++p_;
                SkipWs();
                if (Peek() == ']') return Fail("最後の要素の後ろに ',' があります");
                continue;
            }
            if (Peek() == ']') {
                ++p_;
                return true;
            }
            return Fail("',' か ']' が必要です");
        }
    }

    bool Hex4(uint32_t& out) {
        if (p_ + 4 > s_.size()) return Fail("\\u の後ろに 16 進数 4 桁が必要です");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[p_++];
            out <<= 4;
            if (c >= '0' && c <= '9') {
                out |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                --p_;
                return Fail("\\u の後ろに 16 進数 4 桁が必要です");
            }
        }
        return true;
    }

    bool ParseString(std::string& out) {
        ++p_;  // "
        for (;;) {
            if (AtEnd()) return Fail("文字列が閉じていません");
            const char c = s_[p_];
            if (c == '"') {
                ++p_;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) return Fail("文字列に制御文字があります");
            if (c != '\\') {
                out.push_back(c);
                ++p_;
                continue;
            }
            ++p_;
            if (AtEnd()) return Fail("文字列が閉じていません");
            const char e = s_[p_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // サロゲートペアの下位半分を読んでみる。
                        if (p_ + 6 <= s_.size() && s_[p_] == '\\' && s_[p_ + 1] == 'u') {
                            const size_t save = p_;
                            p_ += 2;
                            uint32_t lo = 0;
                            if (!Hex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                p_ = save;
                                cp = 0xFFFD;
                            }
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    --p_;
                    return Fail("不明なエスケープです");
            }
        }
    }

    bool ParseNumber(Value& out) {
        const size_t start = p_;
        bool isFloat = false;
        if (Peek() == '-') ++p_;
        if (Peek() == '0') {
            ++p_;
        } else if (Peek() >= '1' && Peek() <= '9') {
            while (Peek() >= '0' && Peek() <= '9') ++p_;
        } else {
            return Fail("数値の形が正しくありません");
        }
        if (Peek() == '.') {
            isFloat = true;
            ++p_;
            if (!(Peek() >= '0' && Peek() <= '9')) return Fail("小数点の後ろに数字が必要です");
            while (Peek() >= '0' && Peek() <= '9') ++p_;
        }
        if (Peek() == 'e' || Peek() == 'E') {
            isFloat = true;
            ++p_;
            if (Peek() == '+' || Peek() == '-') ++p_;
            if (!(Peek() >= '0' && Peek() <= '9')) return Fail("指数の数字が必要です");
            while (Peek() >= '0' && Peek() <= '9') ++p_;
        }
        const char* b = s_.data() + start;
        const char* e = s_.data() + p_;
        if (!isFloat) {
            int64_t iv = 0;
            auto r = std::from_chars(b, e, iv);
            if (r.ec == std::errc() && r.ptr == e) {
                out = Value(iv);
                return true;
            }
            // int64 の範囲外: double に回す。
        }
        double dv = 0.0;
        auto r = std::from_chars(b, e, dv);
        if (r.ec != std::errc() || r.ptr != e) return Fail("数値を読めません");
        out = Value(dv);
        return true;
    }

    std::string_view s_;
    size_t p_ = 0;
    std::string err_;
    size_t errPos_ = 0;
};

// ---------------------------------------------------------------------------
// 書き出し (Writer)
// ---------------------------------------------------------------------------

void WriteString(std::string& o, const std::string& s) {
    o.push_back('"');
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o.push_back(ch);
                }
        }
    }
    o.push_back('"');
}

void Indent(std::string& o, int depth) { o.append(static_cast<size_t>(depth) * 2, ' '); }

void WriteValue(std::string& o, const Value& v, int depth, bool pretty) {
    switch (v.GetType()) {
        case Type::Null: o += "null"; break;
        case Type::Bool: o += v.AsBool() ? "true" : "false"; break;
        case Type::Int: {
            char buf[32];
            auto r = std::to_chars(buf, buf + sizeof(buf), v.AsInt());
            o.append(buf, r.ptr);
            break;
        }
        case Type::Double: {
            const double d = v.AsDouble();
            if (!std::isfinite(d)) {
                o += "null";
                break;
            }
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof(buf), d);
            std::string_view sv(buf, static_cast<size_t>(r.ptr - buf));
            o += sv;
            // 「これは double だ」という情報を残し、往復しても結果が変わらないようにする。
            if (sv.find_first_of(".eE") == std::string_view::npos) o += ".0";
            break;
        }
        case Type::String: WriteString(o, v.AsString()); break;
        case Type::Array: {
            if (v.Size() == 0) {
                o += "[]";
                break;
            }
            o.push_back('[');
            bool first = true;
            for (const auto& item : v.Items()) {
                if (!first) o.push_back(',');
                first = false;
                if (pretty) {
                    o.push_back('\n');
                    Indent(o, depth + 1);
                }
                WriteValue(o, item, depth + 1, pretty);
            }
            if (pretty) {
                o.push_back('\n');
                Indent(o, depth);
            }
            o.push_back(']');
            break;
        }
        case Type::Object: {
            if (v.Size() == 0) {
                o += "{}";
                break;
            }
            o.push_back('{');
            bool first = true;
            for (const auto& [key, val] : v.Members()) {
                if (!first) o.push_back(',');
                first = false;
                if (pretty) {
                    o.push_back('\n');
                    Indent(o, depth + 1);
                }
                WriteString(o, key);
                o += pretty ? ": " : ":";
                WriteValue(o, val, depth + 1, pretty);
            }
            if (pretty) {
                o.push_back('\n');
                Indent(o, depth);
            }
            o.push_back('}');
            break;
        }
    }
}

}  // namespace

std::optional<Value> Parse(std::string_view text, ParseError* error) {
    Parser parser(text);
    Value v;
    if (!parser.ParseDocument(v)) {
        if (error) parser.FillError(*error);
        return std::nullopt;
    }
    return v;
}

std::string Write(const Value& v, bool pretty) {
    std::string o;
    WriteValue(o, v, 0, pretty);
    if (pretty) o.push_back('\n');
    return o;
}

}  // namespace rm::json
