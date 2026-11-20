#pragma once
// 小さな JSON の DOM (純粋)。オブジェクトはキーの順番を保つので、読んで一部だけ変えて
// 書き戻した文書は、触っていないキーを全部そのままの位置に残す。
// ".rmpf" などのエンジンのファイルが、知らないキーを捨てずに通せるのはこのため
// (引き継ぎメモ 3-5: 「知らないキーは残して書き戻す」)。

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rm::json {

enum class Type : uint8_t { Null, Bool, Int, Double, String, Array, Object };

class Value {
public:
    using Member = std::pair<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), b_(b) {}
    Value(int v) : type_(Type::Int), i_(v) {}
    Value(unsigned v) : type_(Type::Int), i_(v) {}
    Value(int64_t v) : type_(Type::Int), i_(v) {}
    Value(uint64_t v) : type_(Type::Int), i_(static_cast<int64_t>(v)) {}
    Value(double v) : type_(Type::Double), d_(v) {}
    Value(const char* s) : type_(Type::String), s_(s) {}
    Value(std::string s) : type_(Type::String), s_(std::move(s)) {}
    Value(std::string_view s) : type_(Type::String), s_(s) {}

    static Value MakeArray();
    static Value MakeObject();

    Type GetType() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Int || type_ == Type::Double; }
    bool IsInt() const { return type_ == Type::Int; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool(bool def = false) const { return type_ == Type::Bool ? b_ : def; }
    int64_t AsInt(int64_t def = 0) const;
    double AsDouble(double def = 0.0) const;
    // 値が文字列でないときは空文字列。
    const std::string& AsString() const;

    // 配列 / オブジェクトの要素数 (スカラーは 0)。
    size_t Size() const;

    // --- 配列 ---
    const Value& At(size_t i) const { return arr_[i]; }
    Value& At(size_t i) { return arr_[i]; }
    void Push(Value v);  // null は配列になる
    const std::vector<Value>& Items() const { return arr_; }
    std::vector<Value>& Items() { return arr_; }

    // --- オブジェクト (順番あり) ---
    // 大きなオブジェクト (kIndexFrom 個以上のキー) は、初めて必要になったときに作る索引でキーを探す。
    // キーの多いものを読んだり埋めたりしても、時間は直線的に増える (大きさの 2 乗にはならない)。
    static constexpr size_t kIndexFrom = 16;
    const Value* Find(std::string_view key) const;
    Value* Find(std::string_view key);
    const Value& Get(std::string_view key) const;  // なければ null の値
    Value& Set(std::string_view key, Value v);     // その場で置き換え、なければ末尾に足す。null ならオブジェクトになる
    bool Erase(std::string_view key);
    const std::vector<Member>& Members() const { return obj_; }
    // (これを通して一覧を変えてもよい: 索引は次に必要になったとき作り直す)
    std::vector<Member>& Members() {
        index_.built = false;
        return obj_;
    }

    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const { return !(*this == o); }

private:
    Type type_ = Type::Null;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
    std::vector<Value> arr_;
    std::vector<Member> obj_;

    // 大きなオブジェクトの各キーがどこにあるか。ただのキャッシュ: Value をコピー・ムーブしても、
    // 索引なしで始まる。オブジェクトのキーの数が作ったときと同じ間だけ信用し、使うたびに
    // そこにあるキーと照らして確かめる (Members() が一覧を変えたかもしれない)。
    struct KeyHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
    };
    struct KeyIndex {
        // 大きなオブジェクトのときだけ確保する (空の std::unordered_map も MSVC では確保してしまい、
        // どの Value - 数や文字列 - も KeyIndex を持っている)。
        std::unique_ptr<std::unordered_map<std::string, size_t, KeyHash, std::equal_to<>>> at;
        size_t size = 0;
        bool built = false;
        KeyIndex() = default;
        KeyIndex(const KeyIndex&) {}
        KeyIndex(KeyIndex&&) noexcept {}
        KeyIndex& operator=(const KeyIndex&) {
            Forget();
            return *this;
        }
        KeyIndex& operator=(KeyIndex&&) noexcept {
            Forget();
            return *this;
        }
        void Forget() noexcept {
            built = false;
            at.reset();
        }
    };
    mutable KeyIndex index_;
    static constexpr size_t kNotFound = static_cast<size_t>(-1);
    size_t IndexOf(std::string_view key) const;  // なければ kNotFound
};

struct ParseError {
    size_t offset = 0;
    size_t line = 1;    // 1 始まり
    size_t column = 1;  // 1 始まり、バイト単位
    std::string message;  // 日本語、UTF-8
};

// 先頭の UTF-8 BOM はあってもよい。キーが重複したら、最後の値が勝つ (位置は最初のもの)。
std::optional<Value> Parse(std::string_view text, ParseError* error = nullptr);

// pretty = 2 文字の字下げと末尾の改行。出力は決まった形になるので、
// Parse -> Write -> Parse -> Write は一字一句同じになる。
std::string Write(const Value& v, bool pretty = true);

}  // namespace rm::json
