#include "rm_engine/audio/runtime/AudioEngine.h"

#include "platform/Win.h"

#include <mmreg.h>
#include <x3daudio.h>
#include <xaudio2.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <format>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#pragma comment(lib, "xaudio2.lib")  // XAudio2 2.9 と X3DAudio (xaudio2_9.dll: Windows 10 以降)

using Microsoft::WRL::ComPtr;

namespace rm::audio {

void ApplyImport(SoundSource& sound, const asset::SoundImport& import) {
    sound.assetVolume = import.volume;
    sound.stream = import.stream == asset::SoundStream::Always  ? StreamMode::Always
                   : import.stream == asset::SoundStream::Never ? StreamMode::Never
                                                                : StreamMode::Auto;
    sound.loopStart = import.loopStart;
    sound.loopEnd = import.loopEnd;
}

namespace {

constexpr int kStreamBuffers = 4;   // ストリームの待ち行列に入れた断片 (同時に多くて kStreamBuffers - 1 個)
constexpr double kPieceSeconds = 0.25;

enum class State : uint8_t { Loading, Playing, Done };

struct Voice {
    AudioEngine::VoiceId id = 0;
    SoundSource sound;
    PlayParams p;
    State state = State::Loading;
    bool stopAsked = false;  // 止めてほしい (エンジンのスレッドが壊す)
    bool paused = false;
    bool fadingOut = false;
    double fadeInT = 0, fadeOutT = 0, fadeOutLen = 0;
    // デバイス上
    IXAudio2SourceVoice* src = nullptr;
    IXAudio2Voice* bus = nullptr;
    Format format;
    std::shared_ptr<const Pcm> pcm;  // 丸ごとデコードした音 (共有)
    // ストリーム
    std::unique_ptr<Decoder> dec;
    uint64_t total = 0, pos = 0;
    LoopPoints loop;
    bool streamEnded = false;
    std::array<std::vector<float>, kStreamBuffers> buffers;
    int nextBuffer = 0;
    // 無音: 「鳴らす」ために残っている時間 (秒。ループ中は無限)
    double silentLeft = 0;
};

// X3DAudio は左手系。エンジンの 3D 空間も左手系 (2026-11-20 から): そのまま渡す。
X3DAUDIO_VECTOR Left(Vec3 v) { return {v.x, v.y, v.z}; }

}  // namespace

struct AudioEngine::Impl {
    bool silent = true;
    ComPtr<IXAudio2> xa;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SubmixVoice* music = nullptr;
    IXAudio2SubmixVoice* effects = nullptr;
    UINT32 outChannels = 2;
    X3DAUDIO_HANDLE x3d{};

    mutable std::mutex m;
    std::condition_variable_any cv;
    std::unordered_map<VoiceId, std::shared_ptr<Voice>> voices;
    std::deque<std::shared_ptr<Voice>> loads;
    // 丸ごとデコードした音をキーごとに持つ: エンジンが生きている間は残す (鳴らし直す効果音は読み直さない)。
    // kKeepBytes まで - 超えたら古いものから手放す (それを鳴らしているボイスは自分の分を持ち続ける)。
    static constexpr size_t kKeepBytes = 256u << 20;
    std::unordered_map<std::string, std::shared_ptr<const Pcm>> decoded;
    std::deque<std::string> decodedOrder;
    size_t decodedBytes = 0;
    std::vector<std::string> messages;
    std::unordered_set<std::string> told;
    VoiceId nextId = 1;
    bool pausedAll = false;
    Vec3 listenerPos{0, 0, 0}, listenerFront{0, 0, 1}, listenerUp{0, 1, 0};
    std::jthread thread;

    void Tell(const std::string& key, std::string text) {
        if (told.insert(key).second) messages.push_back(std::move(text));
    }

    // --- エンジンのスレッド ------------------------------------------------------------------------
    void Run(std::stop_token stop) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // (Media Foundation)
        while (!stop.stop_requested()) {
            std::shared_ptr<Voice> job;
            {
                std::unique_lock lock(m);
                cv.wait_for(lock, stop, std::chrono::milliseconds(20), [&] { return !loads.empty(); });
                if (stop.stop_requested()) break;
                if (!loads.empty()) {
                    job = std::move(loads.front());
                    loads.pop_front();
                }
            }
            if (job) Load(job);
            Feed();
        }
        if (SUCCEEDED(co)) CoUninitialize();
    }

    // 音を読み (ロックの外: 時間がかかるかもしれない)、そのあとデバイスに載せる。
    void Load(const std::shared_ptr<Voice>& v) {
        {
            std::lock_guard lock(m);
            if (v->stopAsked) {
                voices.erase(v->id);
                return;
            }
        }
        const SoundBudget budget;
        std::string error;
        std::unique_ptr<Decoder> dec = OpenDecoder(v->sound.bytes, budget, &error);
        std::shared_ptr<const Pcm> pcm;
        bool stream = false;
        if (dec) {
            const double seconds = dec->format.sampleRate ? static_cast<double>(dec->frames) / dec->format.sampleRate : 0;
            stream = v->sound.stream == StreamMode::Always || dec->frames == 0 ||
                     (v->sound.stream == StreamMode::Auto && seconds > kStreamLongerThan) || seconds > budget.maxWholeSeconds;
            if (!stream) {
                {
                    std::lock_guard lock(m);
                    if (const auto it = decoded.find(v->sound.key); !v->sound.key.empty() && it != decoded.end()) pcm = it->second;
                }
                if (!pcm) {
                    auto whole = DecodeWhole(*dec, budget, &error);
                    if (whole) {
                        pcm = std::make_shared<const Pcm>(std::move(*whole));
                    } else {
                        stream = true;  // (結局長すぎた: ストリームにする)
                        dec = OpenDecoder(v->sound.bytes, budget, &error);
                    }
                }
            }
        }
        std::lock_guard lock(m);
        if (v->stopAsked || (!dec && !pcm)) {
            if (!dec && !pcm) Tell(v->sound.key + "|read", std::format("音「{}」を鳴らせません: {}", v->sound.name, error));
            voices.erase(v->id);
            return;
        }
        if (pcm && !v->sound.key.empty() && !decoded.contains(v->sound.key)) {
            decoded[v->sound.key] = pcm;
            decodedOrder.push_back(v->sound.key);
            decodedBytes += pcm->samples.size() * sizeof(float);
            while (decodedBytes > kKeepBytes && decodedOrder.size() > 1) {
                const auto old = decoded.find(decodedOrder.front());
                if (old != decoded.end()) {
                    decodedBytes -= old->second->samples.size() * sizeof(float);
                    decoded.erase(old);
                }
                decodedOrder.pop_front();
            }
        }
        v->format = pcm ? pcm->format : dec->format;
        v->total = pcm ? pcm->Frames() : dec->frames;
        v->loop = LoopFrames(v->sound.loopStart, v->sound.loopEnd, v->format.sampleRate, v->total);
        v->pcm = std::move(pcm);
        if (stream) v->dec = std::move(dec);
        v->silentLeft = v->p.loop ? std::numeric_limits<double>::infinity()
                                  : (v->format.sampleRate ? static_cast<double>(v->total) / v->format.sampleRate / ClampPitch(v->p.pitch) : 0);
        if (!silent && !MakeSourceVoice(*v)) {
            voices.erase(v->id);
            return;
        }
        v->state = State::Playing;
        Apply(*v, 0);
        if (v->src && !v->paused && !pausedAll) v->src->Start(0);
    }

    bool MakeSourceVoice(Voice& v) {
        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = v.format.channels;
        wf.Format.nSamplesPerSec = v.format.sampleRate;
        wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = static_cast<WORD>(v.format.channels * 4);
        wf.Format.nAvgBytesPerSec = v.format.sampleRate * wf.Format.nBlockAlign;
        wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = v.format.channels == 1 ? SPEAKER_FRONT_CENTER : v.format.channels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
        wf.SubFormat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        v.bus = v.p.bus == Bus::Music ? static_cast<IXAudio2Voice*>(music) : static_cast<IXAudio2Voice*>(effects);
        XAUDIO2_SEND_DESCRIPTOR send{0, v.bus};
        const XAUDIO2_VOICE_SENDS sends{1, &send};
        // kMaxPitch までのピッチ用に作る (既定の MaxFrequencyRatio は 2 で、それ以上は断られる)。
        if (FAILED(xa->CreateSourceVoice(&v.src, &wf.Format, 0, kMaxPitch, nullptr, &sends, nullptr))) {
            v.src = nullptr;
            Tell(v.sound.key + "|voice", std::format("音「{}」の声を作れません ({} Hz・{} チャンネル)", v.sound.name, v.format.sampleRate, v.format.channels));
            return false;
        }
        if (v.pcm) {
            XAUDIO2_BUFFER b{};
            b.AudioBytes = static_cast<UINT32>(v.pcm->samples.size() * sizeof(float));
            b.pAudioData = reinterpret_cast<const BYTE*>(v.pcm->samples.data());
            b.Flags = XAUDIO2_END_OF_STREAM;
            if (v.p.loop) {
                b.LoopBegin = static_cast<UINT32>(v.loop.start);
                b.LoopLength = static_cast<UINT32>(v.loop.end - v.loop.start);
                b.LoopCount = XAUDIO2_LOOP_INFINITE;
                if (b.LoopBegin == 0 && v.loop.end >= v.total) b.LoopLength = 0;  // (全体)
            }
            v.src->SubmitSourceBuffer(&b);
        } else {
            FeedStream(v);
        }
        return true;
    }

    // ストリームは kStreamBuffers - 1 個先まで入れておく (ロックの下で)。
    void FeedStream(Voice& v) {
        if (!v.src || !v.dec) return;
        XAUDIO2_VOICE_STATE st{};
        v.src->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        const size_t want = std::max<size_t>(1, static_cast<size_t>(v.format.sampleRate * kPieceSeconds));
        int guard = 0;
        while (st.BuffersQueued < kStreamBuffers - 1 && !v.streamEnded && guard++ < kStreamBuffers) {
            const Piece piece = v.total ? NextPiece(v.pos, v.total, want, v.p.loop, v.loop) : Piece{v.pos, want, false};
            if (piece.from != v.pos && !v.dec->Seek(piece.from)) {
                v.streamEnded = true;
                break;
            }
            std::vector<float>& buf = v.buffers[static_cast<size_t>(v.nextBuffer)];
            buf.resize(piece.frames * v.format.channels);
            const size_t got = piece.frames ? v.dec->Read(buf.data(), piece.frames) : 0;
            v.pos = piece.from + got;
            if (got == 0) {
                // 本当の終わり (長さが間違っていた、または不明): もう一周するか、終わる。
                if (v.p.loop && v.pos != v.loop.start && v.dec->Seek(v.loop.start)) {
                    v.pos = v.loop.start;
                    continue;
                }
                v.streamEnded = true;
                break;
            }
            XAUDIO2_BUFFER b{};
            b.AudioBytes = static_cast<UINT32>(got * v.format.channels * sizeof(float));
            b.pAudioData = reinterpret_cast<const BYTE*>(buf.data());
            const bool last = piece.ends && got == piece.frames;
            if (last) {
                b.Flags = XAUDIO2_END_OF_STREAM;
                v.streamEnded = true;
            }
            v.src->SubmitSourceBuffer(&b);
            v.nextBuffer = (v.nextBuffer + 1) % kStreamBuffers;
            ++st.BuffersQueued;
        }
    }

    void Destroy(Voice& v) {
        if (v.src) {
            v.src->Stop(0);
            v.src->FlushSourceBuffers();
            v.src->DestroyVoice();  // (デバイスがバッファを手放すまで待つ)
            v.src = nullptr;
        }
        v.state = State::Done;
    }

    // ストリームに送り込む。終わった、または止められたボイスを消す (エンジンのスレッド)。
    void Feed() {
        std::lock_guard lock(m);
        for (auto it = voices.begin(); it != voices.end();) {
            Voice& v = *it->second;
            if (v.state != State::Playing) {
                ++it;
                continue;
            }
            bool done = v.stopAsked;
            if (!done && v.src) {
                if (v.dec) FeedStream(v);
                XAUDIO2_VOICE_STATE st{};
                v.src->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                done = st.BuffersQueued == 0 && (v.pcm || v.streamEnded);
            }
            if (done) {
                Destroy(v);
                it = voices.erase(it);
            } else {
                ++it;
            }
        }
    }

    // --- ゲームのスレッド -------------------------------------------------------------------------
    // 音量・ピッチ・聞こえてくる方向 (ロックの下で)。
    void Apply(Voice& v, double dt) {
        const bool running = !v.paused && !pausedAll;
        float g = Gain(1, 1, v.sound.assetVolume, v.p.volume);
        if (v.p.fadeIn > 0) {
            if (running) v.fadeInT += dt;
            g *= FadeGain(v.fadeInT, v.p.fadeIn, true);
        }
        if (v.fadingOut) {
            if (running) v.fadeOutT += dt;
            g *= FadeGain(v.fadeOutT, v.fadeOutLen, false);
            if (v.fadeOutT >= v.fadeOutLen) {
                v.stopAsked = true;
                if (v.src) v.src->Stop(0);
            }
        }
        if (v.p.spatial) {
            const float d = std::sqrt((v.p.position.x - listenerPos.x) * (v.p.position.x - listenerPos.x) +
                                      (v.p.position.y - listenerPos.y) * (v.p.position.y - listenerPos.y) +
                                      (v.p.position.z - listenerPos.z) * (v.p.position.z - listenerPos.z));
            g *= DistanceGain(d, v.p.minDistance, v.p.maxDistance);
        }
        if (silent) {
            if (running && v.state == State::Playing) v.silentLeft -= dt;
            if (v.silentLeft <= 0 && v.state == State::Playing) v.stopAsked = true;  // (終わった: Update が片づける)
            return;
        }
        if (!v.src) return;
        v.src->SetVolume(g);
        v.src->SetFrequencyRatio(ClampPitch(v.p.pitch));
        if (v.p.spatial) {
            // 聞こえてくる方向 (左 / 右 ...): X3DAudio の行列。その距離カーブは平らにしてある
            // (距離による音の大きさは上の DistanceGain: どこでも同じ。テスト済み)。
            static const X3DAUDIO_DISTANCE_CURVE_POINT kFlat[2] = {{0.f, 1.f}, {1.f, 1.f}};
            static const X3DAUDIO_DISTANCE_CURVE kFlatCurve{const_cast<X3DAUDIO_DISTANCE_CURVE_POINT*>(kFlat), 2};
            X3DAUDIO_LISTENER l{};
            l.OrientFront = Left(listenerFront);
            l.OrientTop = Left(listenerUp);
            l.Position = Left(listenerPos);
            std::vector<float> azimuths(v.format.channels, 0.f);
            X3DAUDIO_EMITTER e{};
            e.ChannelCount = v.format.channels;
            e.pChannelAzimuths = azimuths.data();
            e.CurveDistanceScaler = 1.f;
            e.OrientFront = {0, 0, 1};
            e.OrientTop = {0, 1, 0};
            e.Position = Left(v.p.position);
            e.pVolumeCurve = const_cast<X3DAUDIO_DISTANCE_CURVE*>(&kFlatCurve);
            std::vector<float> matrix(static_cast<size_t>(v.format.channels) * outChannels);
            X3DAUDIO_DSP_SETTINGS dsp{};
            dsp.SrcChannelCount = v.format.channels;
            dsp.DstChannelCount = outChannels;
            dsp.pMatrixCoefficients = matrix.data();
            X3DAudioCalculate(x3d, &l, &e, X3DAUDIO_CALCULATE_MATRIX, &dsp);
            v.src->SetOutputMatrix(v.bus, v.format.channels, outChannels, matrix.data());
        }
    }
};

AudioEngine::AudioEngine(bool silent) : impl_(std::make_unique<Impl>()) {
    Impl& s = *impl_;
    s.silent = true;
    if (!silent && SUCCEEDED(XAudio2Create(&s.xa, 0, XAUDIO2_DEFAULT_PROCESSOR)) &&
        SUCCEEDED(s.xa->CreateMasteringVoice(&s.master))) {
        XAUDIO2_VOICE_DETAILS details{};
        s.master->GetVoiceDetails(&details);
        s.outChannels = details.InputChannels;
        DWORD mask = 0;
        s.master->GetChannelMask(&mask);
        if (SUCCEEDED(s.xa->CreateSubmixVoice(&s.music, s.outChannels, details.InputSampleRate)) &&
            SUCCEEDED(s.xa->CreateSubmixVoice(&s.effects, s.outChannels, details.InputSampleRate)) &&
            SUCCEEDED(X3DAudioInitialize(mask, X3DAUDIO_SPEED_OF_SOUND, s.x3d))) {
            s.silent = false;
        }
    }
    if (s.silent && !silent) s.messages.push_back("音を出す装置が見つからないので、音は鳴りません (XAudio2 を使えません)");
    s.thread = std::jthread([&s](std::stop_token stop) { s.Run(stop); });
}

AudioEngine::~AudioEngine() {
    Impl& s = *impl_;
    s.thread.request_stop();
    s.cv.notify_all();
    if (s.thread.joinable()) s.thread.join();
    for (auto& [id, v] : s.voices) s.Destroy(*v);
    s.voices.clear();
    if (s.music) s.music->DestroyVoice();
    if (s.effects) s.effects->DestroyVoice();
    if (s.master) s.master->DestroyVoice();
    s.xa.Reset();
}

bool AudioEngine::HasDevice() const { return !impl_->silent; }

AudioEngine::VoiceId AudioEngine::Play(const SoundSource& sound, const PlayParams& params) {
    Impl& s = *impl_;
    auto v = std::make_shared<Voice>();
    v->sound = sound;
    v->p = params;
    v->p.pitch = ClampPitch(params.pitch);
    v->paused = params.paused;
    {
        std::lock_guard lock(s.m);
        v->id = s.nextId++;
        s.voices[v->id] = v;
        s.loads.push_back(v);
    }
    s.cv.notify_all();
    return v->id;
}

void AudioEngine::Stop(VoiceId id, double fadeOut) {
    Impl& s = *impl_;
    std::lock_guard lock(s.m);
    const auto it = s.voices.find(id);
    if (it == s.voices.end()) return;
    Voice& v = *it->second;
    if (fadeOut > 0 && !v.paused && !s.pausedAll) {  // (まだ読み込み中のものは、鳴り始めてからフェードする)
        if (!v.fadingOut) {
            v.fadingOut = true;
            v.fadeOutT = 0;
            v.fadeOutLen = fadeOut;
        }
        return;
    }
    v.stopAsked = true;
    if (v.src) v.src->Stop(0);  // (すぐ無音にする。エンジンのスレッドが壊す)
}

void AudioEngine::SetPaused(VoiceId id, bool paused) {
    Impl& s = *impl_;
    std::lock_guard lock(s.m);
    const auto it = s.voices.find(id);
    if (it == s.voices.end()) return;
    Voice& v = *it->second;
    v.paused = paused;
    if (v.src && !s.pausedAll && !v.stopAsked) {
        if (paused) {
            v.src->Stop(0);  // 位置を保つ (XAudio2 の Stop は一時停止)
        } else {
            v.src->Start(0);
        }
    }
}

void AudioEngine::SetVolume(VoiceId id, float volume) {
    std::lock_guard lock(impl_->m);
    if (const auto it = impl_->voices.find(id); it != impl_->voices.end()) it->second->p.volume = volume;
}

void AudioEngine::SetPitch(VoiceId id, float pitch) {
    std::lock_guard lock(impl_->m);
    if (const auto it = impl_->voices.find(id); it != impl_->voices.end()) it->second->p.pitch = ClampPitch(pitch);
}

void AudioEngine::SetPosition(VoiceId id, Vec3 position) {
    std::lock_guard lock(impl_->m);
    if (const auto it = impl_->voices.find(id); it != impl_->voices.end()) it->second->p.position = position;
}

bool AudioEngine::IsPlaying(VoiceId id) const {
    std::lock_guard lock(impl_->m);
    const auto it = impl_->voices.find(id);
    return it != impl_->voices.end() && !it->second->stopAsked && it->second->state != State::Done;
}

void AudioEngine::SetListener(Vec3 position, Vec3 forward, Vec3 up) {
    std::lock_guard lock(impl_->m);
    impl_->listenerPos = position;
    impl_->listenerFront = forward;
    impl_->listenerUp = up;
}

void AudioEngine::SetVolumes(float master, float music, float effects) {
    Impl& s = *impl_;
    std::lock_guard lock(s.m);
    if (s.silent) return;
    s.master->SetVolume(Gain(master, 1, 1, 1));
    s.music->SetVolume(Gain(music, 1, 1, 1));
    s.effects->SetVolume(Gain(effects, 1, 1, 1));
}

void AudioEngine::PauseAll(bool paused) {
    Impl& s = *impl_;
    std::lock_guard lock(s.m);
    if (s.pausedAll == paused) return;
    s.pausedAll = paused;
    for (auto& [id, v] : s.voices) {
        if (!v->src || v->paused || v->stopAsked) continue;
        if (paused) {
            v->src->Stop(0);
        } else {
            v->src->Start(0);
        }
    }
}

void AudioEngine::StopAll() {
    Impl& s = *impl_;
    std::lock_guard lock(s.m);
    for (auto& [id, v] : s.voices) {
        v->stopAsked = true;
        if (v->src) v->src->Stop(0);
    }
}

size_t AudioEngine::Voices() const {
    std::lock_guard lock(impl_->m);
    return static_cast<size_t>(std::count_if(impl_->voices.begin(), impl_->voices.end(), [](const auto& kv) { return !kv.second->stopAsked; }));
}

void AudioEngine::Update(double dt) {
    Impl& s = *impl_;
    std::lock_guard lock(s.m);
    for (auto it = s.voices.begin(); it != s.voices.end();) {
        Voice& v = *it->second;
        if (v.state == State::Playing && !v.stopAsked) s.Apply(v, dt);
        // 終わった (または止められた) 無音のボイスはここで消す: デバイスに手放すものは何もない。
        if (s.silent && v.state == State::Playing && v.stopAsked) {
            it = s.voices.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<std::string> AudioEngine::TakeMessages() {
    std::lock_guard lock(impl_->m);
    return std::exchange(impl_->messages, {});
}

}  // namespace rm::audio
