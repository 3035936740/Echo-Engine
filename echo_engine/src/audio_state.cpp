#include "echo/audio_state.hpp"
#include <algorithm>
#include <cmath>
namespace echo {
bool playSound(WorldState& w, int clip, float gain, float pitch) {
    auto& a = w.audio;
    if (clip < 0 || clip >= 32 || a.lengths[clip] <= 0)
        return false;
    SoundVoice v;
    v.id = a.nextVoiceId++;
    v.clip = clip;
    v.gain = std::clamp(gain, 0.0f, 2.0f);
    v.pitch = std::clamp(pitch, .25f, 4.0f);
    v.duration = a.lengths[clip];
    if (!a.voices.push(v)) {
        ++a.voicesDropped;
        return false;
    }
    return true;
}
void fadeMusic(WorldState& w, float target, float seconds) {
    auto& a = w.audio;
    a.fadeStart = a.musicVolume;
    a.fadeTarget = std::clamp(target, 0.0f, 1.0f);
    a.fadeDuration = std::max(0.0f, seconds);
    a.fadeElapsed = 0;
    if (a.fadeDuration == 0)
        a.musicVolume = a.fadeTarget;
}
void startMusic(WorldState& w, bool restart) {
    auto& a = w.audio;
    a.playing = true;
    if (restart) {
        a.playSeconds = 0;
        ++a.transportGeneration;
    }
}
void stopMusic(WorldState& w) {
    w.audio.playing = false;
    w.audio.playSeconds = 0;
    ++w.audio.transportGeneration;
}
void stepAudioState(WorldState& w, float dt) {
    auto& a = w.audio;
    if (a.fadeElapsed < a.fadeDuration) {
        a.fadeElapsed = std::min(a.fadeDuration, a.fadeElapsed + dt);
        float u = a.fadeElapsed / a.fadeDuration;
        a.musicVolume = a.fadeStart + (a.fadeTarget - a.fadeStart) * u;
    }
    if (a.playing && !a.paused) {
        a.playSeconds += dt;
        float end = a.loopEnd > 0 ? a.loopEnd : a.musicLength;
        if (end > 0 && a.playSeconds >= end) {
            if (a.loop)
                a.playSeconds =
                    a.loopStart + std::fmod(a.playSeconds - end, std::max(.001f, end - a.loopStart));
            else {
                a.playSeconds = end;
                a.playing = false;
            }
        }
    }
    for (std::size_t i = 0; i < a.voices.count;) {
        auto& voice = a.voices[i];
        voice.age += dt * voice.pitch;
        if (voice.age >= voice.duration)
            a.voices.erase(i);
        else
            ++i;
    }
    if (a.seenSoundEvent != w.soundEvent) {
        a.seenSoundEvent = w.soundEvent;
        playSound(w, w.soundKind - 1);
    }
}
} // namespace echo
