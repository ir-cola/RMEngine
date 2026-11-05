#pragma once
// A value of a reflected type chosen at run time (the type is known only by its TypeDesc), e.g.
// one component of an entity. It owns the value: made with the type's default member values,
// copied with the type's copy, destroyed with its destructor.
//
//   reflect::Object o(reflect::TypeOf<Transform>());
//   static_cast<Transform*>(o.Get())->position = {1, 2, 3};

#include "core/reflect/Reflect.h"

#include <string_view>

namespace rm::reflect {

class Object {
public:
    Object() = default;
    explicit Object(const TypeDesc& type);
    Object(const Object& o);
    Object(Object&& o) noexcept;
    Object& operator=(const Object& o);
    Object& operator=(Object&& o) noexcept;
    ~Object();

    bool Empty() const { return type_ == nullptr; }
    const TypeDesc* Type() const { return type_; }
    void* Get() { return data_; }
    const void* Get() const { return data_; }

private:
    void Reset();

    const TypeDesc* type_ = nullptr;
    void* data_ = nullptr;
};

// The type with this C++ name (TypeDesc::name, e.g. "rm::space3d::Transform") among `types`.
const TypeDesc* FindTypeNamed(std::span<const TypeDesc* const> types, std::string_view name);

}  // namespace rm::reflect
