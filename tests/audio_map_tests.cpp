#include "echo/audio_decode.hpp"
#include "echo/audio_state.hpp"
#include "echo/camera.hpp"
#include "echo/simulation.hpp"
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace echo;
void check(bool ok, const char* name) {
    if (!ok)
        throw std::runtime_error(name);
}
Bytes wave() {
    Bytes b;
    auto text = [&](const char* s) {
        for (int i = 0; i < 4; ++i)
            b.push_back(s[i]);
    };
    auto u16 = [&](unsigned n) {
        b.push_back(n);
        b.push_back(n >> 8);
    };
    auto u32 = [&](unsigned n) {
        u16(n);
        u16(n >> 16);
    };
    text("RIFF");
    u32(36 + 16000);
    text("WAVE");
    text("fmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(8000);
    u32(16000);
    u16(2);
    u16(16);
    text("data");
    u32(16000);
    for (int i = 0; i < 8000; ++i)
        u16(unsigned(std::int16_t(std::sin(i * .1) * 12000)));
    return b;
}
static void fresh(WorldState& world) { world = makeWorld(); }
int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            std::ifstream f(argv[1], std::ios::binary);
            Bytes ogg((std::istreambuf_iterator<char>(f)), {});
            auto decoded = decodeAudio(ogg);
            check(std::abs(decoded.seconds() - 8) < .01, "OGG Vorbis decoded from project");
        }
        auto bytes = wave();
        auto pcm = decodeAudio(bytes);
        check(pcm.samples.size() == 88200 && std::abs(pcm.seconds() - 1) < 1e-9,
              "bounded mono WAV resampling");
        check(pcm.samples[20] == pcm.samples[21], "mono becomes stereo");
        bool rejected = false;
        try {
            decodeAudio(bytes, 100);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "PCM budget enforced");
        bytes.resize(50);
        rejected = false;
        try {
            decodeAudio(bytes);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "truncated audio rejected");
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        w->aster.enabled = false;
        w->kino.invulnTimer = 1000;
        w->audio.musicLength = 8;
        w->audio.loopStart = 2;
        w->audio.loopEnd = 8;
        w->audio.playSeconds = 7.9;
        stepAudioState(*w, .2f);
        check(std::abs(w->audio.playSeconds - 2.1) < 1e-6, "authored BGM loop points");
        fadeMusic(*w, 0, 1);
        stepAudioState(*w, .5f);
        check(std::abs(w->audio.musicVolume - .35f) < 1e-6, "BGM gradual fade");
        auto snapshot = std::make_unique<WorldState>(*w);
        stepAudioState(*w, .5f);
        restore(*w, *snapshot);
        check(std::abs(w->audio.musicVolume - .35f) < 1e-6 && w->audio.fadeElapsed == .5f,
              "fade state full restore");
        w->audio.lengths[0] = 1;
        for (int i = 0; i < 16; ++i)
            check(playSound(*w, 0), "bounded SE voice");
        check(!playSound(*w, 0) && w->audio.voicesDropped == 1, "SE exhaustion is bounded");
        stepAudioState(*w, 1.1f);
        check(w->audio.voices.count == 0, "SE voices released");
        Simulation sim(128ull * 1024 * 1024);
        sim.reset(*w);
        for (int i = 0; i < 480; ++i)
            sim.tick({});
        Input trace;
        trace.trace = true;
        for (int i = 0; i < 480; ++i) {
            sim.tick(trace);
            if (sim.world().state != TimeState::Trace)
                break;
        }
        sim.tick({});
        sim.tick({});
        check(sim.world().audio.playSeconds >= 2 && sim.world().audio.playSeconds <= 8,
              "audio farthest rewind resumes loop");
        auto start = w->level;
        start.background = 2;
        start.cameraClamp = true;
        auto next = start;
        next.spawn = {800, 592};
        next.background = 1;
        next.cameraClamp = false;
        MapPortal portal;
        portal.region = {500, 540, 40, 110};
        portal.targetMap = 1;
        start.portals.push(portal);
        sim.setMaps({start, next});
        fresh(*w);
        w->aster.enabled = false;
        w->kino.invulnTimer = 1000;
        w->level = start;
        sim.reset(*w);
        Input right;
        right.move = 1;
        for (int i = 0; i < 200 && sim.world().activeMap == 0; ++i)
            sim.tick(right);
        check(sim.world().activeMap == 1 && sim.world().level.background == 1,
              "no-script map transition");
        check(sim.world().kino.x == next.spawn.x && sim.world().transitionCooldown > 0,
              "spawn and transition cooldown snapshotted");
        auto transitioned = std::make_unique<WorldState>(sim.world());
        sim.tick({});
        sim.restore(*transitioned);
        check(!sim.world().level.cameraClamp && sim.world().activeMap == 1, "map state full restore");
        CameraState camera;
        sim.world().kino.x = 0;
        camera.update(sim.world(), 0, true);
        check(camera.logicalX < 0, "unclamped scroll");
        sim.world().level.cameraClamp = true;
        camera.update(sim.world(), 0, true);
        check(camera.logicalX == 0, "clamped scroll");
        for (int i = 0; i < 200; ++i) {
            sim.tick(trace);
            if (sim.world().state != TimeState::Trace)
                break;
        }
        check(sim.world().activeMap == 0 && sim.world().level.background == 2,
              "farthest rewind crosses map transition");
        sim.tick({});
        sim.tick(right);
        check(sim.world().activeMap == 0, "forward continues on restored map");
        fresh(*w);
        w->aster.enabled = false;
        w->rewind.record_hz = 120;
        DialogueDefinition dialog;
        dialog.region = {w->kino.x, w->kino.y, 60, 60};
        dialog.text.set("Long dialogue");
        w->level.dialogues.push(dialog);
        sim.reset(*w);
        sim.tick({});
        int before = sim.history().size();
        sim.tick({});
        check(sim.history().size() == before + 2, "dialogue 120 Hz stores real substeps");
        std::cout << "PASS WAV decode/budget, loop/fade/SE snapshots, audio oldest resume, map "
                     "transition rewind, camera bounds, dialogue 120 Hz\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
