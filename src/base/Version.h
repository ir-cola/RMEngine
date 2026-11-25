#pragma once

namespace rm {

// エンジンの版はここだけに書く (下の 2 つの形はこれから作る)。機能を 1 つ作り終えるか、バグを
// 数個直したら 0.0.1 上げ、resources/updates.json に同じ版の欄を足す (2026-11-21 の決まり)。
#define RM_ENGINE_VERSION "0.0.18"
#define RM_ENGINE_NAME "RM Engine"

inline constexpr const char* kEngineName = RM_ENGINE_NAME;
inline constexpr const char* kEngineVersion = RM_ENGINE_VERSION;
inline constexpr const wchar_t* kEngineNameW = L"" RM_ENGINE_NAME;
inline constexpr const wchar_t* kEngineVersionW = L"" RM_ENGINE_VERSION;

}  // namespace rm
