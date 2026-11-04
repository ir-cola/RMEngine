#include "Test.h"

#include <cstdio>

namespace rmtest {

namespace {
int g_checks = 0;
int g_failures = 0;
const char* g_current = "";
}  // namespace

std::vector<Case>& Registry() {
    static std::vector<Case> cases;
    return cases;
}

void Count() { ++g_checks; }

void Fail(const char* file, int line, const char* expr) {
    ++g_failures;
    std::printf("  FAIL [%s] %s(%d): %s\n", g_current, file, line, expr);
}

}  // namespace rmtest

int main() {
    using namespace rmtest;
    for (const auto& c : Registry()) {
        g_current = c.name;
        const int before = g_failures;
        c.fn();
        std::printf("%-40s %s\n", c.name, g_failures == before ? "ok" : "FAILED");
    }
    std::printf("\n%zu tests, %d checks, %d failures\n", Registry().size(), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
