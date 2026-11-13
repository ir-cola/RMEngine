#pragma once
// ゲーム制作 × 音 - 音源: an entity that plays a sound while the game plays. In a 3D game and a 2D one
// alike (where it is comes from the space's PlaceInfo: game_production/runtime/PlayServices.h).
//
//   始めから鳴らす   when it starts to run (Play, the scene switched to, the entity switched on / spawned)
//   鳴らす先 BGM     the game's music: starting it fades out the music before it (a cross-fade of
//                   フェード seconds) - one music at a time, with rm::Audio::PlayMusic too
//   距離で小さくする  heard from the entity: smaller farther from the game's camera (X3DAudio for left /
//                   right); off: the same everywhere
// It stops when its entity goes (deleted, switched off, the scene switched away) and at ■.
// Its values are read every frame: changed in the インスペクター (or by a script) while it plays, it
// sounds so at once.

#include "core/reflect/Reflect.h"

namespace rm::game_x_audio {

RM_ENUM(Label="鳴らす先")
enum class AudioBus {
    RM_ITEM(Label="効果音") Effects,
    RM_ITEM(Label="BGM (1 つずつ。切り替えるとクロスフェード)") Music,
};

RM_STRUCT(Label="音源")
struct AudioSource {
    RM_PROPERTY(Edit, Label="音", Tooltip="鳴らす音のアセット (WAV / OGG を勧めます。MP3 なども鳴りますが、Media Foundation の無い PC では鳴りません)", AssetKind="audio") AssetRef sound;
    RM_PROPERTY(Edit, Label="音量", Min=0, Max=4, Step=0.01, Tooltip="1 がそのまま。ゲームの設定の音量と、音の取り込みの音量にさらに掛かります") float volume = 1;
    RM_PROPERTY(Edit, Label="ピッチ", Min=0.05, Max=8, Step=0.01, Tooltip="1 がそのまま。2 で 1 オクターブ高く (速く)、0.5 で低く (遅く)") float pitch = 1;
    RM_PROPERTY(Edit, Label="ループ", Tooltip="終わったら始め (音の取り込みのループの点) へ戻って鳴り続けます") bool loop = false;
    RM_PROPERTY(Edit, Label="始めから鳴らす", Tooltip="Play が始まったとき (シーンが切り替わって来たとき・エンティティが有効になったとき) に鳴らします。外すとスクリプトの rm::AudioSource::Play で鳴らします") bool playOnStart = true;
    RM_PROPERTY(Edit, Label="鳴らす先") AudioBus bus = AudioBus::Effects;
    RM_PROPERTY(Edit, Label="フェード (秒)", Min=0, Max=30, Step=0.1, Tooltip="鳴り始めと止めるときに、この秒数をかけて大きく・小さくします。BGM では前の BGM とのクロスフェードの長さ") float fade = 0;
    RM_PROPERTY(Edit, Label="距離で小さくする", Tooltip="エンティティの位置から聞こえ、ゲームのカメラから離れるほど小さく、左右にも分かれて聞こえます") bool spatial = false;
    RM_PROPERTY(Edit, Label="そのままの大きさの距離", Min=0, Step=0.1, Tooltip="カメラからこの距離までは小さくなりません") float minDistance = 1;
    RM_PROPERTY(Edit, Label="聞こえなくなる距離", Min=0, Step=0.1, Tooltip="カメラからこの距離で音が 0 になります") float maxDistance = 30;
};

}  // namespace rm::game_x_audio
