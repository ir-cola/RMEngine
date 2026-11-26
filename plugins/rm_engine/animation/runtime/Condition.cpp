#include "rm_engine/animation/runtime/Condition.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

namespace rm::animation {

namespace {

enum class Tok { End, Number, Name, LParen, RParen, Comma, Not, And, Or, Lt, Le, Gt, Ge, Eq, Ne, Plus, Minus, Star, Slash, Bad };

struct Token {
    Tok kind = Tok::End;
    std::string text;
    double number = 0;
    size_t at = 0;  // バイトの位置
};

bool NameStart(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }
bool NamePart(unsigned char c) { return NameStart(c) || (c >= '0' && c <= '9'); }

// 何文字目か (UTF-8 の文字の数で、1 から)。
size_t ColumnOf(std::string_view text, size_t byte) {
    size_t column = 1;
    for (size_t i = 0; i < byte && i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++column;
    }
    return column;
}

std::vector<Token> Lex(std::string_view s, Token& bad) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        Token t;
        t.at = i;
        if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9')) {
            size_t j = i;
            while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.')) ++j;
            t.kind = Tok::Number;
            t.text = std::string(s.substr(i, j - i));
            char* end = nullptr;
            t.number = std::strtod(t.text.c_str(), &end);
            if (!end || *end != '\0') {
                bad = t;
                bad.kind = Tok::Bad;
                return {};
            }
            i = j;
        } else if (NameStart(c)) {
            size_t j = i;
            while (j < s.size() && NamePart(static_cast<unsigned char>(s[j]))) ++j;
            t.kind = Tok::Name;
            t.text = std::string(s.substr(i, j - i));
            i = j;
        } else {
            const auto two = [&](char a, char b) { return i + 1 < s.size() && s[i] == a && s[i + 1] == b; };
            if (two('&', '&')) t.kind = Tok::And, i += 2;
            else if (two('|', '|')) t.kind = Tok::Or, i += 2;
            else if (two('<', '=')) t.kind = Tok::Le, i += 2;
            else if (two('>', '=')) t.kind = Tok::Ge, i += 2;
            else if (two('=', '=')) t.kind = Tok::Eq, i += 2;
            else if (two('!', '=')) t.kind = Tok::Ne, i += 2;
            else {
                switch (c) {
                    case '(': t.kind = Tok::LParen; break;
                    case ')': t.kind = Tok::RParen; break;
                    case ',': t.kind = Tok::Comma; break;
                    case '!': t.kind = Tok::Not; break;
                    case '<': t.kind = Tok::Lt; break;
                    case '>': t.kind = Tok::Gt; break;
                    case '+': t.kind = Tok::Plus; break;
                    case '-': t.kind = Tok::Minus; break;
                    case '*': t.kind = Tok::Star; break;
                    case '/': t.kind = Tok::Slash; break;
                    default:
                        bad = t;
                        bad.kind = Tok::Bad;
                        bad.text = std::string(1, static_cast<char>(c));
                        return {};
                }
                ++i;
            }
        }
        out.push_back(std::move(t));
    }
    Token end;
    end.kind = Tok::End;
    end.at = s.size();
    out.push_back(end);
    return out;
}

}  // namespace

// 再帰で下りる解き方 (強さの低いものから)。
class ConditionParser {
public:
    ConditionParser(Condition& c, std::string_view text, std::vector<Token> tokens) : c_(c), text_(text), toks_(std::move(tokens)) {}

    void Run() {
        const int root = Or();
        if (failed_) return;
        if (Peek().kind != Tok::End) {
            Fail(Peek().at, "ここで式が終わるはずです");
            return;
        }
        c_.root_ = root;
    }

private:
    const Token& Peek() const { return toks_[std::min(pos_, toks_.size() - 1)]; }
    const Token& Next() { return toks_[std::min(pos_++, toks_.size() - 1)]; }
    bool Accept(Tok k) {
        if (Peek().kind != k) return false;
        ++pos_;
        return true;
    }
    int Add(Condition::Node n) {
        c_.nodes_.push_back(n);
        return static_cast<int>(c_.nodes_.size()) - 1;
    }
    int Binary(Condition::Op op, int a, int b) { return Add({op, 0, -1, a, b}); }
    void Fail(size_t byte, std::string why) {
        if (failed_) return;
        failed_ = true;
        c_.error_ = std::move(why);
        c_.column_ = ColumnOf(text_, byte);
    }

    int Or() {
        int a = And();
        while (!failed_ && Accept(Tok::Or)) a = Binary(Condition::Op::Or, a, And());
        return a;
    }
    int And() {
        int a = Equality();
        while (!failed_ && Accept(Tok::And)) a = Binary(Condition::Op::And, a, Equality());
        return a;
    }
    int Equality() {
        int a = Compare();
        while (!failed_) {
            if (Accept(Tok::Eq)) a = Binary(Condition::Op::Eq, a, Compare());
            else if (Accept(Tok::Ne)) a = Binary(Condition::Op::Ne, a, Compare());
            else break;
        }
        return a;
    }
    int Compare() {
        int a = Sum();
        while (!failed_) {
            if (Accept(Tok::Lt)) a = Binary(Condition::Op::Lt, a, Sum());
            else if (Accept(Tok::Le)) a = Binary(Condition::Op::Le, a, Sum());
            else if (Accept(Tok::Gt)) a = Binary(Condition::Op::Gt, a, Sum());
            else if (Accept(Tok::Ge)) a = Binary(Condition::Op::Ge, a, Sum());
            else break;
        }
        return a;
    }
    int Sum() {
        int a = Product();
        while (!failed_) {
            if (Accept(Tok::Plus)) a = Binary(Condition::Op::Add, a, Product());
            else if (Accept(Tok::Minus)) a = Binary(Condition::Op::Sub, a, Product());
            else break;
        }
        return a;
    }
    int Product() {
        int a = Unary();
        while (!failed_) {
            if (Accept(Tok::Star)) a = Binary(Condition::Op::Mul, a, Unary());
            else if (Accept(Tok::Slash)) a = Binary(Condition::Op::Div, a, Unary());
            else break;
        }
        return a;
    }
    int Unary() {
        if (Accept(Tok::Not)) return Add({Condition::Op::Not, 0, -1, Unary(), -1});
        if (Accept(Tok::Minus)) return Add({Condition::Op::Neg, 0, -1, Unary(), -1});
        return Primary();
    }
    int Primary() {
        if (failed_) return -1;
        const Token& t = Peek();
        switch (t.kind) {
            case Tok::Number: {
                ++pos_;
                return Add({Condition::Op::Number, t.number, -1, -1, -1});
            }
            case Tok::LParen: {
                ++pos_;
                const int inner = Or();
                if (!failed_ && !Accept(Tok::RParen)) Fail(Peek().at, "「)」が足りません");
                return inner;
            }
            case Tok::Name: {
                ++pos_;
                if (t.text == "true") return Add({Condition::Op::Number, 1, -1, -1, -1});
                if (t.text == "false") return Add({Condition::Op::Number, 0, -1, -1, -1});
                if (Peek().kind == Tok::LParen) return Call(t);
                const auto it = std::find(c_.names_.begin(), c_.names_.end(), t.text);
                int index = static_cast<int>(it - c_.names_.begin());
                if (it == c_.names_.end()) c_.names_.push_back(t.text);
                return Add({Condition::Op::Name, 0, index, -1, -1});
            }
            case Tok::End: Fail(t.at, "式が途中で終わっています"); return -1;
            default: Fail(t.at, "ここに値 (名前・数・「(」) がくるはずです"); return -1;
        }
    }
    int Call(const Token& name) {
        Condition::Op op;
        int args;
        if (name.text == "abs") op = Condition::Op::Abs, args = 1;
        else if (name.text == "min") op = Condition::Op::Min, args = 2;
        else if (name.text == "max") op = Condition::Op::Max, args = 2;
        else {
            Fail(name.at, std::format("「{}」という関数はありません (abs・min・max が使えます)", name.text));
            return -1;
        }
        Accept(Tok::LParen);
        const int a = Or();
        int b = -1;
        if (!failed_ && args == 2) {
            if (!Accept(Tok::Comma)) {
                Fail(Peek().at, std::format("{} は 2 つの値をとります (「,」が足りません)", name.text));
                return -1;
            }
            b = Or();
        }
        if (!failed_ && !Accept(Tok::RParen)) Fail(Peek().at, "「)」が足りません");
        return Add({op, 0, -1, a, b});
    }

    Condition& c_;
    std::string_view text_;
    std::vector<Token> toks_;
    size_t pos_ = 0;
    bool failed_ = false;
};

Condition Condition::Parse(std::string_view text) {
    Condition c;
    Token bad;
    std::vector<Token> tokens = Lex(text, bad);
    if (tokens.empty()) {
        c.error_ = bad.text.empty() ? std::string("読めない文字があります") : std::format("「{}」は使えません", bad.text);
        c.column_ = ColumnOf(text, bad.at);
        return c;
    }
    if (tokens.size() == 1) return c;  // (空: いつも真)
    ConditionParser p(c, text, std::move(tokens));
    p.Run();
    if (!c.error_.empty()) {
        c.nodes_.clear();
        c.root_ = -1;
    }
    return c;
}

double Condition::Evaluate(const std::function<std::optional<double>(std::string_view)>& lookup, std::vector<std::string>* unknown) const {
    if (!ok()) return 0.0;
    if (root_ < 0) return 1.0;
    return Eval(root_, lookup, unknown);
}

double Condition::Eval(int i, const std::function<std::optional<double>(std::string_view)>& lookup, std::vector<std::string>* unknown) const {
    if (i < 0 || static_cast<size_t>(i) >= nodes_.size()) return 0.0;
    const Node& n = nodes_[static_cast<size_t>(i)];
    auto A = [&] { return Eval(n.a, lookup, unknown); };
    auto B = [&] { return Eval(n.b, lookup, unknown); };
    auto truth = [](double v) { return v != 0.0 ? 1.0 : 0.0; };
    switch (n.op) {
        case Op::Number: return n.number;
        case Op::Name: {
            const std::string& name = names_[static_cast<size_t>(n.name)];
            if (const auto v = lookup ? lookup(name) : std::nullopt) return std::isfinite(*v) ? *v : 0.0;
            if (unknown && std::find(unknown->begin(), unknown->end(), name) == unknown->end()) unknown->push_back(name);
            return 0.0;
        }
        case Op::Neg: return -A();
        case Op::Not: return A() != 0.0 ? 0.0 : 1.0;
        case Op::Add: return A() + B();
        case Op::Sub: return A() - B();
        case Op::Mul: return A() * B();
        case Op::Div: {
            const double a = A(), b = B();
            return b == 0.0 ? 0.0 : a / b;
        }
        case Op::Lt: return A() < B() ? 1.0 : 0.0;
        case Op::Le: return A() <= B() ? 1.0 : 0.0;
        case Op::Gt: return A() > B() ? 1.0 : 0.0;
        case Op::Ge: return A() >= B() ? 1.0 : 0.0;
        case Op::Eq: return A() == B() ? 1.0 : 0.0;
        case Op::Ne: return A() != B() ? 1.0 : 0.0;
        case Op::And: return truth(A()) * truth(B());  // (両方を求める: 知らない名前を全部集める)
        case Op::Or: return truth(A()) + truth(B()) > 0 ? 1.0 : 0.0;
        case Op::Abs: return std::fabs(A());
        case Op::Min: return std::min(A(), B());
        case Op::Max: return std::max(A(), B());
    }
    return 0.0;
}

}  // namespace rm::animation
