// stb_vorbis compiled once, here (third-party C: its warnings are not ours - /W4 with zero warnings
// holds for the engine's own code).

#pragma warning(push, 0)
#pragma warning(disable : 4244 4245 4456 4457 4701 4702 4996 6001 6011 6246 6262 6385 6386)
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "rm_engine/audio/runtime/third_party/stb_vorbis.c"
#pragma warning(pop)
