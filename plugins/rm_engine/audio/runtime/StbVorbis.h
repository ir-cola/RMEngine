#pragma once
// stb_vorbis (runtime/third_party/stb_vorbis.c、v1.24 - パブリックドメインまたは MIT、Sean Barrett
// 作: ライセンスはそのファイルの末尾) - その宣言と、コンパイルするときの設定 (StbVorbis.cpp):
// メモリからだけ (FILE* も push API も無し)。

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "rm_engine/audio/runtime/third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
