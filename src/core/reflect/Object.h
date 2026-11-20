#pragma once
// 実行時に選ぶリフレクションの型の値 (型は TypeDesc でしか分からない)。たとえば
// エンティティの部品の 1 つ。値を所有する: 型の既定のメンバー値で作り、
// 型のコピーでコピーし、デストラクターで壊す。
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

// この C++ 名 (TypeDesc::name、たとえば "rm::space3d::Transform") を持つ型を `types` の中から。
const TypeDesc* FindTypeNamed(std::span<const TypeDesc* const> types, std::string_view name);

}  // namespace rm::reflect
