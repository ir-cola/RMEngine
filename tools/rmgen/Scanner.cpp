#include "Scanner.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <set>
#include <span>

namespace rmgen {

std::string Diagnostic::Text() const { return std::format("{}({},{}): error {}: {}", file, line, col, code, message); }

namespace {

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

enum class Tok { Ident, Number, String, Char, Punct, End };

struct Token {
    Tok kind = Tok::End;
    std::string text;  // String: the content between the quotes, as written
    int line = 0, col = 0;
    bool Is(std::string_view s) const { return (kind == Tok::Ident || kind == Tok::Punct) && text == s; }
};

bool IsIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
bool IsIdentChar(char c) { return IsIdentStart(c) || (c >= '0' && c <= '9'); }

std::vector<Token> Tokenize(std::string_view s, const std::string& file, std::vector<Diagnostic>& diags) {
    std::vector<Token> out;
    size_t i = 0;
    int line = 1, col = 1;
    bool lineStart = true;
    auto advance = [&](size_t n) {
        for (size_t k = 0; k < n && i < s.size(); ++k, ++i) {
            if (s[i] == '\n') {
                ++line;
                col = 1;
                lineStart = true;
            } else if (static_cast<unsigned char>(s[i]) < 0x80 || static_cast<unsigned char>(s[i]) >= 0xC0) {
                ++col;  // count characters, not UTF-8 continuation bytes
            }
        }
    };
    if (s.substr(0, 3) == "\xEF\xBB\xBF") i = 3;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') {
            advance(1);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            advance(1);
            continue;
        }
        if (c == '#' && lineStart) {  // preprocessor line (with \ continuations)
            while (i < s.size() && s[i] != '\n') {
                if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == '\n' || s[i + 1] == '\r')) advance(1);
                advance(1);
            }
            continue;
        }
        lineStart = false;
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') advance(1);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            advance(2);
            while (i < s.size() && !(s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/')) advance(1);
            advance(2);
            continue;
        }
        Token t;
        t.line = line;
        t.col = col;
        size_t prefix = std::string_view::npos;  // u8"..." / L"..." are read as plain strings
        if (c == '"') {
            prefix = 0;
        } else if ((c == 'L' || c == 'u' || c == 'U') && i + 1 < s.size() && s[i + 1] == '"') {
            prefix = 1;
        } else if (c == 'u' && i + 2 < s.size() && s[i + 1] == '8' && s[i + 2] == '"') {
            prefix = 2;
        }
        if (prefix != std::string_view::npos) {
            advance(prefix + 1);
            const size_t start = i;
            while (i < s.size() && s[i] != '"' && s[i] != '\n') {
                if (s[i] == '\\') advance(1);
                advance(1);
            }
            t.kind = Tok::String;
            t.text = std::string(s.substr(start, i - start));
            if (i >= s.size() || s[i] != '"') {
                diags.push_back({file, t.line, t.col, "RM0007", "文字列が閉じていません"});
            } else {
                advance(1);
            }
            out.push_back(std::move(t));
            continue;
        }
        if (c == '\'') {
            advance(1);
            while (i < s.size() && s[i] != '\'' && s[i] != '\n') {
                if (s[i] == '\\') advance(1);
                advance(1);
            }
            advance(1);
            t.kind = Tok::Char;
            out.push_back(std::move(t));
            continue;
        }
        if (IsIdentStart(c)) {
            const size_t start = i;
            while (i < s.size() && IsIdentChar(s[i])) advance(1);
            t.kind = Tok::Ident;
            t.text = std::string(s.substr(start, i - start));
            out.push_back(std::move(t));
            continue;
        }
        if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9')) {
            const size_t start = i;
            while (i < s.size() && (IsIdentChar(s[i]) || s[i] == '.' || s[i] == '\'' ||
                                    ((s[i] == '+' || s[i] == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E')))) {
                advance(1);
            }
            t.kind = Tok::Number;
            t.text = std::string(s.substr(start, i - start));
            out.push_back(std::move(t));
            continue;
        }
        t.kind = Tok::Punct;
        if (c == ':' && i + 1 < s.size() && s[i + 1] == ':') {
            t.text = "::";
            advance(2);
        } else {
            t.text = std::string(1, c);
            advance(1);
        }
        out.push_back(std::move(t));
    }
    Token end;
    end.line = line;
    end.col = col;
    out.push_back(end);
    return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

bool IsTypeMacro(const Token& t) { return t.Is("RM_STRUCT") || t.Is("RM_COMPONENT") || t.Is("RM_SCRIPT"); }
bool IsFieldMacro(const Token& t) { return t.Is("RM_PROPERTY") || t.Is("RM_TRANSIENT"); }

class Parser {
public:
    Parser(std::vector<Token> toks, const std::string& file, std::vector<Diagnostic>& diags)
        : t_(std::move(toks)), file_(file), diags_(diags) {}

    void Run(Header& h) {
        struct Frame {
            bool ns;
            size_t nsParts;
        };
        std::vector<Frame> frames;
        while (Cur().kind != Tok::End) {
            const Token& tk = Cur();
            if (tk.Is("namespace")) {
                ++p_;
                std::vector<std::string> parts;
                while (Cur().kind == Tok::Ident || Cur().Is("::")) {
                    if (Cur().kind == Tok::Ident && Cur().text != "inline") parts.push_back(Cur().text);
                    ++p_;
                }
                if (Cur().Is("=")) {  // namespace alias
                    SkipTo(";");
                    continue;
                }
                if (Cur().Is("{")) {
                    ++p_;
                    for (auto& part : parts) ns_.push_back(part);
                    frames.push_back({true, parts.size()});
                }
                continue;
            }
            if (tk.Is("{")) {
                frames.push_back({false, 0});
                ++p_;
                continue;
            }
            if (tk.Is("}")) {
                if (!frames.empty()) {
                    if (frames.back().ns) ns_.resize(ns_.size() - frames.back().nsParts);
                    frames.pop_back();
                }
                ++p_;
                continue;
            }
            if (IsTypeMacro(tk)) {
                ParseStruct(h);
                continue;
            }
            if (tk.Is("RM_ENUM")) {
                ParseEnum(h);
                continue;
            }
            if (IsFieldMacro(tk) || tk.Is("RM_ITEM")) {
                Error(tk, "RM0005", std::format("{} は RM_STRUCT / RM_ENUM の中でだけ使えます", tk.text));
                ++p_;
                continue;
            }
            ++p_;
        }
    }

private:
    const Token& Cur() const { return t_[std::min(p_, t_.size() - 1)]; }
    const Token& Peek(size_t n) const { return t_[std::min(p_ + n, t_.size() - 1)]; }

    void Error(const Token& at, const char* code, std::string msg) { diags_.push_back({file_, at.line, at.col, code, std::move(msg)}); }

    void SkipTo(std::string_view punct) {
        int depth = 0;
        while (Cur().kind != Tok::End) {
            if (Cur().Is("{") || Cur().Is("(")) ++depth;
            if (Cur().Is("}") || Cur().Is(")")) {
                if (depth == 0) return;
                --depth;
            }
            if (depth == 0 && Cur().Is(punct)) {
                ++p_;
                return;
            }
            ++p_;
        }
    }

    // From an opening bracket to just after its partner.
    void SkipBalanced() {
        const std::string open = Cur().text;
        const std::string close = open == "{" ? "}" : open == "(" ? ")" : "]";
        int depth = 0;
        while (Cur().kind != Tok::End) {
            if (Cur().Is(open)) ++depth;
            if (Cur().Is(close) && --depth == 0) {
                ++p_;
                return;
            }
            ++p_;
        }
    }

    bool Expect(std::string_view s, const char* what) {
        if (Cur().Is(s)) {
            ++p_;
            return true;
        }
        Error(Cur(), "RM0007", std::format("ここには {} が必要です", what));
        return false;
    }

    std::string Ns() const {
        std::string s;
        for (const auto& part : ns_) s += (s.empty() ? "" : "::") + part;
        return s;
    }

    // ( key, key = "text", key = -1.5 ... )
    std::vector<Attr> ParseAttrs() {
        std::vector<Attr> attrs;
        if (!Expect("(", "(")) return attrs;
        while (!Cur().Is(")") && Cur().kind != Tok::End) {
            if (Cur().kind != Tok::Ident) {
                Error(Cur(), "RM0003", "属性はキーの名前から書いてください (例: Label=\"名前\")");
                SkipBalancedArgs();
                return attrs;
            }
            Attr a;
            a.key = Cur().text;
            a.line = Cur().line;
            a.col = Cur().col;
            ++p_;
            if (Cur().Is("=")) {
                ++p_;
                std::string sign;
                if (Cur().Is("-") || Cur().Is("+")) {
                    sign = Cur().text == "-" ? "-" : "";
                    ++p_;
                }
                if (Cur().kind == Tok::String && sign.empty()) {
                    a.kind = Attr::Kind::String;
                    a.value = Cur().text;
                } else if (Cur().kind == Tok::Number) {
                    a.kind = Attr::Kind::Number;
                    a.value = sign + Cur().text;
                } else if (Cur().kind == Tok::Ident && sign.empty()) {
                    a.kind = Attr::Kind::Ident;
                    a.value = Cur().text;
                } else {
                    Error(Cur(), "RM0003", std::format("{} の値は文字列か数値のリテラルで書いてください", a.key));
                }
                ++p_;
            }
            attrs.push_back(std::move(a));
            if (Cur().Is(",")) {
                ++p_;
            } else if (!Cur().Is(")")) {
                Error(Cur(), "RM0003", "属性の区切りは , です (値に式や定数の名前は書けません。数値のリテラルだけです)");
                SkipBalancedArgs();
                return attrs;
            }
        }
        Expect(")", ")");
        return attrs;
    }

    void SkipBalancedArgs() {
        int depth = 1;
        while (Cur().kind != Tok::End && depth > 0) {
            if (Cur().Is("(")) ++depth;
            if (Cur().Is(")")) --depth;
            ++p_;
        }
    }

    // Checks the keys against what the macro accepts.
    void CheckAttrs(const std::vector<Attr>& attrs, std::string_view macro) {
        struct Rule {
            std::string_view key;
            Attr::Kind kind;
        };
        static const Rule kProperty[] = {{"Edit", Attr::Kind::Flag},       {"ReadOnly", Attr::Kind::Flag},
                                         {"Label", Attr::Kind::String},    {"Tooltip", Attr::Kind::String},
                                         {"Min", Attr::Kind::Number},      {"Max", Attr::Kind::Number},
                                         {"Step", Attr::Kind::Number},     {"AssetKind", Attr::Kind::String}};
        static const Rule kLabelOnly[] = {{"Label", Attr::Kind::String}};
        std::span<const Rule> rules;
        if (macro == "RM_PROPERTY") {
            rules = kProperty;
        } else if (macro != "RM_TRANSIENT") {
            rules = kLabelOnly;
        }
        std::set<std::string> seen;
        for (const Attr& a : attrs) {
            const Rule* rule = nullptr;
            for (const Rule& r : rules) {
                if (r.key == a.key) rule = &r;
            }
            if (!rule) {
                std::string list;
                for (const Rule& r : rules) list += (list.empty() ? "" : " / ") + std::string(r.key);
                Error(TokAt(a), "RM0002",
                      std::format("{} に {} という属性はありません{}", macro, a.key, list.empty() ? "" : " (使えるもの: " + list + ")"));
                continue;
            }
            if (!seen.insert(a.key).second) Error(TokAt(a), "RM0003", std::format("{} を 2 回書いています", a.key));
            if (a.kind != rule->kind) {
                const char* want = rule->kind == Attr::Kind::Flag     ? "値を付けずに書きます"
                                   : rule->kind == Attr::Kind::String ? "文字列で書いてください (例: Label=\"名前\")"
                                                                      : "数値のリテラルで書いてください (定数の名前や式は使えません)";
                Error(TokAt(a), "RM0003", std::format("{} は{}", a.key, want));
            }
        }
    }

    static Token TokAt(const Attr& a) {
        Token t;
        t.line = a.line;
        t.col = a.col;
        return t;
    }

    static std::string LabelOf(const std::vector<Attr>& attrs) {
        for (const Attr& a : attrs) {
            if (a.key == "Label" && a.kind == Attr::Kind::String) return a.value;
        }
        return {};
    }

    void ParseEnum(Header& h) {
        const Token start = Cur();
        ++p_;
        const auto attrs = ParseAttrs();
        CheckAttrs(attrs, "RM_ENUM");
        if (!Expect("enum", "enum class")) return;
        if (!Cur().Is("class") && !Cur().Is("struct")) {
            Error(Cur(), "RM0007", "RM_ENUM は enum class に付けてください");
            return;
        }
        ++p_;
        Enum e;
        e.ns = Ns();
        e.label = LabelOf(attrs);
        e.line = start.line;
        if (Cur().kind != Tok::Ident) {
            Error(Cur(), "RM0007", "列挙の名前が必要です");
            return;
        }
        e.name = Cur().text;
        ++p_;
        if (Cur().Is(":")) {
            while (!Cur().Is("{") && Cur().kind != Tok::End) ++p_;
        }
        if (!Expect("{", "{")) return;
        while (!Cur().Is("}") && Cur().kind != Tok::End) {
            EnumItem item;
            if (Cur().Is("RM_ITEM")) {
                ++p_;
                const auto ia = ParseAttrs();
                CheckAttrs(ia, "RM_ITEM");
                item.label = LabelOf(ia);
            }
            if (Cur().kind != Tok::Ident) {
                Error(Cur(), "RM0007", "列挙子の名前が必要です");
                SkipTo("}");
                return;
            }
            item.name = Cur().text;
            item.line = Cur().line;
            ++p_;
            while (!Cur().Is(",") && !Cur().Is("}") && Cur().kind != Tok::End) ++p_;  // "= value"
            if (Cur().Is(",")) ++p_;
            e.items.push_back(std::move(item));
        }
        Expect("}", "}");
        h.enums.push_back(std::move(e));
    }

    void ParseStruct(Header& h) {
        const Token start = Cur();
        Struct s;
        s.macro = start.text;
        ++p_;
        const auto attrs = ParseAttrs();
        CheckAttrs(attrs, s.macro);
        s.label = LabelOf(attrs);
        s.ns = Ns();
        s.line = start.line;
        if (!Cur().Is("struct")) {
            Error(Cur(), "RM0007", std::format("{} は struct に付けてください (class は使えません)", s.macro));
            return;
        }
        ++p_;
        if (Cur().kind != Tok::Ident) {
            Error(Cur(), "RM0007", "struct の名前が必要です");
            return;
        }
        s.name = Cur().text;
        ++p_;
        if (Cur().Is("final")) ++p_;
        if (Cur().Is(":")) {
            Error(Cur(), "RM0011", "基底クラスは使えません (データの型は継承しない struct にします)");
            while (!Cur().Is("{") && Cur().kind != Tok::End) ++p_;
        }
        if (!Expect("{", "{")) return;
        ParseBody(s);
        h.structs.push_back(std::move(s));
    }

    void ParseBody(Struct& s) {
        std::vector<Attr> pending;
        std::string pendingMacro;
        Token pendingAt;
        auto dropPending = [&](const char* why) {
            if (!pendingMacro.empty()) Error(pendingAt, "RM0005", std::format("{} の後に{}", pendingMacro, why));
            pendingMacro.clear();
            pending.clear();
        };
        while (Cur().kind != Tok::End) {
            const Token& tk = Cur();
            if (tk.Is("}")) {
                dropPending("フィールドがありません");
                ++p_;
                if (Cur().Is(";")) ++p_;
                return;
            }
            if (tk.Is("public") || tk.Is("private") || tk.Is("protected")) {
                if (!tk.Is("public")) Error(tk, "RM0011", "private / protected は使えません (データの型は全部 public にします)");
                ++p_;
                Expect(":", ":");
                continue;
            }
            if (IsFieldMacro(tk)) {
                if (!pendingMacro.empty()) dropPending("もう一度注釈が来ています");
                pendingMacro = tk.text;
                pendingAt = tk;
                ++p_;
                pending = ParseAttrs();
                CheckAttrs(pending, pendingMacro);
                continue;
            }
            if (tk.Is(";")) {
                ++p_;
                continue;
            }
            // Things that are not data members.
            if (tk.Is("static") || tk.Is("using") || tk.Is("typedef") || tk.Is("friend") || tk.Is("template") ||
                tk.Is("constexpr") || tk.Is("enum") || tk.Is("struct") || tk.Is("class") || tk.Is("union") ||
                tk.Is("virtual") || tk.Is("explicit") || tk.Is("operator") || tk.Is("~")) {
                if ((tk.Is("struct") || tk.Is("class") || tk.Is("union") || tk.Is("enum")) && !pendingMacro.empty()) {
                    dropPending("型が来ています (型の中に型は書けません)");
                } else if (!pendingMacro.empty()) {
                    dropPending("データではないものが来ています");
                }
                SkipDeclaration();
                continue;
            }
            ParseMember(s, pending, pendingMacro, pendingAt);
            pendingMacro.clear();
            pending.clear();
        }
    }

    // To the end of a declaration, including a function body.
    void SkipDeclaration() {
        while (Cur().kind != Tok::End) {
            if (Cur().Is("{") || Cur().Is("(") || Cur().Is("[")) {
                const bool brace = Cur().Is("{");
                SkipBalanced();
                if (brace && !Cur().Is(";") && !Cur().Is(",")) return;  // a function body
                continue;
            }
            if (Cur().Is(";")) {
                ++p_;
                return;
            }
            if (Cur().Is("}")) return;
            ++p_;
        }
    }

    void ParseMember(Struct& s, std::vector<Attr>& attrs, std::string& macro, const Token& macroAt) {
        // Collect the type and the name: everything up to "=", "{", ";", "(", "[" or ":" at depth 0.
        const size_t begin = p_;
        int angle = 0;
        while (Cur().kind != Tok::End) {
            if (Cur().Is("<")) ++angle;
            if (Cur().Is(">")) --angle;
            if (angle <= 0 && (Cur().Is("=") || Cur().Is("{") || Cur().Is(";") || Cur().Is("(") || Cur().Is("[") ||
                               Cur().Is(":") || Cur().Is(","))) {
                break;
            }
            if (Cur().Is("}")) break;
            ++p_;
        }
        const size_t end = p_;
        if (Cur().Is("(")) {  // a member function (or a constructor)
            if (!macro.empty()) Error(macroAt, "RM0005", std::format("{} は関数には付けられません", macro));
            p_ = begin;
            SkipDeclaration();
            return;
        }
        if (end <= begin || t_[end - 1].kind != Tok::Ident) {
            Error(t_[begin], "RM0007", "このメンバーを読めません");
            p_ = begin;
            SkipDeclaration();
            return;
        }
        Field f;
        const Token& nameTok = t_[end - 1];
        f.name = nameTok.text;
        f.line = nameTok.line;
        f.col = nameTok.col;
        f.transient = macro == "RM_TRANSIENT";
        f.attrs = attrs;
        for (size_t k = begin; k + 1 < end; ++k) {
            const Token& x = t_[k];
            if (!f.cppType.empty() && !(x.Is("::") || x.Is("<") || x.Is(">") || x.Is(",")) &&
                !(t_[k - 1].Is("::") || t_[k - 1].Is("<") || t_[k - 1].Is(","))) {
                f.cppType += ' ';
            }
            f.cppType += x.text;
        }
        if (Cur().Is("[")) Error(Cur(), "RM0004", std::format("{}: C の配列は使えません (std::vector を使います)", f.name));
        if (Cur().Is(":")) Error(Cur(), "RM0004", std::format("{}: ビットフィールドは使えません", f.name));
        if (Cur().Is(",")) Error(Cur(), "RM0007", std::format("{}: 1 つの宣言に 1 つのフィールドだけ書いてください", f.name));
        if (macro.empty()) {
            Error(nameTok, "RM0001",
                  std::format("フィールド {} に注釈がありません (保存するなら RM_PROPERTY、しないなら RM_TRANSIENT を付けます)", f.name));
        }
        if (!f.transient) {
            size_t k = begin;
            if (!ParseType(k, end - 1, f.type, f.name)) f.type.kind = TypeRef::Kind::Int;
        }
        // Skip the initializer.
        SkipDeclaration();
        s.fields.push_back(std::move(f));
    }

    // Reads a type from tokens [k, end).
    bool ParseType(size_t& k, size_t end, TypeRef& out, const std::string& field) {
        auto fail = [&](const Token& at, std::string why) {
            Error(at, "RM0004", std::format("{}: {}", field, why));
            return false;
        };
        if (k >= end) return fail(t_[k], "型がありません");
        if (t_[k].Is("const") || t_[k].Is("volatile") || t_[k].Is("mutable")) return fail(t_[k], "const / volatile / mutable は付けられません");
        // a qualified name
        std::string name;
        const Token& first = t_[k];
        if (t_[k].Is("::")) ++k;
        while (k < end && t_[k].kind == Tok::Ident) {
            name += t_[k].text;
            ++k;
            if (k < end && t_[k].Is("::")) {
                name += "::";
                ++k;
            } else {
                break;
            }
        }
        if (name.empty()) return fail(first, "型を読めません");
        if (name == "std::vector") {
            if (k >= end || !t_[k].Is("<")) return fail(first, "std::vector には要素の型が要ります");
            ++k;
            TypeRef elem;
            if (!ParseType(k, end, elem, field)) return false;
            if (k >= end || !t_[k].Is(">")) return fail(first, "std::vector の要素の型の後には > が要ります (アロケーターは指定できません)");
            ++k;
            out.kind = TypeRef::Kind::Vector;
            out.element.push_back(std::move(elem));
        } else {
            static const std::map<std::string, TypeRef::Kind> kBuiltin = {
                {"bool", TypeRef::Kind::Bool},      {"int", TypeRef::Kind::Int},         {"int32_t", TypeRef::Kind::Int},
                {"std::int32_t", TypeRef::Kind::Int}, {"float", TypeRef::Kind::Float},  {"std::string", TypeRef::Kind::String},
                {"Vec2", TypeRef::Kind::Vec2},      {"rm::Vec2", TypeRef::Kind::Vec2},   {"Vec3", TypeRef::Kind::Vec3},
                {"rm::Vec3", TypeRef::Kind::Vec3},  {"Color", TypeRef::Kind::Color},     {"rm::Color", TypeRef::Kind::Color},
                {"AssetRef", TypeRef::Kind::AssetRef}, {"rm::AssetRef", TypeRef::Kind::AssetRef},
                {"EntityRef", TypeRef::Kind::EntityRef}, {"rm::EntityRef", TypeRef::Kind::EntityRef},
            };
            static const std::set<std::string> kRefused = {"double", "long", "short", "char", "unsigned", "signed", "int8_t",
                                                           "uint8_t", "int16_t", "uint16_t", "uint32_t", "int64_t", "uint64_t",
                                                           "size_t", "std::size_t", "wchar_t", "std::wstring", "auto"};
            if (auto it = kBuiltin.find(name); it != kBuiltin.end()) {
                out.kind = it->second;
            } else if (kRefused.count(name)) {
                return fail(first, std::format("{} は使えません (使える数の型は int と float です)", name));
            } else {
                out.kind = TypeRef::Kind::Named;
                out.name = name;
            }
        }
        if (k < end && (t_[k].Is("*") || t_[k].Is("&"))) return fail(t_[k], "ポインターと参照は使えません");
        if (k < end && t_[k].Is("<")) return fail(t_[k], "テンプレートの型は std::vector だけ使えます");
        return true;
    }

    std::vector<Token> t_;
    size_t p_ = 0;
    std::string file_;
    std::vector<Diagnostic>& diags_;
    std::vector<std::string> ns_;
};

// ---------------------------------------------------------------------------
// Emitting
// ---------------------------------------------------------------------------

std::string Id(const std::string& qualified) {
    std::string s;
    for (const char c : qualified) s += c == ':' ? '_' : c;
    return s;
}

std::string Cpp(const std::string& qualified) { return "::" + qualified; }

std::string NumberLiteral(std::string v) {
    while (!v.empty() && (v.back() == 'f' || v.back() == 'F')) v.pop_back();
    if (v.find_first_of(".eE") == std::string::npos && v.find('x') == std::string::npos) v += ".0";
    return v;
}

const char* FieldTypeName(const TypeRef& t) {
    switch (t.kind) {
        case TypeRef::Kind::Bool: return "Bool";
        case TypeRef::Kind::Int: return "Int";
        case TypeRef::Kind::Float: return "Float";
        case TypeRef::Kind::String: return "String";
        case TypeRef::Kind::Vec2: return "Vec2";
        case TypeRef::Kind::Vec3: return "Vec3";
        case TypeRef::Kind::Color: return "Color";
        case TypeRef::Kind::AssetRef: return "AssetRef";
        case TypeRef::Kind::EntityRef: return "EntityRef";
        case TypeRef::Kind::Named: return t.isEnum ? "Enum" : "Struct";
        case TypeRef::Kind::Vector: return "Array";
    }
    return "Int";
}

std::string CppTypeOf(const TypeRef& t) {
    switch (t.kind) {
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::Int: return "int";
        case TypeRef::Kind::Float: return "float";
        case TypeRef::Kind::String: return "std::string";
        case TypeRef::Kind::Vec2: return "::rm::Vec2";
        case TypeRef::Kind::Vec3: return "::rm::Vec3";
        case TypeRef::Kind::Color: return "::rm::Color";
        case TypeRef::Kind::AssetRef: return "::rm::AssetRef";
        case TypeRef::Kind::EntityRef: return "::rm::EntityRef";
        case TypeRef::Kind::Named: return Cpp(t.resolved);
        case TypeRef::Kind::Vector: return "std::vector<" + CppTypeOf(t.element[0]) + ">";
    }
    return "int";
}

std::string ValueTypeInit(const TypeRef& t) {
    std::string st = "nullptr", en = "nullptr";
    if (t.kind == TypeRef::Kind::Named) {
        (t.isEnum ? en : st) = "&" + std::string(t.isEnum ? "E_" : "T_") + Id(t.resolved);
    }
    return std::format("{{::rm::reflect::FieldType::{}, {}, {}}}", FieldTypeName(t), st, en);
}

void CollectRefs(const TypeRef& t, std::set<std::string>& structs, std::set<std::string>& enums) {
    if (t.kind == TypeRef::Kind::Named) (t.isEnum ? enums : structs).insert(t.resolved);
    for (const auto& e : t.element) CollectRefs(e, structs, enums);
}

const Header* HeaderOf(const std::vector<Header>& all, const std::string& qualified) {
    for (const auto& h : all) {
        for (const auto& s : h.structs) {
            if (s.Qualified() == qualified) return &h;
        }
        for (const auto& e : h.enums) {
            if (e.Qualified() == qualified) return &h;
        }
    }
    return nullptr;
}

}  // namespace

Header Scan(std::string_view text, const std::string& file, const std::string& includePath, std::vector<Diagnostic>& diags) {
    Header h;
    h.file = file;
    h.includePath = includePath;
    Parser p(Tokenize(text, file, diags), file, diags);
    p.Run(h);
    return h;
}

void Validate(std::vector<Header>& headers, std::vector<Diagnostic>& diags) {
    // Two headers must not make the same file, nor have the same include path (one would hide the
    // other). Compared without case: Windows' files do not tell "A" from "a".
    {
        auto lower = [](std::string s) {
            for (char& c : s) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : (c == '\\' ? '/' : c);
            return s;
        };
        std::map<std::string, const Header*> paths, outputs;
        for (const Header& h : headers) {
            if (const auto [it, fresh] = paths.emplace(lower(h.includePath), &h); !fresh) {
                diags.push_back({h.file, 1, 1, "RM0012",
                                 std::format("#include のパス \"{}\" が {} と同じです (--root のどれから見ても違うパスにしてください)",
                                             h.includePath, it->second->file)});
                continue;
            }
            if (const auto [it, fresh] = outputs.emplace(lower(OutputName(h.includePath)), &h); !fresh) {
                diags.push_back({h.file, 1, 1, "RM0012",
                                 std::format("作るファイル {} が {} のものと同じになります (ヘッダーの名前か場所を変えてください)",
                                             OutputName(h.includePath), it->second->file)});
            }
        }
    }
    struct Known {
        std::string qualified;
        bool isEnum;
    };
    std::vector<Known> known;
    std::map<std::string, std::pair<std::string, int>> where;  // qualified -> (file, line)
    for (const auto& h : headers) {
        auto add = [&](const std::string& q, bool isEnum, int line) {
            if (auto it = where.find(q); it != where.end()) {
                diags.push_back({h.file, line, 1, "RM0009",
                                 std::format("{} は {}({}) にもあります (同じ名前の型は 1 つだけ)", q, it->second.first, it->second.second)});
                return;
            }
            where[q] = {h.file, line};
            known.push_back({q, isEnum});
        };
        for (const auto& s : h.structs) add(s.Qualified(), false, s.line);
        for (const auto& e : h.enums) add(e.Qualified(), true, e.line);
    }

    // A name is looked up from the namespace it is used in outwards, like C++ does.
    auto resolve = [&](const std::string& name, const std::string& fromNs) -> const Known* {
        std::string ns = fromNs;
        for (;;) {
            const std::string q = ns.empty() ? name : ns + "::" + name;
            for (const auto& k : known) {
                if (k.qualified == q) return &k;
            }
            if (ns.empty()) break;
            const size_t cut = ns.rfind("::");
            ns = cut == std::string::npos ? std::string() : ns.substr(0, cut);
        }
        const std::string stripped = name.starts_with("::") ? name.substr(2) : name;
        for (const auto& k : known) {
            if (k.qualified == stripped) return &k;
        }
        return nullptr;
    };

    for (auto& h : headers) {
        for (auto& s : h.structs) {
            size_t props = 0;
            for (auto& f : s.fields) {
                if (f.transient) continue;
                ++props;
                auto fix = [&](auto&& self, TypeRef& t, int depth) -> void {
                    if (t.kind == TypeRef::Kind::Vector) {
                        if (depth > 0) diags.push_back({h.file, f.line, f.col, "RM0008", std::format("{}: 配列の配列は使えません", f.name)});
                        self(self, t.element[0], depth + 1);
                        return;
                    }
                    if (t.kind != TypeRef::Kind::Named) return;
                    if (const Known* k = resolve(t.name, s.ns)) {
                        t.resolved = k->qualified;
                        t.isEnum = k->isEnum;
                    } else {
                        diags.push_back({h.file, f.line, f.col, "RM0006",
                                         std::format("{}: 型 {} に RM_STRUCT / RM_ENUM が付いていません (または rmgen が読むフォルダの外にあります)",
                                                     f.name, t.name)});
                    }
                };
                fix(fix, f.type, 0);

                const TypeRef& base = f.type.kind == TypeRef::Kind::Vector ? f.type.element[0] : f.type;
                const bool numeric = base.kind == TypeRef::Kind::Int || base.kind == TypeRef::Kind::Float ||
                                     base.kind == TypeRef::Kind::Vec2 || base.kind == TypeRef::Kind::Vec3 ||
                                     base.kind == TypeRef::Kind::Color;
                double mn = 0, mx = 0;
                bool hasMin = false, hasMax = false;
                for (const Attr& a : f.attrs) {
                    if ((a.key == "Min" || a.key == "Max" || a.key == "Step") && !numeric) {
                        diags.push_back({h.file, a.line, a.col, "RM0003", std::format("{} は数値・ベクトル・色だけに付けられます", a.key)});
                    }
                    if (a.key == "AssetKind") {
                        // The names themselves (image, model ...) are core/asset's; a test checks every one is known.
                        const bool words = !a.value.empty() && a.value.front() != ',' && a.value.back() != ',' &&
                                           a.value.find(",,") == std::string::npos &&
                                           std::all_of(a.value.begin(), a.value.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || ch == ','; });
                        if (base.kind != TypeRef::Kind::AssetRef) {
                            diags.push_back({h.file, a.line, a.col, "RM0003", "AssetKind は AssetRef (とその配列) だけに付けられます"});
                        } else if (!words) {
                            diags.push_back({h.file, a.line, a.col, "RM0003",
                                             std::format("AssetKind=\"{}\": 種類の名前 (小文字) をコンマで区切って書いてください (例: \"image,model\")", a.value)});
                        }
                    }
                    if (a.kind != Attr::Kind::Number) continue;
                    try {
                        const double v = std::stod(a.value);
                        if (a.key == "Min") {
                            hasMin = true;
                            mn = v;
                        }
                        if (a.key == "Max") {
                            hasMax = true;
                            mx = v;
                        }
                    } catch (...) {
                        diags.push_back({h.file, a.line, a.col, "RM0003", std::format("{} の値 {} を数として読めません", a.key, a.value)});
                    }
                }
                if (hasMin && hasMax && mn > mx) diags.push_back({h.file, f.line, f.col, "RM0003", std::format("{}: Min が Max より大きくなっています", f.name)});
            }
            if (props > 64) diags.push_back({h.file, s.line, 1, "RM0010", std::format("{}: 保存するフィールドは 64 個までです", s.name)});
        }
    }
}

std::string OutputName(const std::string& includePath) {
    std::string s;
    uint32_t hash = 2166136261u;  // FNV-1a of the whole path ('\' read as '/')
    for (const char c : includePath) {
        const char k = c == '\\' ? '/' : c;
        hash = (hash ^ static_cast<uint8_t>(k)) * 16777619u;
        s += k == '/' ? '_' : k;
    }
    if (s.ends_with(".h")) s.resize(s.size() - 2);
    return std::format("{}.{:08x}.reflect.cpp", s, hash);
}

std::string EmitHeader(const std::vector<Header>& all, const Header& h) {
    std::set<std::string> refStructs, refEnums;
    for (const auto& s : h.structs) {
        for (const auto& f : s.fields) {
            if (!f.transient) CollectRefs(f.type, refStructs, refEnums);
        }
    }
    std::set<std::string> includes{h.includePath};
    for (const auto& q : refStructs) {
        if (const Header* o = HeaderOf(all, q)) includes.insert(o->includePath);
    }
    for (const auto& q : refEnums) {
        if (const Header* o = HeaderOf(all, q)) includes.insert(o->includePath);
    }

    std::string o;
    o += std::format("// rmgen が {} から作りました。手で直さないでください (ビルドのたびに作り直されます)。\n\n", h.includePath);
    o += "#include \"core/reflect/Reflect.h\"\n";
    for (const auto& inc : includes) o += std::format("#include \"{}\"\n", inc);
    o += "\n#include <memory>\n#include <new>\n#include <string>\n#include <type_traits>\n#include <typeinfo>\n#include <vector>\n\n";
    o += "namespace rmgen_generated {\n";
    for (const auto& q : refStructs) o += std::format("extern const ::rm::reflect::TypeDesc T_{};\n", Id(q));
    for (const auto& q : refEnums) o += std::format("extern const ::rm::reflect::EnumDesc E_{};\n", Id(q));
    o += "}  // namespace rmgen_generated\n\n";

    for (const auto& e : h.enums) {
        const std::string q = e.Qualified(), id = Id(q), cpp = Cpp(q);
        o += std::format("static_assert(std::is_enum_v<{}>, \"{} は enum class にしてください\");\n", cpp, q);
        o += "namespace rmgen_generated {\n";
        o += std::format("static const ::rm::reflect::EnumItem E_{}_items[] = {{\n", id);
        for (const auto& it : e.items) {
            o += std::format("    {{\"{}\", \"{}\", static_cast<int64_t>({}::{})}},\n", it.name, it.label.empty() ? it.name : it.label, cpp,
                             it.name);
        }
        o += "};\n";
        o += std::format(
            "extern const ::rm::reflect::EnumDesc E_{0} = {{\n"
            "    \"{1}\", E_{0}_items,\n"
            "    [](const void* p) -> int64_t {{ return static_cast<int64_t>(*static_cast<const {2}*>(p)); }},\n"
            "    [](void* p, int64_t v) {{ *static_cast<{2}*>(p) = static_cast<{2}>(v); }},\n"
            "    []() -> const std::type_info& {{ return typeid({2}); }},\n"
            "}};\n",
            id, q, cpp);
        o += "}  // namespace rmgen_generated\n\n";
    }

    for (const auto& s : h.structs) {
        const std::string q = s.Qualified(), id = Id(q), cpp = Cpp(q);
        o += std::format(
            "static_assert(std::is_aggregate_v<{0}>, \"{1}: 反映する型は集成体 (コンストラクタも private も基底クラスも無い struct) にしてください\");\n",
            cpp, q);
        o += std::format(
            "static_assert(::rm::reflect::FieldCount<{0}>() == {1}, \"{2} のフィールドの数が変わっています。rmgen を走らせ直してください (ビルドすると走ります)\");\n",
            cpp, s.fields.size(), q);
        for (const auto& f : s.fields) {
            if (f.transient) continue;
            o += std::format(
                "static_assert(std::is_same_v<decltype({0}::{1}), {2}>, \"{3}::{1} の型が変わっています。rmgen を走らせ直してください (ビルドすると走ります)\");\n",
                cpp, f.name, CppTypeOf(f.type), q);
        }
        o += "namespace rmgen_generated {\n";
        size_t props = 0;
        for (const auto& f : s.fields) {
            if (f.transient) continue;
            ++props;
            if (f.type.kind != TypeRef::Kind::Vector) continue;
            const std::string elem = CppTypeOf(f.type.element[0]);
            o += std::format(
                "static const ::rm::reflect::ArrayOps A_{0}_{1} = {{\n"
                "    {2},\n"
                "    [](const void* v) -> size_t {{ return static_cast<const std::vector<{3}>*>(v)->size(); }},\n"
                "    [](void* v, size_t n) {{ static_cast<std::vector<{3}>*>(v)->resize(n); }},\n"
                "    [](void* v, size_t i) -> void* {{ return &(*static_cast<std::vector<{3}>*>(v))[i]; }},\n"
                "}};\n",
                id, f.name, ValueTypeInit(f.type.element[0]), elem);
        }
        if (props > 0) {
            o += std::format("static const ::rm::reflect::FieldDesc T_{}_fields[] = {{\n", id);
            for (const auto& f : s.fields) {
                if (f.transient) continue;
                auto attr = [&](const char* key) -> const Attr* {
                    for (const auto& a : f.attrs) {
                        if (a.key == key) return &a;
                    }
                    return nullptr;
                };
                const Attr* label = attr("Label");
                const Attr* tip = attr("Tooltip");
                const Attr* mn = attr("Min");
                const Attr* mx = attr("Max");
                const Attr* st = attr("Step");
                const Attr* kinds = attr("AssetKind");
                auto num = [](const Attr* a) { return a && a->kind == Attr::Kind::Number ? NumberLiteral(a->value) : std::string("0.0"); };
                o += std::format(
                    "    {{.name = \"{}\", .value = {}, .access = [](void* o) -> void* {{ return &static_cast<{}*>(o)->{}; }}, .array = {},\n"
                    "     .edit = {}, .readOnly = {}, .label = {}, .tooltip = {},\n"
                    "     .hasMin = {}, .hasMax = {}, .hasStep = {}, .min = {}, .max = {}, .step = {}, .line = {}, .assetKinds = {}}},\n",
                    f.name, ValueTypeInit(f.type), cpp, f.name, f.type.kind == TypeRef::Kind::Vector ? "&A_" + id + "_" + f.name : "nullptr",
                    attr("Edit") ? "true" : "false", attr("ReadOnly") ? "true" : "false",
                    label ? "\"" + label->value + "\"" : "nullptr", tip ? "\"" + tip->value + "\"" : "nullptr",
                    mn && mn->kind == Attr::Kind::Number ? "true" : "false", mx && mx->kind == Attr::Kind::Number ? "true" : "false",
                    st && st->kind == Attr::Kind::Number ? "true" : "false", num(mn), num(mx), num(st), f.line,
                    kinds && kinds->kind == Attr::Kind::String ? "\"" + kinds->value + "\"" : std::string("nullptr"));
            }
            o += "};\n";
        }
        const char* kind = s.macro == "RM_COMPONENT" ? "Component" : s.macro == "RM_SCRIPT" ? "Script" : "Struct";
        o += std::format(
            "extern const ::rm::reflect::TypeDesc T_{0} = {{\n"
            "    \"{1}\", \"{2}\", ::rm::reflect::TypeKind::{3}, sizeof({4}),\n"
            "    {5},\n"
            "    [](void* p) {{ new (p) {4}(); }},\n"
            "    [](void* p) {{ std::destroy_at(static_cast<{4}*>(p)); }},\n"
            "    [](void* d, const void* s) {{ *static_cast<{4}*>(d) = *static_cast<const {4}*>(s); }},\n"
            "    []() -> const std::type_info& {{ return typeid({4}); }},\n"
            "    \"{6}\",\n"
            "}};\n",
            id, q, s.label.empty() ? s.name : s.label, kind, cpp,
            props > 0 ? "T_" + id + "_fields" : std::string("std::span<const ::rm::reflect::FieldDesc>{}"), h.includePath);
        o += "}  // namespace rmgen_generated\n\n";
    }
    return o;
}

std::string EmitAll(const std::vector<Header>& all) {
    std::string o = "// rmgen が作りました: このプログラムの反映する型の一覧。手で直さないでください。\n\n";
    o += "#include \"core/reflect/Reflect.h\"\n\n";
    std::vector<std::string> types, enums;
    for (const auto& h : all) {
        for (const auto& s : h.structs) types.push_back(Id(s.Qualified()));
        for (const auto& e : h.enums) enums.push_back(Id(e.Qualified()));
    }
    o += "namespace rmgen_generated {\n";
    for (const auto& t : types) o += std::format("extern const ::rm::reflect::TypeDesc T_{};\n", t);
    for (const auto& e : enums) o += std::format("extern const ::rm::reflect::EnumDesc E_{};\n", e);
    o += "}  // namespace rmgen_generated\n\n";
    o += "namespace rm::reflect {\n\n";
    o += "std::span<const TypeDesc* const> AllTypes() {\n";
    if (types.empty()) {
        o += "    return {};\n";
    } else {
        o += "    static const TypeDesc* const kTypes[] = {\n";
        for (const auto& t : types) o += std::format("        &rmgen_generated::T_{},\n", t);
        o += "    };\n    return kTypes;\n";
    }
    o += "}\n\n";
    o += "std::span<const EnumDesc* const> AllEnums() {\n";
    if (enums.empty()) {
        o += "    return {};\n";
    } else {
        o += "    static const EnumDesc* const kEnums[] = {\n";
        for (const auto& e : enums) o += std::format("        &rmgen_generated::E_{},\n", e);
        o += "    };\n    return kEnums;\n";
    }
    o += "}\n\n}  // namespace rm::reflect\n";
    return o;
}

}  // namespace rmgen
