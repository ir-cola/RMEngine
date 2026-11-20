#include "core/reflect/Object.h"

#include <new>
#include <utility>

namespace rm::reflect {

// メモリーは既定の境界合わせ (x64 では 16 バイト) で new する。リフレクションが対応する型には
// どれも十分 (境界合わせが過大なメンバーはない)。
Object::Object(const TypeDesc& type) : type_(&type) {
    data_ = ::operator new(type.size);
    type.construct(data_);
}

Object::Object(const Object& o) {
    if (!o.type_) return;
    type_ = o.type_;
    data_ = ::operator new(type_->size);
    type_->construct(data_);
    type_->copy(data_, o.data_);
}

Object::Object(Object&& o) noexcept : type_(std::exchange(o.type_, nullptr)), data_(std::exchange(o.data_, nullptr)) {}

Object& Object::operator=(const Object& o) {
    if (this != &o) {
        Object copy(o);
        *this = std::move(copy);
    }
    return *this;
}

Object& Object::operator=(Object&& o) noexcept {
    if (this != &o) {
        Reset();
        type_ = std::exchange(o.type_, nullptr);
        data_ = std::exchange(o.data_, nullptr);
    }
    return *this;
}

Object::~Object() { Reset(); }

void Object::Reset() {
    if (type_) {
        type_->destruct(data_);
        ::operator delete(data_);
    }
    type_ = nullptr;
    data_ = nullptr;
}

const TypeDesc* FindTypeNamed(std::span<const TypeDesc* const> types, std::string_view name) {
    for (const TypeDesc* t : types) {
        if (t && name == t->name) return t;
    }
    return nullptr;
}

}  // namespace rm::reflect
