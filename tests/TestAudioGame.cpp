// ゲーム制作 × 音 (plugins/rm_engine/game_x_audio) のテスト: 書き出したゲームの .rmsound、
// 保存して読み戻した 音源 部品、Play の音 (無音: デバイスなし) - 音源 はエンティティと一緒に
// 始まり止まり、音楽はクロスフェードして同じ BGM のシーンへ続き、■ はすべてを止める -
// 音量、スクリプトの部分、エンティティがどこから聞こえるか (3D 空間の PlaceInfo)。

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

// 1 回の Play: 3D コネクター (エンティティの居場所) と 音 のランタイムの部品、ここで作った
// WAV をアセットにする無音のホスト。
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
    // `sound` の 音源 を持つ (Transform もある) エンティティ。
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

RM_TEST(AudioGame_OtherServicesPlaySounds) {
    // ゲーム UI のボタンの音: ほかのサービスは、音を鳴らすサービスを知らなくても見つけられる。
    Play p;
    SoundPlayer* sounds = p.services.Sounds();
    CHECK(sounds == static_cast<SoundPlayer*>(&p.Audio()));
    // 音楽の大きさ (ゲーム UI の 動画 の音): 全体 × 音楽。スクリプトの変更も。
    p.settings.volume.master = 0.5f;
    p.settings.volume.music = 0.8f;
    CHECK(Near(sounds->MusicVolume(), 0.4f));
    p.Audio().SetVolume(AudioService::BusVolume::Music, 0.2f);
    CHECK(Near(sounds->MusicVolume(), 0.1f));
    sounds->PlaySound(AssetRef{kBeep}, 0.5f);
    sounds->PlaySound(AssetRef{"0123456789abcdef0123456789abcdef"}, 1);  // (ない: 何もしない)
    p.Frame();
    // 音を鳴らすサービスがない: 何も鳴らない。
    PlayServices none;
    CHECK(none.Sounds() == nullptr);
}

RM_TEST(AudioGame_SoundPack_RoundTripAndBroken) {
    json::Value imp = json::Value::MakeObject();
    imp.Set("volume", 0.5);
    imp.Set("loopStart", 1.25);
    const std::string file = audio::WriteWav16(std::vector<float>(10, 0.f), {8000, 1});
    const std::string packed = PackSound("bgm.wav", imp, file);
    const auto back = UnpackSound(packed);
    CHECK(back && back->name == "bgm.wav" && back->file == file);
    CHECK(back && Near(static_cast<float>(back->import.Get("volume").AsDouble(0)), 0.5f) && Near(static_cast<float>(back->import.Get("loopStart").AsDouble(0)), 1.25f));
    // ただの音のファイルはパックではない。壊れたパックは断る (終わりを越えて読まない)。
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
    // 取り込みの設定なし: 空のオブジェクト。
    const std::string plainPacked = PackSound("a.ogg", json::Value(), "OggS");  // (残る: ファイルはその中を指す)
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
    // sound フィールドは音を受け取る (インスペクターの選択とドロップ)。
    const reflect::TypeDesc& t = reflect::TypeOf<AudioSource>();
    bool found = false;
    for (const reflect::FieldDesc& f : t.fields) {
        if (std::string_view(f.name) == "sound") found = f.assetKinds && std::string_view(f.assetKinds) == "audio";
    }
    CHECK(found);
}

RM_TEST(AudioGame_AskingDoesNotStopPlayOnStart) {
    // スクリプトが BeginPlay の中で IsPlaying を尋ねる (サービスの最初の Frame より前): 音源の
    // 始めから鳴らす は、それでも始まる。
    Play p;
    const int s = p.Source(kBeep, true);
    CHECK(!p.Audio().SourcePlaying(p.scene, s));  // (まだ始まっていない)
    p.Frame();
    CHECK(p.Audio().SourcePlaying(p.scene, s));
}

RM_TEST(AudioGame_SourcesStartAndStopWithTheirEntity) {
    Play p;
    CHECK(p.services.Running() && p.services.Find(kAudioServiceName) != nullptr);
    const int on = p.Source(kBeep, true);
    const int off = p.Source(kBeep, false);
    p.Frame();
    CHECK(p.Audio().Engine().Voices() == 1);           // 始めから鳴らす: それだけ
    CHECK(p.Audio().SourcePlaying(p.scene, on) && !p.Audio().SourcePlaying(p.scene, off));
    CHECK(p.Audio().SourcePlay(p.scene, off));         // スクリプトの rm::AudioSource::Play
    p.Frame();
    CHECK(p.Audio().Engine().Voices() == 2);
    // オフにした: 止まる (オンに戻すと、新しいものとしてまた始まる)。
    p.scene.Find(on)->data.active = false;
    CHECK(p.Within([&] { return p.Audio().Engine().Voices() == 1; }));
    p.scene.Find(on)->data.active = true;
    p.Frame();
    CHECK(p.Audio().Engine().Voices() == 2);
    // 削除した: 止まる。
    const int gone[] = {off};
    p.scene.Delete(gone);
    CHECK(p.Within([&] { return p.Audio().Engine().Voices() == 1; }));
    // スクリプトで止めた。音源 のないものはそう答える。
    CHECK(p.Audio().SourceStop(p.scene, on));
    CHECK(p.Within([&] { return p.Audio().Engine().Voices() == 0; }));
    const int bare = p.scene.Create("no source");
    CHECK(!p.Audio().SourcePlay(p.scene, bare));
    // ない音: 何も鳴らず、1 回だけ知らせる。
    const size_t before = p.told.size();
    CHECK(p.Audio().PlayOnce(AssetRef{"ffffffffffffffffffffffffffffffff"}, 1, 1, nullptr) == 0);
    CHECK(p.Audio().PlayOnce(AssetRef{"ffffffffffffffffffffffffffffffff"}, 1, 1, nullptr) == 0);
    CHECK(p.told.size() == before + 1);
    // ■: すべてのサービスが消え、すべての音も一緒に消える。
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
    // 別の音: 前のものはフェードアウトし、今度のものはフェードインする。
    a.PlayMusic(AssetRef{kSongB}, 1, 0.2f);
    const auto second = a.Music();
    CHECK(second != 0 && second != first && a.Playing(first) && a.Playing(second));
    CHECK(p.Within([&] { return !a.Playing(first); }));
    CHECK(a.Playing(second));
    // 同じものをもう一度: そのまま続く。
    a.PlayMusic(AssetRef{kSongB}, 1, 0.2f);
    CHECK(a.Music() == second);
    // 同じ音の BGM の 音源 はそれを引き継ぐ。同じ BGM のシーンに切り替えても保つ。
    p.Source(kSongB, true, AudioBus::Music);
    p.Frame();
    CHECK(a.Music() == second && p.Audio().Engine().Voices() == 1);
    a.SceneSwitched();
    p.scene = Scene();
    p.scene.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<AudioSource>()}, {&reflect::TypeOf<AudioSource>()});
    p.Source(kSongB, true, AudioBus::Music);
    p.Frame();
    CHECK(a.Music() == second && a.Playing(second));
    // ... 別の BGM のシーンならそちらへクロスフェードし、BGM のないシーンなら音楽を手放す。
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
    // StopMusic: スクリプトが始めた音楽。
    a.PlayMusic(AssetRef{kSongA}, 1, 0);
    CHECK(a.Music() != 0);
    a.StopMusic(0);
    CHECK(a.Music() == 0 && p.Within([&] { return p.Audio().Engine().Voices() == 0; }));
}

RM_TEST(AudioGame_Volumes_SettingsOrTheScript) {
    Play p;
    AudioService& a = p.Audio();
    CHECK(Near(a.Volume(AudioService::BusVolume::Master), 1) && Near(a.Volume(AudioService::BusVolume::Music), 1));
    p.settings.volume.music = 0.5f;  // (再生中に編集: そのまま読む)
    p.settings.volume.effects = 0.25f;
    CHECK(Near(a.Volume(AudioService::BusVolume::Music), 0.5f) && Near(a.Volume(AudioService::BusVolume::Effects), 0.25f));
    a.SetVolume(AudioService::BusVolume::Music, 0.2f);  // スクリプトのオプション画面
    CHECK(Near(a.Volume(AudioService::BusVolume::Music), 0.2f));
    p.settings.volume.music = 0.9f;
    CHECK(Near(a.Volume(AudioService::BusVolume::Music), 0.2f));  // スクリプトの値が残る
    a.SetVolume(AudioService::BusVolume::Master, 9);
    CHECK(Near(a.Volume(AudioService::BusVolume::Master), 2));    // 最大 2
    a.SetVolume(AudioService::BusVolume::Effects, -1);
    CHECK(Near(a.Volume(AudioService::BusVolume::Effects), 0));
    p.Frame();  // (エンジンに渡される)
    // 1 つの声の大きさ: 全体 x バス x アセット x 頼まれた値 (audio::Gain)。それぞれ最大 4。
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
    // サービスがない (ない状態のホスト): 何も起きず、何も壊れない。
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
    CHECK(Near(pose.forward.z, -1) && Near(pose.up.y, 1));  // -Z を向く (カメラと同じ)
    t->rotation = {0, 90, 0};  // 左を向いた: -X を向く
    CHECK(EntityPose(p.registry, p.scene, child, pose) && Near(pose.forward.x, -1) && Near(pose.forward.z, 0));
    const int bare = p.scene.Create("no transform");
    CHECK(!EntityPose(p.registry, p.scene, bare, pose) && !EntityPose(p.registry, p.scene, 999, pose));
    // 3D の 音源 はそこから鳴る (カメラがそれを聞く)。
    const int s = p.Source(kSongA, true);
    p.SourceOf(s).spatial = true;
    p.Frame(0.02f, static_cast<uint64_t>(child));
    CHECK(p.Audio().SourcePlaying(p.scene, s));
}
