#include "rm_engine/game_x_audio/runtime/AudioService.h"

#include "core/reflect/Data.h"
#include "rm_engine/audio/runtime/Sound.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_audio/runtime/SoundPack.h"
#include "rm_engine/game_x_audio/runtime/sdk/RMAudioAbi.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>

namespace rm::game_x_audio {

AudioService::Facts AudioService::DebugFacts() const {
    Facts f;
    f.device = engine_ && engine_->HasDevice();
    f.voices = engine_ ? engine_->Voices() : 0;
    f.sources = sources_.size();
    if (music_ != 0 && engine_ && engine_->IsPlaying(music_)) {
        const AssetRef ref{musicSound_};
        f.music = host_.name ? host_.name(ref) : std::string();
        if (f.music.empty()) f.music = musicSound_;
    }
    f.master = Volume(BusVolume::Master);
    f.musicVolume = Volume(BusVolume::Music);
    f.effects = Volume(BusVolume::Effects);
    return f;
}

namespace gp = game_production;

AudioService::AudioService(const gp::PlayHost& host) : host_(host), engine_(std::make_unique<audio::AudioEngine>(host.silent)) {
    if (!host.silent && !engine_->HasDevice()) Tell(1, "音: 音の出る機器が見つからないので、音は鳴りません (ゲームはそのまま動きます)");
}

AudioService::~AudioService() { engine_.reset(); }  // (every voice stops, the device closes)

void AudioService::Tell(int level, const std::string& text) {
    if (host_.log) host_.log(level, text);
}

const audio::SoundSource* AudioService::Sound(const AssetRef& ref) {
    if (ref.Empty()) return nullptr;
    if (const auto it = sounds_.find(ref.guid); it != sounds_.end()) return it->second ? &*it->second : nullptr;
    std::optional<audio::SoundSource> made;
    std::string name = host_.name ? host_.name(ref) : std::string();
    if (name.empty()) name = ref.guid;
    if (auto bytes = host_.asset ? host_.asset(ref) : std::nullopt) {
        audio::SoundSource s;
        json::Value import;
        if (const auto pack = UnpackSound(*bytes)) {  // the game's: baked with its import settings
            if (!pack->name.empty()) name = pack->name;
            import = pack->import;
            s.bytes = std::make_shared<const std::string>(pack->file);
        } else {
            import = host_.import ? host_.import(ref) : json::Value::MakeObject();
            s.bytes = std::make_shared<const std::string>(std::move(*bytes));
        }
        asset::SoundImport settings;
        reflect::Load(settings, import);
        audio::ApplyImport(s, settings);
        s.key = ref.guid;
        s.name = name;
        made = std::move(s);
        Tell(0, std::format("音: 「{}」を鳴らします", name));
    } else {
        Tell(1, std::format("音: 「{}」がありません (アセットが消えたか、書き出しに入っていません)", name));
    }
    const auto& slot = sounds_.emplace(ref.guid, std::move(made)).first->second;
    return slot ? &*slot : nullptr;
}

void AudioService::Frame(const gp::PlayFrame& f) {
    // The volumes: the game settings' (edited while playing: heard at once), or a script's.
    engine_->SetVolumes(Volume(BusVolume::Master), Volume(BusVolume::Music), Volume(BusVolume::Effects));
    // Who hears: the game's camera.
    gp::Pose ear;
    if (f.camera != 0 && f.camera <= static_cast<uint64_t>(std::numeric_limits<int>::max()) && host_.registry &&
        gp::EntityPose(*host_.registry, f.scene, static_cast<int>(f.camera), ear)) {
        engine_->SetListener(ear.position, ear.forward, ear.up);
    }
    // The 音源 components of the entities that run: new ones start, the others follow their values.
    std::unordered_map<SourceKey, Source, KeyHash> seen;
    seen.reserve(sources_.size());
    gp::ForEachComponent(f.scene, reflect::TypeOf<AudioSource>().name, [&](gp::SceneEntity& e, gp::Component& c) {
        if (!c.Known()) return;
        const auto* a = static_cast<const AudioSource*>(c.value.Get());
        const SourceKey key{a, e.data.id};
        auto old = sources_.find(key);
        Source s;
        bool fresh = old == sources_.end();
        if (!fresh) {
            s = old->second;
            sources_.erase(old);
        }
        s.entity = e.data.id;
        if (fresh) {
            if (a->playOnStart) StartSource(*a, s, f.scene);
        } else if (s.voice != 0) {
            if (!engine_->IsPlaying(s.voice)) {
                s.voice = 0;  // it ended
            } else {
                engine_->SetVolume(s.voice, a->volume);
                engine_->SetPitch(s.voice, a->pitch);
                gp::Pose at;
                if (a->spatial && host_.registry && gp::EntityPose(*host_.registry, f.scene, e.data.id, at)) engine_->SetPosition(s.voice, at.position);
            }
        }
        seen.emplace(key, s);
    });
    // The ones that went (deleted, switched off): stopped with their フェード.
    for (auto& [key, s] : sources_) StopVoice(s);
    sources_ = std::move(seen);
    // The music of the scene before that no 音源 of this one took over.
    if (musicOrphan_) {
        if (music_ != 0) engine_->Stop(music_, musicFade_);
        music_ = 0;
        musicSound_.clear();
        musicOwner_ = nullptr;
        musicOrphan_ = false;
    }
    if (music_ != 0 && !engine_->IsPlaying(music_)) music_ = 0;
    engine_->Update(f.dt);
    for (const std::string& m : engine_->TakeMessages()) Tell(1, "音: " + m);
}

void AudioService::SetPaused(bool paused) { engine_->PauseAll(paused); }

void AudioService::SceneSwitched() {
    for (auto& [key, s] : sources_) {
        if (s.voice != 0 && s.voice == music_) {
            musicOrphan_ = true;  // (the next Frame: taken over by the same BGM, or stopped)
            musicFade_ = s.fade;
            musicOwner_ = nullptr;
            continue;
        }
        StopVoice(s);
    }
    sources_.clear();
}

AudioService::VoiceId AudioService::StartSource(const AudioSource& c, Source& s, gp::Scene& scene) {
    if (s.voice != 0) {
        const float fade = s.fade;
        s.fade = 0;  // (played again: the one before stops at once)
        StopVoice(s);
        s.fade = fade;
    }
    s.paused = false;
    s.fade = c.fade;
    audio::PlayParams p;
    p.volume = c.volume;
    p.pitch = c.pitch;
    p.loop = c.loop;
    p.fadeIn = c.fade;
    gp::Pose at;
    if (c.spatial && host_.registry && gp::EntityPose(*host_.registry, scene, s.entity, at)) {
        p.spatial = true;
        p.position = at.position;
        p.minDistance = c.minDistance;
        p.maxDistance = c.maxDistance;
    }
    if (c.bus == AudioBus::Music) {
        s.voice = StartMusic(c.sound, p, c.fade, &c);
    } else {
        const audio::SoundSource* sound = Sound(c.sound);
        s.voice = sound ? engine_->Play(*sound, p) : 0;
    }
    return s.voice;
}

AudioService::VoiceId AudioService::StartMusic(const AssetRef& ref, audio::PlayParams params, double fade, const void* owner) {
    // The same sound already playing as the music: it goes on (a scene switched to with the same BGM).
    if (music_ != 0 && !ref.Empty() && musicSound_ == ref.guid && engine_->IsPlaying(music_)) {
        musicOwner_ = owner;
        musicOrphan_ = false;
        musicFade_ = static_cast<float>(fade);
        engine_->SetVolume(music_, params.volume);
        return music_;
    }
    if (music_ != 0) engine_->Stop(music_, fade);  // fades out while the new one fades in
    music_ = 0;
    musicSound_.clear();
    musicOwner_ = nullptr;
    musicOrphan_ = false;
    const audio::SoundSource* sound = Sound(ref);
    if (!sound) return 0;
    params.bus = audio::Bus::Music;
    params.fadeIn = fade;
    music_ = engine_->Play(*sound, params);
    musicSound_ = ref.guid;
    musicOwner_ = owner;
    musicFade_ = static_cast<float>(fade);
    return music_;
}

void AudioService::StopVoice(Source& s) {
    if (s.voice == 0) return;
    engine_->Stop(s.voice, s.fade);
    if (s.voice == music_) {
        music_ = 0;
        musicSound_.clear();
        musicOwner_ = nullptr;
    }
    s.voice = 0;
}

AudioService::VoiceId AudioService::PlayOnce(const AssetRef& ref, float volume, float pitch, const Vec3* position) {
    const audio::SoundSource* sound = Sound(ref);
    if (!sound) return 0;
    audio::PlayParams p;
    p.volume = volume;
    p.pitch = pitch;
    if (position) {
        p.spatial = true;
        p.position = *position;
    }
    return engine_->Play(*sound, p);
}

void AudioService::Stop(VoiceId voice, float fade) {
    if (voice == 0) return;
    engine_->Stop(voice, fade);
    if (voice == music_) {
        music_ = 0;
        musicSound_.clear();
        musicOwner_ = nullptr;
    }
}

bool AudioService::Playing(VoiceId voice) const { return voice != 0 && engine_->IsPlaying(voice); }

void AudioService::PlayMusic(const AssetRef& ref, float volume, float fade) {
    audio::PlayParams p;
    p.volume = volume;
    p.loop = true;
    StartMusic(ref, p, std::max(0.f, fade), nullptr);
}

void AudioService::StopMusic(float fade) {
    if (music_ == 0) return;
    Stop(music_, std::max(0.f, fade));
}

void AudioService::SetVolume(BusVolume bus, float volume) {
    const auto i = static_cast<size_t>(bus);
    if (i < 3) scriptVolume_[i] = std::clamp(volume, 0.f, 2.f);
}

float AudioService::Volume(BusVolume bus) const {
    const auto i = static_cast<size_t>(bus);
    if (i >= 3) return 0;
    if (scriptVolume_[i] >= 0) return scriptVolume_[i];
    const gp::AudioVolumes v = host_.settings ? host_.settings->volume : gp::AudioVolumes{};
    return bus == BusVolume::Master ? v.master : bus == BusVolume::Music ? v.music : v.effects;
}

std::pair<AudioSource*, AudioService::Source*> AudioService::SourceOf(gp::Scene& scene, int entity) {
    gp::SceneEntity* e = scene.Find(entity);
    gp::Component* c = e ? e->FindComponent(reflect::TypeOf<AudioSource>().name) : nullptr;
    if (!c || !c->Known()) return {nullptr, nullptr};
    auto* a = static_cast<AudioSource*>(c->value.Get());
    Source& s = sources_[SourceKey{a, entity}];  // (a new one: started by the script, so not by 始めから鳴らす)
    s.entity = entity;
    return {a, &s};
}

bool AudioService::SourcePlay(gp::Scene& scene, int entity) {
    const auto [a, s] = SourceOf(scene, entity);
    if (!a) return false;
    StartSource(*a, *s, scene);
    return true;
}

bool AudioService::SourceStop(gp::Scene& scene, int entity) {
    const auto [a, s] = SourceOf(scene, entity);
    if (!a) return false;
    StopVoice(*s);
    return true;
}

bool AudioService::SourcePause(gp::Scene& scene, int entity, bool paused) {
    const auto [a, s] = SourceOf(scene, entity);
    if (!a) return false;
    if (s->voice != 0) engine_->SetPaused(s->voice, paused);
    s->paused = paused;
    return true;
}

bool AudioService::SourcePlaying(gp::Scene& scene, int entity) {
    // Only asked: nothing is made (a source made here would no longer be "new" at the next Frame, and
    // its 始めから鳴らす would never start - a script asking in its BeginPlay silenced it).
    gp::SceneEntity* e = scene.Find(entity);
    gp::Component* c = e ? e->FindComponent(reflect::TypeOf<AudioSource>().name) : nullptr;
    if (!c || !c->Known()) return false;
    const auto it = sources_.find(SourceKey{static_cast<AudioSource*>(c->value.Get()), entity});
    return it != sources_.end() && it->second.voice != 0 && engine_->IsPlaying(it->second.voice);
}

// --- The scripts' part (RMAudioAbi.h): RmApi::ctx is game production's ScriptWorld --------------------
namespace {

AudioService* ServiceOf(void* ctx) {
    auto& w = *static_cast<gp::ScriptWorld*>(ctx);
    return w.services ? static_cast<AudioService*>(w.services->Find(kAudioServiceName)) : nullptr;
}

AssetRef RefOf(const char* guid) {
    AssetRef r;
    if (guid) r.guid.assign(guid, strnlen(guid, RM_ASSET_ID_SIZE));
    return r;
}

int EntityId(uint64_t id) { return id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max()) ? 0 : static_cast<int>(id); }

uint64_t Play(void* ctx, const char* guid, float volume, float pitch, const float* position) {
    AudioService* s = ServiceOf(ctx);
    if (!s) return 0;
    const Vec3 p = position ? Vec3{position[0], position[1], position[2]} : Vec3{};
    return s->PlayOnce(RefOf(guid), volume, pitch, position ? &p : nullptr);
}
void StopV(void* ctx, uint64_t voice, float fade) {
    if (AudioService* s = ServiceOf(ctx)) s->Stop(voice, fade);
}
int PlayingV(void* ctx, uint64_t voice) {
    AudioService* s = ServiceOf(ctx);
    return s && s->Playing(voice) ? 1 : 0;
}
void PlayMusicV(void* ctx, const char* guid, float volume, float fade) {
    if (AudioService* s = ServiceOf(ctx)) s->PlayMusic(RefOf(guid), volume, fade);
}
void StopMusicV(void* ctx, float fade) {
    if (AudioService* s = ServiceOf(ctx)) s->StopMusic(fade);
}
void SetVolumeV(void* ctx, int bus, float volume) {
    if (AudioService* s = ServiceOf(ctx); s && bus >= 0 && bus <= 2) s->SetVolume(static_cast<AudioService::BusVolume>(bus), volume);
}
float VolumeV(void* ctx, int bus) {
    AudioService* s = ServiceOf(ctx);
    return s && bus >= 0 && bus <= 2 ? s->Volume(static_cast<AudioService::BusVolume>(bus)) : 0.f;
}
int SourceV(void* ctx, uint64_t entity, int op) {
    auto& w = *static_cast<gp::ScriptWorld*>(ctx);
    AudioService* s = ServiceOf(ctx);
    const int id = EntityId(entity);
    if (!s || !w.scene || id == 0) return 0;
    switch (op) {
        case 0: return s->SourcePlay(*w.scene, id) ? 1 : 0;
        case 1: return s->SourceStop(*w.scene, id) ? 1 : 0;
        case 2: return s->SourcePause(*w.scene, id, true) ? 1 : 0;
        case 3: return s->SourcePause(*w.scene, id, false) ? 1 : 0;
        default: return 0;
    }
}
int SourceSetV(void* ctx, uint64_t entity, int what, float value) {
    auto& w = *static_cast<gp::ScriptWorld*>(ctx);
    const int id = EntityId(entity);
    gp::SceneEntity* e = w.scene && id != 0 ? w.scene->Find(id) : nullptr;
    gp::Component* c = e ? e->FindComponent(reflect::TypeOf<AudioSource>().name) : nullptr;
    if (!c || !c->Known()) return 0;
    auto* a = static_cast<AudioSource*>(c->value.Get());  // (heard at the end of the frame: Frame follows the values)
    if (what == 0) a->volume = std::clamp(value, 0.f, 4.f);
    if (what == 1) a->pitch = audio::ClampPitch(value);
    return 1;
}
int SourcePlayingV(void* ctx, uint64_t entity) {
    auto& w = *static_cast<gp::ScriptWorld*>(ctx);
    AudioService* s = ServiceOf(ctx);
    const int id = EntityId(entity);
    return s && w.scene && id != 0 && s->SourcePlaying(*w.scene, id) ? 1 : 0;
}

constexpr RmAudioApi kTable{&Play, &StopV, &PlayingV, &PlayMusicV, &StopMusicV, &SetVolumeV, &VolumeV, &SourceV, &SourceSetV, &SourcePlayingV};

}  // namespace

const gp::ScriptApiPart& ScriptAudioPart() {
    static const gp::ScriptApiPart part{RM_PART_AUDIO, RM_PART_AUDIO_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_audio
