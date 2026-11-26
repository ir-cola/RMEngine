#include "Test.h"

#include <cstdio>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

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
#ifdef _DEBUG
    // Debug の CRT の確認 (範囲外の添字など) は、窓を出さずに文を標準エラーに書く (2026-11-26: 画面の無い所で
    // 走らせると、何で止まったか分からないまま落ちていた)。
    for (const int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
    for (const auto& c : Registry()) {
        g_current = c.name;
        const int before = g_failures;
        c.fn();
        std::printf("%-40s %s\n", c.name, g_failures == before ? "ok" : "FAILED");
        std::fflush(stdout);  // (途中で落ちたとき、最後に出た行が本当に最後に終わったテスト)
    }
    std::printf("\n%zu tests, %d checks, %d failures\n", Registry().size(), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
