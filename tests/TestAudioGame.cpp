// Tests for ゲーム制作 × 音 (plugins/rm_engine/game_x_audio): the exported game's .rmsound, the 音源
// component saved and read back, the sounds of a Play (silent: no device) - a 音源 starts and stops with
// its entity, the music cross-fades and goes on into a scene with the same BGM, ■ stops everything -
// the volumes, the scripts' part, and where an entity is heard from (the 3D space's PlaceInfo).

#include "Test.h"

#include "core/RuntimeRegistry.h"
#include "core/reflect/Data.h"
#include "rm_engine/audio/runtime/Sound.h"
#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/game_x_audio/runtime/AudioService.h"
#include "rm_engine/game_x_audio/runtime/AudioSource.h"
#include "rm_engine/game_x_audio/runtime/Id.h"
#include "rm_engine/game_x_audio/runtime/SoundPack.h"
#include "rm_engine/game_x_audio/runtime/sdk/RMAudioAbi.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace rm;
using namespace rm::game_production;
using namespace rm::game_x_audio;

namespace {

constexpr char kBeep[] = "0123456789abcdef0123456789abcde1";
constexpr char kSongA[] = "0123456789abcdef0123456789abcde2";
constexpr char kSongB[] = "0123456789abcdef0123456789abcde3";

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// One Play: the 3D connector (where entities are) and 音's runtime parts, a silent host whose assets
// are WAVs made here.
struct Play {
    RuntimeRegistry registry;
    GameSettings settings;
    Scene scene;
    PlayServices services;
    std::unordered_map<std::string, std::string> files;
    std::vector<std::string> told;

    Play() {
        const PluginId ids[] = {game_x_3d::kPluginId.Id(), game_x_audio::kPluginId.Id()};
        RegisterRuntimeParts(registry, ids);
        const reflect::TypeDesc* tf = &reflect::TypeOf<space3d::Transform>();
        const reflect::TypeDesc* src = &reflect::TypeOf<AudioSource>();
        scene.SetComponentTypes({tf, src}, {src});
        files[kBeep] = audio::WriteWav16(std::vector<float>(800, 0.1f), {8000, 1});        // 0.1 s
        files[kSongA] = audio::WriteWav16(std::vector<float>(16000, 0.1f), {8000, 1});     // 2 s
        files[kSongB] = audio::WriteWav16(std::vector<float>(16000, -0.1f), {8000, 1});
        PlayHost host;
        host.registry = &registry;
        host.settings = &settings;
        host.asset = [this](const AssetRef& r) -> std::optional<std::string> {
            const auto it = files.find(r.guid);
            return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
        };
        host.log = [this](int, std::string_view t) { told.emplace_back(t); };
        host.silent = true;
        services.Start(std::move(host));
    }
    AudioService& Audio() { return *static_cast<AudioService*>(services.Find(kAudioServiceName)); }
    void Frame(float dt = 0.02f, uint64_t camera = 0) { services.Frame({scene, camera, dt}); }
    // An entity with a 音源 of `sound` (and a Transform).
    int Source(const char* sound, bool playOnStart, AudioBus bus = AudioBus::Effects, bool loop = true) {
        const int e = scene.Create("音");
        scene.AddComponent(e, reflect::TypeOf<space3d::Transform>());
        scene.AddComponent(e, reflect::TypeOf<AudioSource>());
        AudioSource& a = SourceOf(e);
        a.sound.guid = sound;
        a.playOnStart = playOnStart;
        a.bus = bus;
        a.loop = loop;
        return e;
    }
    AudioSource& SourceOf(int e) { return *static_cast<AudioSource*>(scene.Find(e)->FindComponent(reflect::TypeOf<AudioSource>().name)->value.Get()); }
    template <class F>
    bool Within(F done, double seconds = 3) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < until) {
            if (done()) return true;
            Frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return done();
    }
};

}  // namespace

RM_TEST(AudioGame_SoundPack_RoundTripAndBroken) {
    json::Value imp = json::Value::MakeObject();
    imp.Set("volume", 0.5);
    imp.Set("loopStart", 1.25);
    const std::string file = audio::WriteWav16(std::vector<float>(10, 0.f), {8000, 1});
    const std::string packed = PackSound("bgm.wav", imp, file);
    const auto back = UnpackSound(packed);
    CHECK(back && back->name == "bgm.wav" && back->file == file);
    CHECK(back && Near(static_cast<float>(back->import.Get("volume").AsDouble(0)), 0.5f) && Near(static_cast<float>(back->import.Get("loopStart").AsDouble(0)), 1.25f));
    // A plain sound file is not a pack; a broken pack is refused (never read past its end).
    CHECK(!UnpackSound(file));
    CHECK(!UnpackSound(""));
    CHECK(!UnpackSound(packed.substr(0, 10)));
    std::string longer = packed;
    longer[8] = '\xFF';
    longer[9] = '\xFF';
    CHECK(!UnpackSound(longer));
    std::string notJson = PackSound("x", imp, "");
    notJson[12] = '!';
    CHECK(!UnpackSound(notJson));
    // No import settings: an empty object.
    const std::string plainPacked = PackSound("a.ogg", json::Value(), "OggS");  // (kept: file points into it)
    const auto plain = UnpackSound(plainPacked);
    CHECK(plain && plain->import.IsObject() && plain->file == "OggS");
}

RM_TEST(AudioGame_Source_SavedAndReadBack) {
    AudioSource a;
    a.sound.guid = kSongA;
    a.volume = 0.4f;
    a.pitch = 1.5f;
    a.loop = true;
    a.playOnStart = false;
    a.bus = AudioBus::Music;
    a.fade = 2;
    a.spatial = true;
    a.minDistance = 3;
    a.maxDistance = 50;
    const json::Value saved = reflect::Save(a);
    CHECK(saved.Get("sound").AsString() == kSongA && saved.Get("bus").AsString() == "Music");
    AudioSource b;
    reflect::Load(b, saved);
    CHECK(b.sound == a.sound && Near(b.volume, 0.4f) && Near(b.pitch, 1.5f) && b.loop && !b.playOnStart && b.bus == AudioBus::Music);
    CHECK(Near(b.fade, 2) && b.spatial && Near(b.minDistance, 3) && Near(b.maxDistance, 50));
    // The sound field takes sounds (the インスペクター's picker and drops).
    const reflect::TypeDesc& t = reflect::TypeOf<AudioSource>();
    bool found = false;
    for (const reflect::FieldDesc& f : t.fields) {
        if (std::string_view(f.name) == "sound") found = f.assetKinds && std::string_view(f.assetKinds) == "audio";
    }
    CHECK(found);
}

RM_TEST(AudioGame_SourcesStartAndStopWithTheirEntity) {
    Play p;
    CHECK(p.services.Running() && p.services.Find(kAudioServiceName) != nullptr);
    const int on = p.Source(kBeep, true);
    const int off = p.Source(kBeep, false);
    p.Frame();
    CHECK(p.Audio().Engine().Voices() == 1);           // 始めから鳴らす: only that one
    CHECK(p.Audio().SourcePlaying(p.scene, on) && !p.Audio().SourcePlaying(p.scene, off));
    CHECK(p.Audio().SourcePlay(p.scene, off));         // the script's rm::AudioSource::Play
    p.Frame();
    CHECK(p.Audio().Engine().Voices() == 2);
    // Switched off: it stops (and starts again when switched on, as a new one).
    p.scene.Find(on)->data.active = false;
    CHECK(p.Within([&] { return p.Audio().Engine().Voices() == 1; }));
    p.scene.Find(on)->data.active = true;
    p.Frame();
    CHECK(p.Audio().Engine().Voices() == 2);
    // Deleted: it stops.
    const int gone[] = {off};
    p.scene.Delete(gone);
    CHECK(p.Within([&] { return p.Audio().Engine().Voices() == 1; }));
    // Stopped by a script; one with no 音源 says so.
    CHECK(p.Audio().SourceStop(p.scene, on));
    CHECK(p.Within([&] { return p.Audio().Engine().Voices() == 0; }));
    const int bare = p.scene.Create("no source");
    CHECK(!p.Audio().SourcePlay(p.scene, bare));
    // A sound that is not there: nothing plays, told once.
    const size_t before = p.told.size();
    CHECK(p.Audio().PlayOnce(AssetRef{"ffffffffffffffffffffffffffffffff"}, 1, 1, nullptr) == 0);
    CHECK(p.Audio().PlayOnce(AssetRef{"ffffffffffffffffffffffffffffffff"}, 1, 1, nullptr) == 0);
    CHECK(p.told.size() == before + 1);
    // ■: every service goes, and every sound with it.
    p.Audio().PlayOnce(AssetRef{kSongA}, 1, 1, nullptr);
    p.services.Stop();
    CHECK(!p.services.Running() && p.services.Find(kAudioServiceName) == nullptr);
}

RM_TEST(AudioGame_MusicCrossFadesAndGoesOnIntoTheNextScene) {
    Play p;
    AudioService& a = p.Audio();
    a.PlayMusic(AssetRef{kSongA}, 1, 0.2f);
    const auto first = a.Music();
    CHECK(first != 0 && a.Playing(first));
    // Another: the one before fades out while this one fades in.
    a.PlayMusic(AssetRef{kSongB}, 1, 0.2f);
    const auto second = a.Music();
    CHECK(second != 0 && second != first && a.Playing(first) && a.Playing(second));
    CHECK(p.Within([&] { return !a.Playing(first); }));
    CHECK(a.Playing(second));
    // The same again: it goes on as it is.
    a.PlayMusic(AssetRef{kSongB}, 1, 0.2f);
    CHECK(a.Music() == second);
    // A 音源 that is BGM with the same sound takes it over; a scene switched to with the same BGM keeps it.
    p.Source(kSongB, true, AudioBus::Music);
    p.Frame();
    CHECK(a.Music() == second && p.Audio().Engine().Voices() == 1);
    a.SceneSwitched();
    p.scene = Scene();
    p.scene.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<AudioSource>()}, {&reflect::TypeOf<AudioSource>()});
    p.Source(kSongB, true, AudioBus::Music);
    p.Frame();
    CHECK(a.Music() == second && a.Playing(second));
    // ... a scene with another BGM cross-fades to it; a scene with none lets the music go.
    a.SceneSwitched();
    p.scene = Scene();
    p.scene.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<AudioSource>()}, {&reflect::TypeOf<AudioSource>()});
    p.Source(kSongA, true, AudioBus::Music);
    p.Frame();
    const auto third = a.Music();
    CHECK(third != second && a.Playing(third));
    a.SceneSwitched();
    p.scene = Scene();
    p.Frame();
    CHECK(a.Music() == 0);
    CHECK(p.Within([&] { return p.Audio().Engine().Voices() == 0; }));
    // StopMusic: the music a script started.
    a.PlayMusic(AssetRef{kSongA}, 1, 0);
    CHECK(a.Music() != 0);
    a.StopMusic(0);
    CHECK(a.Music() == 0 && p.Within([&] { return p.Audio().Engine().Voices() == 0; }));
}

RM_TEST(AudioGame_Volumes_SettingsOrTheScript) {
    Play p;
    AudioService& a = p.Audio();
    CHECK(Near(a.Volume(AudioService::BusVolume::Master), 1) && Near(a.Volume(AudioService::BusVolume::Music), 1));
    p.settings.volume.music = 0.5f;  // (edited while playing: read as it is)
    p.settings.volume.effects = 0.25f;
    CHECK(Near(a.Volume(AudioService::BusVolume::Music), 0.5f) && Near(a.Volume(AudioService::BusVolume::Effects), 0.25f));
    a.SetVolume(AudioService::BusVolume::Music, 0.2f);  // a script's options screen
    CHECK(Near(a.Volume(AudioService::BusVolume::Music), 0.2f));
    p.settings.volume.music = 0.9f;
    CHECK(Near(a.Volume(AudioService::BusVolume::Music), 0.2f));  // the script's stays
    a.SetVolume(AudioService::BusVolume::Master, 9);
    CHECK(Near(a.Volume(AudioService::BusVolume::Master), 2));    // at most 2
    a.SetVolume(AudioService::BusVolume::Effects, -1);
    CHECK(Near(a.Volume(AudioService::BusVolume::Effects), 0));
    p.Frame();  // (given to the engine)
    // The loudness of one voice: 全体 x bus x asset x asked (audio::Gain), each at most 4.
    CHECK(Near(audio::Gain(2, 0.2f, 1, 0.5f), 0.2f));
}

RM_TEST(AudioGame_ScriptPart) {
    Play p;
    ScriptWorld w;
    w.scene = &p.scene;
    w.services = &p.services;
    const auto* api = static_cast<const RmAudioApi*>(ScriptAudioPart().table);
    CHECK(std::string_view(ScriptAudioPart().name) == RM_PART_AUDIO && ScriptAudioPart().version == RM_PART_AUDIO_VERSION);
    char guid[RM_ASSET_ID_SIZE] = {};
    std::memcpy(guid, kBeep, sizeof(kBeep) - 1);
    const uint64_t v = api->play(&w, guid, 1, 1, nullptr);
    CHECK(v != 0 && api->playing(&w, v) == 1);
    const float at[3] = {1, 2, 3};
    CHECK(api->play(&w, guid, 1, 2, at) != 0);
    const int e = p.Source(kBeep, false);
    CHECK(api->sourceSet(&w, static_cast<uint64_t>(e), -1, 0) == 1);  // rm::AudioSource::Valid
    CHECK(api->sourceSet(&w, static_cast<uint64_t>(e), 0, 0.3f) == 1 && Near(p.SourceOf(e).volume, 0.3f));
    CHECK(api->sourceSet(&w, static_cast<uint64_t>(e), 1, 100) == 1 && Near(p.SourceOf(e).pitch, audio::kMaxPitch));
    CHECK(api->source(&w, static_cast<uint64_t>(e), 0) == 1 && api->sourcePlaying(&w, static_cast<uint64_t>(e)) == 1);
    CHECK(api->source(&w, static_cast<uint64_t>(e), 1) == 1);
    const int bare = p.scene.Create("no source");
    CHECK(api->source(&w, static_cast<uint64_t>(bare), 0) == 0 && api->sourceSet(&w, static_cast<uint64_t>(bare), -1, 0) == 0);
    CHECK(api->source(&w, 0, 0) == 0 && api->source(&w, ~uint64_t{0}, 0) == 0);
    api->setVolume(&w, 1, 0.5f);
    CHECK(Near(api->volume(&w, 1), 0.5f) && api->volume(&w, 7) == 0);
    api->playMusic(&w, guid, 1, 0);
    CHECK(p.Audio().Music() != 0);
    api->stopMusic(&w, 0);
    CHECK(p.Audio().Music() == 0);
    // No services (a host without them): nothing happens, nothing breaks.
    ScriptWorld none;
    CHECK(api->play(&none, guid, 1, 1, nullptr) == 0 && api->source(&none, 1, 0) == 0 && api->playing(&none, 1) == 0);
}

RM_TEST(AudioGame_WhereAnEntityIsHeardFrom) {
    Play p;
    const reflect::TypeDesc& tf = reflect::TypeOf<space3d::Transform>();
    const int parent = p.scene.Create("parent");
    p.scene.AddComponent(parent, tf);
    static_cast<space3d::Transform*>(p.scene.Find(parent)->FindComponent(tf.name)->value.Get())->position = {10, 0, 0};
    const int child = p.scene.Create("child", parent);
    p.scene.AddComponent(child, tf);
    auto* t = static_cast<space3d::Transform*>(p.scene.Find(child)->FindComponent(tf.name)->value.Get());
    t->position = {1, 2, 3};
    Pose pose;
    CHECK(EntityPose(p.registry, p.scene, child, pose));
    CHECK(Near(pose.position.x, 11) && Near(pose.position.y, 2) && Near(pose.position.z, 3));
    CHECK(Near(pose.forward.z, -1) && Near(pose.up.y, 1));  // looks along -Z (as a camera)
    t->rotation = {0, 90, 0};  // turned left: looks along -X
    CHECK(EntityPose(p.registry, p.scene, child, pose) && Near(pose.forward.x, -1) && Near(pose.forward.z, 0));
    const int bare = p.scene.Create("no transform");
    CHECK(!EntityPose(p.registry, p.scene, bare, pose) && !EntityPose(p.registry, p.scene, 999, pose));
    // A 3D 音源 plays from there (and the camera hears it).
    const int s = p.Source(kSongA, true);
    p.SourceOf(s).spatial = true;
    p.Frame(0.02f, static_cast<uint64_t>(child));
    CHECK(p.Audio().SourcePlaying(p.scene, s));
}
