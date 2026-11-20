/* RM Engine - スクリプト API の音の部分 (ゲーム制作 × 音 が提供する)。RmAbi.h と同じ素の C。
 * スクリプトはこれでなく RMAudio.h を include する。変えたら RM_PART_AUDIO_VERSION を上げる。 */
#pragma once

#include <stdint.h>

#define RM_PART_AUDIO "rm_engine.game_x_audio.audio"
#define RM_PART_AUDIO_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* guid: 音のアセットの id (rm::SoundRef::guid)。声: 鳴っている 1 つの音 (0: なし - その音が
   ない、またはゲームに音がない)。bus: 0 = 全体、1 = BGM、2 = 効果音。 */
typedef struct RmAudioApi {
    /* 効果音を 1 回。position が null: どこでも同じに聞こえる。そうでなければそこから (遠いほど小さい)。 */
    uint64_t (*play)(void* ctx, const char* guid, float volume, float pitch, const float* position);
    void (*stop)(void* ctx, uint64_t voice, float fadeSeconds);
    int (*playing)(void* ctx, uint64_t voice);
    /* ゲームの音楽 (1 度に 1 つ、ループ): 前のものがフェードアウトし、これがフェードインする。
       同じ音がもう音楽として鳴っていれば、そのまま続く。 */
    void (*playMusic)(void* ctx, const char* guid, float volume, float fadeSeconds);
    void (*stopMusic)(void* ctx, float fadeSeconds);
    void (*setVolume)(void* ctx, int bus, float volume);
    float (*volume)(void* ctx, int bus);
    /* エンティティの (最初の) 音源。op: 0 再生 (始めから)、1 停止、2 一時停止、3 再開。0: 持っていない。 */
    int (*source)(void* ctx, uint64_t entity, int op);
    /* what: 0 = 音量、1 = ピッチ。0: 持っていない。 */
    int (*sourceSet)(void* ctx, uint64_t entity, int what, float value);
    int (*sourcePlaying)(void* ctx, uint64_t entity);
} RmAudioApi;

#ifdef __cplusplus
}
#endif
