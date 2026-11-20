#pragma once
// プラットフォーム層で <windows.h> を include する唯一の場所。
// NOMINMAX / WIN32_LEAN_AND_MEAN / UNICODE はプロジェクトでも設定している。

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
