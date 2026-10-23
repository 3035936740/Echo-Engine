#include "echo/focus.hpp"
#include "echo/camera.hpp"
#include "echo/input_bindings.hpp"
#include "echo/resources.hpp"
#include "echo/script.hpp"
#include "echo/collision.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>
static std::size_t allocations{};
void* operator new(std::size_t n) {
    ++allocations;
    if (auto* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete[](void* p) noexcept {
    ::operator delete(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    ::operator delete(p);
}
using namespace echo;
static void check(bool v, const char* s) {
    if (!v)
        throw std::runtime_error(s);
}
static void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.jump.gravity = 0;
    w.kino.x = 100;
    w.kino.y = 100;
    w.kino.grounded = false;
    w.kino.invulnTimer = 100;
    w.kino.vx = w.kino.vy = 0;
    w.level.tiles.fill(Tile::Empty);
    w.level.colliders.clear();
    w.level.platforms.clear();
    w.level.events.clear();
    w.level.dialogues.clear();
    w.level.portals.clear();
    w.bullets.clear();
    w.hurt = {};
    w.hurt.source = HurtSource::Custom;
    w.hurt.regions[0].anchorX = w.hurt.regions[0].anchorY = 0;
    w.hurt.regions[0].radius = 3;
}
static void history(Simulation& sim, WorldState& w, int hz, int mode = 0) {
    w.rewind.record_hz = hz;
    sim.reset(w);
    for (int f = 1; f <= 6 * hz; ++f) {
        auto& state = sim.world();
        state.clock = double(f) / hz;
        state.kino.x = 100 + 40 * float(state.clock);
        if (mode == 1 && state.bullets.count) {
            state.bullets[0].x = 800 - 100 * float(state.clock);
            state.bullets[0].px = state.bullets[0].x + 100.f / hz;
        }
        if (mode == 2) {
            state.bullets.clear();
            if (f == 6 * hz - 1) {
                Bullet b;
                b.id = 42;
                b.x = 333;
                b.y = 100;
                b.px = 332;
                b.py = 100;
                state.bullets.push(b);
            }
        }
        sim.record();
    }
}
static void runFast(Simulation& sim) {
    Input fast;
    fast.fast = true;
    for (int i = 0; i < 150 && (i == 0 || sim.world().state == TimeState::Fast); ++i)
        sim.tick(fast);
}
int main(int argc, char** argv) {
    try {
        auto w = std::make_unique<WorldState>();
        for (int hz : {30, 60, 120}) {
            fresh(*w);
            Bullet b;
            b.id = 42;
            b.x = b.px = 220;
            b.y = b.py = 100;
            w->bullets.push(b);
            w->level.cameraClamp = false;
            Simulation sim(384ull * 1024 * 1024);
            history(sim, *w, hz);
            CameraState camera;
            camera.update(sim.world(), 0, true);
            runFast(sim);
            camera.update(sim.world(), 1.f / 60);
            check(std::abs(camera.logicalX - (sim.world().kino.x + sim.world().kino.width * .5f - 640 +
                                              sim.world().kino.facing * 64)) < .001f,
                  "camera follows a guarded branch instantly");
            auto& state = sim.world();
            check(state.state == TimeState::Free && state.kino.x > 228 && state.kino.x < 228.02,
                  "default +2 guard stops before first expanded-circle contact");
            check(state.kino.hp == 1 && state.bullets.count == 1 && state.bullets[0].hitR == 3 &&
                      state.bullets[0].px == 220 && state.bullets[0].py == 100,
                  "guard ignores invulnerability and preserves health/bullet hitR and px/py");
            check(state.timelineGenerationId == 1 && sim.history().size() == state.timelineCursor + 1 &&
                      !state.fast.hold && state.heldRewind.mode == TimeState::Idle,
                  "guard truncates old future and releases snapshotted controllers");
            float stopped = state.kino.x;
            Input held;
            held.fast = true;
            sim.tick(held);
            check(state.state != TimeState::Fast && state.kino.x == stopped,
                  "held key cannot re-enter after guard");
            sim.tick({});
            sim.tick(held);
            check(state.state != TimeState::Hit, "release/re-arm never causes collision damage");
            // T still reaches the actual oldest trajectory snapshot.
            history(sim, *w, hz);
            Input trace;
            trace.trace = true;
            for (int i = 0; i < 370 && (i == 0 || sim.world().state == TimeState::Trace); ++i)
                sim.tick(trace);
            check(sim.world().clock < .001 && sim.world().kino.x == 100, "T has no proximity guard");
            sim.tick({});
            Input move;
            move.move = 1;
            sim.tick(move);
            check(sim.world().clock > 0 && sim.world().kino.hp == 1, "oldest T branch continues");
            w->rewind.fast_stop_near_bullets = false;
            history(sim, *w, hz);
            runFast(sim);
            check(sim.world().clock < .001 && sim.world().kino.x == 100 &&
                      sim.world().bullets.count == 1,
                  "disabled guard reaches oldest and still never clears bullets");
            sim.tick({});
            sim.tick(move);
            check(sim.world().clock > 0, "oldest R branch continues");
            fresh(*w);
            history(sim, *w, hz);
            runFast(sim);
            check(sim.world().clock < .001 && sim.world().kino.x == 100,
                  "enabled guard with no danger reaches the complete six-second oldest R");
            sim.tick({});
            sim.tick(move);
            check(sim.world().clock > 0, "enabled oldest R continues at every recording Hz");
            fresh(*w);
            history(sim, *w, hz, 2);
            runFast(sim);
            check(sim.world().clock > 5.8 && sim.world().bullets.count == 1,
                  "short-lived danger in a skipped fast frame is inspected");
            fresh(*w);
            w->bullets.push(b);
            history(sim, *w, hz, 1);
            runFast(sim);
            check(sim.world().clock > 4.9 && sim.world().clock < 5.2 &&
                      std::abs(sim.world().kino.x - sim.world().bullets[0].x) > 6,
                  "relative moving bullet sweep stops before restored bullet hit");
        }
        {
            Simulation sim(128ull * 1024 * 1024);
            fresh(*w);
            Bullet b;
            b.id = 1;
            b.x = b.px = 220;
            b.y = b.py = 100;
            w->bullets.push(b);
            w->hurt.method = HurtMethod::Overlap;
            w->hurt.touching = false;
            w->rewind.fast_bullet_margin = 10;
            history(sim, *w, 30);
            runFast(sim);
            check(sim.world().kino.x > 236 && sim.world().kino.x < 236.02 &&
                      sim.world().hurt.method == HurtMethod::Overlap && !sim.world().hurt.touching,
                  "proximity remains swept without changing authored hit rules");
            fresh(*w);
            b.damage = 0;
            w->bullets.push(b);
            history(sim, *w, 30);
            runFast(sim);
            check(sim.world().clock < .001, "non-damaging decorative bullets ignored");
            fresh(*w);
            b.damage = 1;
            w->bullets.push(b);
            setCollisionPair(w->collisions, CollisionClass::Player, CollisionClass::Projectile, false);
            history(sim, *w, 30);
            runFast(sim);
            check(sim.world().clock < .001, "collision matrix respected");
            fresh(*w);
            b.brake = true;
            w->bullets.push(b);
            history(sim, *w, 30);
            runFast(sim);
            check(sim.world().kino.x > 100 && sim.world().bullets.count == 1,
                  "amber body brake retained");
            fresh(*w);
            b.brake = false;
            b.x = b.px = 338;
            w->bullets.push(b);
            history(sim, *w, 30);
            runFast(sim);
            check(sim.world().kino.x == 340 && !sim.world().fast.hold,
                  "initial proximity stops in place");
            fresh(*w);
            b.x = b.px = 220;
            b.y = b.py = 124;
            w->bullets.push(b);
            w->hurt.source = HurtSource::Whole;
            history(sim, *w, 30);
            runFast(sim);
            check(sim.world().kino.x > 225 && sim.world().kino.x < 225.02,
                  "whole character hurt rectangle");
            fresh(*w);
            b.x = b.px = 338;
            b.y = b.py = 100;
            w->bullets.push(b);
            w->rewind.fast_rate = .1f;
            history(sim, *w, 30);
            sim.world().clock += 1. / 60;
            sim.world().kino.x += 2.f / 3;
            float unrecordedX = sim.world().kino.x;
            runFast(sim);
            check(sim.world().timelineGenerationId == 1 &&
                      sim.history().at(sim.world().timelineCursor).kino.x == unrecordedX &&
                      !sim.world().fast.hold,
                  "sub-frame guard commits restored world and new position even at same cursor");
            fresh(*w);
            b.x = b.px = 220;
            b.y = b.py = 100;
            w->bullets.push(b);
            w->hurt.count = 2;
            w->hurt.regions[0].mask = hurtContact;
            w->hurt.regions[1] = w->hurt.regions[0];
            w->hurt.regions[1].mask = hurtBullets;
            w->hurt.regions[1].shape = HurtShape::Rectangle;
            w->hurt.regions[1].offsetX = 10;
            w->hurt.regions[1].width = w->hurt.regions[1].height = 4;
            history(sim, *w, 30);
            runFast(sim);
            check(sim.world().kino.x > 217 && sim.world().kino.x < 217.02,
                  "multi-region hurt masks and geometry");
            fresh(*w);
            w->rewind.enabled = false;
            Simulation off;
            off.reset(*w);
            Input fast;
            fast.fast = true;
            off.tick(fast);
            check(off.history().capacity() == 0 && off.world().state != TimeState::Fast,
                  "disabled rewind has no guard or allocation");
        }
        if (argc > 1) {
            Resources r;
            r.root = std::filesystem::path(argv[1]) / "fast-guard-lua";
            std::filesystem::create_directories(r.root / "scripts");
            writeTextAtomically(r.root / "project.echo", "scripts=scripts/guard.lua\n");
            writeTextAtomically(r.root / "scripts/guard.lua",
                                "rewind.fast_stop_near_bullets=false\nrewind.fast_bullet_margin=7.5\n");
            ScriptRuntime lua;
            fresh(*w);
            lua.load(r, *w);
            check(!w->rewind.fast_stop_near_bullets && w->rewind.fast_bullet_margin == 7.5f,
                  "Lua guard fields");
            auto snapshot = std::make_unique<WorldState>(*w);
            w->rewind.fast_stop_near_bullets = true;
            w->rewind.fast_bullet_margin = 0;
            Simulation sim;
            sim.restore(*snapshot);
            check(!sim.world().rewind.fast_stop_near_bullets &&
                      sim.world().rewind.fast_bullet_margin == 7.5f,
                  "guard config snapshots fully restore");
            writeTextAtomically(r.root / "scripts/guard.lua", "rewind.fast_bullet_margin=65\n");
            bool rejected = false;
            try {
                lua.load(r, *w);
            } catch (...) {
                rejected = true;
            }
            check(rejected, "Lua invalid margin rejected");
        }
        // Dense six-second rewind with deliberately colliding hash IDs. No heap work in ticks.
        fresh(*w);
        for (int i = 0; i < maxBullets; ++i) {
            Bullet b;
            b.id = 1 + i * 4096;
            b.x = b.px = 500;
            b.y = b.py = 1000;
            w->bullets.push(b);
        }
        Simulation dense(384ull * 1024 * 1024);
        history(dense, *w, 120);
        Input fast;
        fast.fast = true;
        std::vector<double> times;
        times.reserve(150);
        auto before = allocations;
        for (int i = 0; i < 150 && (i == 0 || dense.world().state == TimeState::Fast); ++i) {
            auto start = std::chrono::steady_clock::now();
            dense.tick(fast);
            times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count());
        }
        check(allocations == before, "dense fast rewind tick performs no C++ heap allocation");
        check(dense.world().clock < .001 && dense.world().bullets.count == maxBullets,
              "dense oldest preserves all bullets");
        dense.tick({});
        dense.tick({});
        std::sort(times.begin(), times.end());
        if (argc > 1) {
            std::ofstream out(std::filesystem::path(argv[1]) / "fast-rewind-performance.txt");
            out << "WorldState_bytes=" << sizeof(WorldState)
                << "\nfast_rewind_2048_hz=120\nheap_allocations_in_ticks=0\n"
                << "p95_ms=" << times[std::min(times.size() - 1, times.size() * 95 / 100)]
                << "\np99_ms=" << times[std::min(times.size() - 1, times.size() * 99 / 100)]
                << "\nworst_ms=" << times.back() << '\n';
        }
        std::cout << "Fast rewind proximity guard passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
