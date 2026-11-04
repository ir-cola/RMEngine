#pragma once
// Minimal test harness: RM_TEST registers a case, CHECK records a failure and continues.
// The exe returns non-zero when anything failed, so CI and tools/build.ps1 can rely on the exit code.

#include <vector>

namespace rmtest {

struct Case {
    const char* name;
    void (*fn)();
};

std::vector<Case>& Registry();
void Fail(const char* file, int line, const char* expr);
void Count();

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

}  // namespace rmtest

#define RM_TEST(name)                                             \
    static void name();                                           \
    static ::rmtest::Registrar name##_registrar(#name, &name);    \
    static void name()

#define CHECK(expr)                                               \
    do {                                                          \
        ::rmtest::Count();                                        \
        if (!(expr)) ::rmtest::Fail(__FILE__, __LINE__, #expr);   \
    } while (0)
