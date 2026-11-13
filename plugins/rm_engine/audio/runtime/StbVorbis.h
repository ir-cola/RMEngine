#pragma once
// stb_vorbis (runtime/third_party/stb_vorbis.c, v1.24 - public domain or MIT, by Sean Barrett: the
// licence is at the end of that file) - its declarations, with the settings it is compiled with
// (StbVorbis.cpp): from memory only (no FILE*, no push API).

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "rm_engine/audio/runtime/third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
