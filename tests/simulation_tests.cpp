#include "echo/camera.hpp"
#include "echo/simulation.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace echo;
void check(bool value, const char* name) {
    if (!value)
        throw std::runtime_error(name);
}
int main(int argc, char** argv) {
    try {
        auto start = std::make_unique<WorldState>(makeWorld());
        start->aster.enabled = false;
        Simulation sim(256ull * 1024 * 1024);
        sim.reset(*start);
        Input right;
        right.move = 1;
        for (int i = 0; i < 180; ++i)
            sim.tick(right);
        auto before = sim.world().clock;
        Input trace;
        trace.trace = true;
        for (int i = 0; i < 120; ++i)
            sim.tick(trace);
        check(std::abs(sim.world().clock - (before - 2)) < .04, "T rewinds two seconds");
        int cut = sim.world().timelineCursor;
        sim.tick({});
        check(sim.history().size() == cut + 1 && sim.world().timelineGenerationId == 1,
              "T truncates and increments generation");
        for (int i = 0; i < 360; ++i)
            sim.tick(right);
        Input fast;
        fast.fast = true;
        sim.tick(fast);
        auto endpoints = sim.world().fast;
        for (int i = 0; i < 12; ++i)
            sim.tick(fast);
        float u = sim.world().heldRewind.heldSeconds / endpoints.maxRewindSeconds;
        check(std::abs(sim.world().kino.x - (endpoints.A.x + (endpoints.B.x - endpoints.A.x) * u)) <
                  .001,
              "R fixed chord");
        sim.tick({});
        check(sim.world().rewindExitGrace > .44f, "R exit shield");
        for (int i = 0; i < 60; ++i)
            sim.tick({});
        check(sim.world().state == TimeState::Free, "continues at oldest history");

        auto& w = sim.world();
        w.kino.y = 592;
        w.kino.grounded = true;
        w.kino.vx = 0;
        w.kino.vy = 0;
        Input jump;
        jump.jump = true;
        sim.tick(jump);
        check(w.kino.vy < -300 && w.kino.animState == KinoAnimState::Jump, "jump driven animation");
        sim.tick({});
        check(w.kino.jumpWasCut && !w.kino.jumpHeld && w.kino.vy > -120, "release cuts jump");
        auto saved = std::make_unique<WorldState>(sim.snapshot());
        for (int i = 0; i < 20; ++i)
            sim.tick({});
        float y = w.kino.y, vy = w.kino.vy;
        sim.restore(*saved);
        sim.history().clear();
        sim.record();
        for (int i = 0; i < 20; ++i)
            sim.tick({});
        check(w.kino.y == y && w.kino.vy == vy, "deterministic jump after restore");

        start->kino.x = 200;
        start->kino.y = 464;
        start->kino.grounded = true;
        start->kino.standingPlatformId = -1544;
        sim.reset(*start);
        Input down;
        down.down = true;
        sim.tick(down);
        check(sim.world().kino.dropThroughTimer > 0 && sim.world().kino.y > 464, "one-way drop");
        CameraState camera;
        camera.update(sim.world(), 0, true);
        sim.world().state = TimeState::Trace;
        sim.world().kino.x = 1800;
        camera.update(sim.world(), fixedDt);
        check(camera.logicalX == camera.targetX, "rewind camera has no smoothing");
        float logical = sim.world().kino.x;
        camera.project(logical, 0);
        check(sim.world().kino.x == logical, "render snap never alters physics");

        for (int hz : {30, 60, 120}) {
            start->kino = {};
            start->rewind.record_hz = hz;
            start->rewind.history_seconds = 12;
            sim.reset(*start);
            for (int i = 0; i < 780; ++i)
                sim.tick(right);
            check(std::abs(sim.history().seconds() -
                           std::min(12.0, double(sim.history().capacity() - 1) / hz)) < .05,
                  "12-second window at each sample rate or budget");
            if (hz == 120)
                break;
        }
        // Budget really caps a 120 Hz long window; the UI reports the effective history.
        check(sim.history().allocatedBytes() <= sim.history().budgetBytes(), "hard memory budget");

        start->rewind = {};
        start->kino = {};
        start->bullets.clear();
        Bullet b;
        b.x = start->kino.x - 80;
        b.y = start->kino.y + 32;
        b.vx = 12000;
        b.expireAge = 5;
        spawnBullet(*start, b);
        sim.reset(*start);
        sim.tick({});
        check(sim.world().state == TimeState::Hit, "swept bullet catches tunneling");
        double hitTime = sim.world().clock;
        sim.tick(fast);
        check(sim.world().state == TimeState::Hit && sim.world().clock == hitTime, "Hit rejects R");
        sim.tick(trace);
        check(sim.world().state == TimeState::Trace || sim.world().state == TimeState::Free,
              "Hit allows T");
        start->rewind.enabled = false;
        sim.reset(*start);
        sim.tick({});
        sim.tick(trace);
        check(sim.world().state == TimeState::Hit, "disabled Hit waits for restart");
        Input restart;
        restart.restart = true;
        sim.tick(restart);
        check(sim.world().kino.hp == 1, "restart disabled Hit");

        start->bullets.clear();
        start->rewind.enabled = true;
        start->kino.invulnTimer = 100;
        sim.reset(*start);
        for (int i = 0; i < maxBullets + 100; ++i) {
            Bullet c;
            c.x = 120 + i % 60 * 18;
            c.y = 80 + i / 60 * 12;
            c.vx = std::cos(float(i)) * .1f;
            c.expireAge = 100;
            spawnBullet(sim.world(), c);
        }
        check(sim.world().bullets.count == maxBullets && sim.world().bulletsDropped == 100,
              "pool has deterministic overflow policy");
        auto clock = std::chrono::steady_clock::now();
        for (int i = 0; i < 600; ++i)
            sim.tick({});
        double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count() /
            600;
        sim.tick(fast);
        for (int i = 0; i < 80; ++i)
            sim.tick(fast);
        sim.tick({});
        sim.tick({});
        check(sim.world().state == TimeState::Free, "dense bullets farthest rewind and resume");
        std::cout << "PASS physics, camera, T/R, branching, sampling, Hit, sweep, pools\n";
        std::cout << "2048 bullets + full snapshots: " << ms << " ms/tick; memory "
                  << sim.history().allocatedBytes() / 1048576.0 << " MiB\n";
        if (argc > 1 && std::string(argv[1]) == "--benchmark")
            std::cout << "CPU benchmark includes record; GPU timing is separate.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
