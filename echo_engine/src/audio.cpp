#include <windows.h>
#include "echo/audio.hpp"
#include "echo/audio_decode.hpp"
#include "echo/audio_state.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <mmsystem.h>
#include <stdexcept>
namespace echo {
struct Audio::Impl {
    static constexpr int blockFrames = 512;
    HWAVEOUT device{};
    std::array<std::array<std::int16_t, blockFrames * 2>, 3> buffers{};
    std::array<WAVEHDR, 3> headers{};
    PcmClip music;
    std::array<PcmClip, 32> effects;
    struct Voice {
        std::uint32_t id{};
        double cursor{};
    };
    std::array<Voice, 16> voices{};
    double cursor{}, previousClock{};
    float previousGain{};
    std::uint64_t generation = ~0ull, transport = ~0ull;
    bool paused{}, rewinding{}, lastPlaying{}, musicMuted{};
    Impl() {
        WAVEFORMATEX f{WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0};
        if (waveOutOpen(&device, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            device = nullptr;
        if (device)
            for (int i = 0; i < 3; ++i) {
                auto& h = headers[i];
                h.lpData = reinterpret_cast<char*>(buffers[i].data());
                h.dwBufferLength = sizeof(buffers[i]);
                waveOutPrepareHeader(device, &h, sizeof(h));
            }
    }
    ~Impl() {
        if (device) {
            waveOutReset(device);
            for (auto& h : headers)
                waveOutUnprepareHeader(device, &h, sizeof(h));
            waveOutClose(device);
        }
    }
    void reset(double position) {
        if (device)
            waveOutReset(device);
        cursor = position * 44100;
        voices = {};
        previousGain = 0;
    }
    static float sample(const PcmClip& clip, double cursor, int channel) {
        auto frames = clip.samples.size() / 2;
        if (cursor < 0 || cursor >= frames || !frames)
            return 0;
        auto a = std::size_t(cursor), b = std::min(frames - 1, a + 1);
        float t = float(cursor - a);
        return clip.samples[a * 2 + channel] +
               (clip.samples[b * 2 + channel] - clip.samples[a * 2 + channel]) * t;
    }
    Voice& voice(const SoundVoice& logical) {
        for (auto& v : voices)
            if (v.id == logical.id)
                return v;
        for (auto& v : voices)
            if (v.id == 0) {
                v = {logical.id, logical.age * 44100.0};
                return v;
            }
        voices[logical.id % voices.size()] = {logical.id, logical.age * 44100.0};
        return voices[logical.id % voices.size()];
    }
};
Audio::Audio() : p_(std::make_unique<Impl>()) {}
Audio::~Audio() = default;
bool Audio::available() const {
    return p_->device != nullptr;
}
void Audio::load(const Resources& resources, const ProjectConfig& cfg) {
    PcmClip music;
    std::array<PcmClip, 32> effects;
    std::size_t total = 0;
    constexpr std::size_t budget = 64ull * 1024 * 1024;
    if (cfg.musicEnabled) {
        music = decodeAudio(resources.read(cfg.music), budget);
        total += music.samples.size() * 2;
    }
    for (int i = 0; i < 32; ++i)
        if (!cfg.effects[i].empty()) {
            effects[i] = decodeAudio(resources.read(cfg.effects[i]), budget - total);
            total += effects[i].samples.size() * 2;
            if (total > budget)
                throw std::runtime_error("Project PCM audio exceeds 64 MiB budget");
        }
    if (cfg.musicEnabled &&
        (cfg.loopStart < 0 || cfg.loopStart >= music.seconds() ||
         (cfg.loopEnd > 0 && (cfg.loopEnd > music.seconds() + .001 || cfg.loopEnd <= cfg.loopStart))))
        throw std::runtime_error(
            "BGM loop requires 0 <= start < end <= clip duration (end=0 uses clip end)");
    auto& p = *p_;
    p.reset(0);
    p.music = std::move(music);
    p.effects = std::move(effects);
    p.generation = p.transport = ~0ull;
    p.previousClock = 0;
}
void Audio::configure(WorldState& w, const ProjectConfig& cfg) const {
    w.audio = {};
    auto& a = w.audio;
    a.playing = cfg.musicEnabled;
    a.musicLength = float(p_->music.seconds());
    a.loop = cfg.musicLoop;
    a.loopStart = cfg.loopStart;
    a.loopEnd = cfg.loopEnd;
    a.musicVolume = cfg.fadeIn > 0 ? 0 : cfg.musicVolume;
    a.seVolume = cfg.seVolume;
    for (int i = 0; i < 32; ++i)
        a.lengths[i] = float(p_->effects[i].seconds());
    if (cfg.fadeIn > 0)
        fadeMusic(w, cfg.musicVolume, cfg.fadeIn);
}
void Audio::pause(bool paused) {
    auto& p = *p_;
    if (p.paused == paused)
        return;
    p.paused = paused;
    if (p.device) {
        if (paused)
            waveOutPause(p.device);
        else
            waveOutRestart(p.device);
    }
}
void Audio::setMusicMuted(bool muted) {
    auto& p = *p_;
    if (p.musicMuted == muted)
        return;
    p.musicMuted = muted;
    // Flush already queued music immediately; SE resume from logical voice ages.
    if (p.device)
        waveOutReset(p.device);
    p.voices = {};
    p.previousGain = 0;
}
bool Audio::musicMuted() const {
    return p_->musicMuted;
}
void Audio::update(const WorldState& w) {
    auto& p = *p_;
    auto& a = w.audio;
    if (!p.device)
        return;
    bool rewind = w.state == TimeState::Fast || w.state == TimeState::Trace;
    if (w.timelineGenerationId != p.generation || a.transportGeneration != p.transport ||
        w.clock < p.previousClock || rewind != p.rewinding || a.playing != p.lastPlaying) {
        p.reset(a.playSeconds);
        p.generation = w.timelineGenerationId;
        p.transport = a.transportGeneration;
    }
    p.previousClock = w.clock;
    p.rewinding = rewind;
    p.lastPlaying = a.playing;
    pause(rewind || a.paused);
    if (rewind || a.paused)
        return;
    for (auto& v : p.voices) {
        bool alive = false;
        for (auto& cue : a.voices)
            if (cue.id == v.id)
                alive = true;
        if (!alive)
            v = {};
    }
    // Fixed buffers and 16 bounded voices: no decode, allocation, thread spawning,
    // singleton music, or device-state feedback into gameplay in the render loop.
    bool musicPaused = (w.gameplayClockPaused && w.dialogue.active < 0) ||
                       w.state == TimeState::Suppressed || w.state == TimeState::Escape;
    for (int block = 0; block < 3; ++block) {
        auto& header = p.headers[block];
        if ((header.dwFlags & WHDR_INQUEUE) && !(header.dwFlags & WHDR_DONE))
            continue;
        std::array<Impl::Voice*, 16> voices{};
        int count = 0;
        for (auto& cue : a.voices)
            voices[count++] = &p.voice(cue);
        float target = a.playing && !musicPaused && !p.musicMuted ? a.musicVolume : 0;
        double loopStart = a.loopStart * 44100.0,
               loopEnd = (a.loopEnd > 0 ? a.loopEnd : a.musicLength) * 44100.0;
        for (int frame = 0; frame < Impl::blockFrames; ++frame) {
            float volume =
                p.previousGain + (target - p.previousGain) * float(frame + 1) / Impl::blockFrames;
            if (loopEnd > loopStart && p.cursor >= loopEnd && a.loop)
                p.cursor = loopStart + std::fmod(p.cursor - loopEnd, loopEnd - loopStart);
            for (int channel = 0; channel < 2; ++channel) {
                float mixed = Impl::sample(p.music, p.cursor, channel) * volume;
                for (int i = 0; i < count; ++i) {
                    auto& cue = a.voices[i];
                    mixed += Impl::sample(p.effects[cue.clip], voices[i]->cursor, channel) * cue.gain *
                             a.seVolume;
                }
                p.buffers[block][frame * 2 + channel] =
                    std::int16_t(std::clamp(mixed, -32768.0f, 32767.0f));
            }
            if (a.playing && !musicPaused)
                ++p.cursor;
            for (int i = 0; i < count; ++i)
                voices[i]->cursor += a.voices[i].pitch;
        }
        p.previousGain = target;
        waveOutWrite(p.device, &header, sizeof(header));
    }
}
} // namespace echo
