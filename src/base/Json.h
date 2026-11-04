#pragma once
// Small JSON DOM (pure). Objects keep key order, so a document that is read,
// partly changed and written back keeps every key it did not touch, in place.
// This is how ".rmpf" and the other engine files pass unknown keys through
// instead of dropping them (handoff memo 3-5: "unknown keys are kept and written back").

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    // Empty string when the value is not a string.
    const std::string& AsString() const;

    // Array / object element count (0 for scalars).
    size_t Size() const;

    // --- Array ---
    const Value& At(size_t i) const { return arr_[i]; }
    Value& At(size_t i) { return arr_[i]; }
    void Push(Value v);  // turns null into an array
    const std::vector<Value>& Items() const { return arr_; }
    std::vector<Value>& Items() { return arr_; }

    // --- Object (ordered) ---
    const Value* Find(std::string_view key) const;
    Value* Find(std::string_view key);
    const Value& Get(std::string_view key) const;  // null value when missing
    Value& Set(std::string_view key, Value v);     // replaces in place, else appends; null -> object
    bool Erase(std::string_view key);
    const std::vector<Member>& Members() const { return obj_; }
    std::vector<Member>& Members() { return obj_; }

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
};

struct ParseError {
    size_t offset = 0;
    size_t line = 1;    // 1-based
    size_t column = 1;  // 1-based, in bytes
    std::string message;  // Japanese, UTF-8
};

// Accepts an optional UTF-8 BOM. Duplicate keys: the last value wins (position of the first).
std::optional<Value> Parse(std::string_view text, ParseError* error = nullptr);

// pretty = 2-space indent and a trailing newline. Output is deterministic,
// so Parse -> Write -> Parse -> Write is byte-identical.
std::string Write(const Value& v, bool pretty = true);

}  // namespace rm::json
