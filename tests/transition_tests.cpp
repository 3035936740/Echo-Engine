#include "echo/simulation.hpp"
#include "echo/resources.hpp"
#include "echo/transition.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace echo;
static void check(bool good, const char* text) {
    if (!good)
        throw std::runtime_error(text);
}
static std::unique_ptr<WorldState> capture(const Simulation& sim) {
    return std::make_unique<WorldState>(sim.world());
}
static void fresh(WorldState& world) { world = makeWorld(); }
int main() {
    try {
        TransitionConfig c;
        c.kind = TransitionKind::Iris;
        c.closeSeconds = .12f;
        c.holdSeconds = .06f;
        c.openSeconds = .12f;
        c.direction = 2;
        c.color = {.1f, .2f, .3f};
        check(writeTransition(readTransition(writeTransition(c))) == writeTransition(c),
              "transition config roundtrip");
        bool invalid = false;
        try {
            readTransition("7 0 0 1 nan 0 0 0 0 0 12");
        } catch (...) {
            invalid = true;
        }
        check(invalid, "nonfinite config rejected");
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        auto start = w->level;
        MapPortal p;
        p.region = {150, 540, 60, 110};
        p.targetMap = 1;
        start.portals.push(p);
        auto target = start;
        target.portals.clear();
        target.spawn = {800, 592};
        target.background = 1;
        start.portals[0].transitionOverride = true;
        start.portals[0].transition = c;
        auto level = loadLevel(saveLevel(start));
        check(level.portals[0].transitionOverride &&
                  writeTransition(level.portals[0].transition) == writeTransition(c),
              "portal override roundtrip");
        for (int hz : {30, 60, 120}) {
            fresh(*w);
            w->aster.enabled = false;
            w->kino.invulnTimer = 1000;
            w->level = start;
            w->mapTransition.kind = TransitionKind::None;
            w->rewind.record_hz = hz;
            w->audio.playing = true;
            w->audio.musicLength = 100;
            Simulation sim(32ull * 1024 * 1024);
            sim.setMaps({start, target});
            sim.reset(*w);
            sim.tick({});
            check(sim.world().activeMap == 0 && sim.world().transition.phase == TransitionPhase::Closing,
                  "closing keeps source map");
            auto checkpoint = capture(sim);
            Input move;
            move.move = 1;
            move.jump = true;
            move.confirm = true;
            sim.tick(move);
            check(sim.world().kino.x == checkpoint->kino.x && sim.world().kino.y == checkpoint->kino.y,
                  "transition input and physics are frozen");
            auto expected = capture(sim);
            sim.truncateToCursor(checkpoint->timelineCursor);
            sim.restore(*checkpoint);
            sim.tick(move);
            check(sim.world().transition.elapsed == expected->transition.elapsed &&
                      sim.world().activeMap == expected->activeMap,
                  "closing full restore progresses deterministically");
            bool covered = false, opening = false;
            std::uint64_t before = checkpoint->timelineGenerationId;
            for (int i = 0; i < 80 && sim.world().transition.phase != TransitionPhase::Idle; ++i) {
                sim.tick(move);
                const auto& t = sim.world().transition;
                if (t.phase == TransitionPhase::Covered) {
                    covered = true;
                    check(sim.world().activeMap == 1 && transitionCover(t) == 1,
                          "covered commits target under opaque mask");
                    auto hold = capture(sim);
                    sim.tick({});
                    auto heldElapsed = sim.world().transition.elapsed;
                    sim.truncateToCursor(hold->timelineCursor);
                    sim.restore(*hold);
                    sim.tick({});
                    check(sim.world().transition.elapsed == heldElapsed &&
                              sim.world().timelineGenerationId == before + 1,
                          "covered restore never repeats transfer");
                }
                if (t.phase == TransitionPhase::Opening)
                    opening = true;
            }
            check(covered && opening && sim.world().activeMap == 1 &&
                      sim.world().kino.x == target.spawn.x,
                  "reveal remains at spawn until completion");
            check(sim.world().timelineGenerationId == before + 1, "one transition one commit");
            check(sim.world().audio.playSeconds > checkpoint->audio.playSeconds,
                  "BGM advances during transition");
            Input trace;
            trace.trace = true;
            for (int i = 0; i < 100; ++i) {
                sim.tick(trace);
                if (sim.world().state != TimeState::Trace)
                    break;
            }
            sim.tick({});
            check(sim.world().activeMap == 0, "farthest T crosses transition");
            for (int i = 0; i < 50; ++i)
                sim.tick({});
            check(sim.world().activeMap == 1 && sim.world().transition.phase == TransitionPhase::Idle,
                  "farthest rewind then continue completes transition");
        }
        // None keeps the established immediate transfer behavior.
        fresh(*w);
        w->aster.enabled = false;
        w->kino.invulnTimer = 1000;
        start.portals[0].transitionOverride = false;
        w->level = start;
        Simulation sim(32ull * 1024 * 1024);
        sim.setMaps({start, target});
        sim.reset(*w);
        sim.tick({});
        check(sim.world().activeMap == 1 && sim.world().transition.phase == TransitionPhase::Idle,
              "no animation switches immediately");
        // Zero-duration animation completes without hanging or committing twice.
        w->mapTransition = c;
        w->mapTransition.closeSeconds = 0;
        w->mapTransition.holdSeconds = 0;
        w->mapTransition.openSeconds = 0;
        sim.reset(*w);
        sim.tick({});
        sim.tick({});
        check(sim.world().activeMap == 1 && sim.world().transition.phase == TransitionPhase::Idle,
              "zero-duration phases complete");
        // Disabled rewind Hit still waits for explicit restart; death wipe works without history.
        w->rewind.enabled = false;
        w->deathTransition = c;
        {
            Simulation startupDisabled(32ull * 1024 * 1024);
            startupDisabled.reset(*w);
            check(startupDisabled.history().capacity() == 0,
                  "disabled startup transition config allocates no history");
        }
        sim.reset(*w);
        sim.world().state = TimeState::Hit;
        sim.world().gameplayClockPaused = true;
        sim.world().kino.hp = 0;
        sim.tick({});
        check(sim.world().state == TimeState::Hit, "disabled Hit waits for restart");
        Input restart;
        restart.restart = true;
        sim.tick(restart);
        check(sim.world().transition.reason == TransitionReason::Restart &&
                  sim.world().transition.phase == TransitionPhase::Closing,
              "restart closes before reset");
        for (int i = 0; i < 50; ++i)
            sim.tick(restart);
        check(sim.world().kino.hp == 1 && sim.world().transition.phase == TransitionPhase::Idle &&
                  sim.history().size() == 0,
              "disabled restart finishes once with held key and no records");
        // Global event-triggered transfers use the same project transition.
        fresh(*w);
        w->aster.enabled = false;
        w->kino.invulnTimer = 1000;
        w->mapTransition = c;
        start.portals.clear();
        w->level = start;
        sim.setMaps({start, target});
        EventMap local;
        auto e = eventPreset(1, 3);
        e.pages[0].commands[0].a = 1;
        local.events.push_back(e);
        EventPlacement placed;
        placed.id = 1;
        placed.region = {185, 592, 32, 48};
        w->level.events.push(placed);
        sim.setEvents({local, {}}, {});
        sim.reset(*w);
        Input confirm;
        confirm.confirm = true;
        sim.tick(confirm);
        check(sim.world().activeMap == 0 && sim.world().transition.phase == TransitionPhase::Closing,
              "event transfer uses shared transition request");
        for (int i = 0; i < 40; ++i)
            sim.tick({});
        check(sim.world().activeMap == 1 && sim.world().transition.phase == TransitionPhase::Idle,
              "event transfer completes");
        std::cout
            << "PASS presets, serialization, source/cover/reveal, frozen input, BGM, 30/60/120Hz rewind "
               "resume, None, zero durations, disabled Hit restart, held restart, event transfer\n";
        std::cout << "WorldState bytes: " << sizeof(WorldState) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << "\n";
        return 1;
    }
}
