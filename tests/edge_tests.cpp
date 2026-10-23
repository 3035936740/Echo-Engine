#include "echo/simulation.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
std::size_t allocations{};
void* operator new(std::size_t n) {
    ++allocations;
    if (auto p = std::malloc(n))
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
void check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
void resetWorld(WorldState& world) {
    // Keep the large returned value in a separate frame in unoptimized builds.
    world = makeWorld();
}
int main() {
    try {
        auto w = std::make_unique<WorldState>();
        resetWorld(*w);
        w->aster.enabled = false;
        w->kino.x = 150;
        w->level.tiles.fill(Tile::Empty);
        w->level.tiles[20 * mapWidth + 4] = Tile::Solid;
        Input right;
        right.move = 1;
        for (int i = 0; i < 30 && w->kino.grounded; ++i)
            stepPhysics(*w, right, fixedDt);
        check(!w->kino.grounded && w->kino.coyoteTimer > 0, "walk off ledge opens coyote window");
        Input jump;
        jump.jump = true;
        stepPhysics(*w, jump, fixedDt);
        check(w->kino.vy < -300, "coyote jump");
        resetWorld(*w);
        w->aster.enabled = false;
        w->kino.y = 560;
        w->kino.grounded = false;
        w->kino.coyoteTimer = 0;
        w->kino.vy = 200;
        stepPhysics(*w, jump, fixedDt);
        bool buffered = false;
        for (int i = 0; i < 8; ++i) {
            stepPhysics(*w, {}, fixedDt);
            if (w->kino.vy < -100)
                buffered = true;
        }
        check(buffered, "jump buffer fires on landing");
        check(w->soundEvent == 1 && w->soundKind == 1, "buffered landing jump emits one SE event");
        resetWorld(*w);
        w->level.tiles.fill(Tile::Empty);
        w->level.platforms.clear();
        MovingPlatform support;
        support.id = 7;
        support.base = {100, 248, 100, 8};
        support.pos = support.previous = {100, 248};
        w->level.platforms.push(support);
        w->kino.x = 120;
        w->kino.y = 200;
        w->kino.vy = 0;
        w->kino.jumpHeld = w->kino.jumpWasCut = true;
        refreshGroundContact(*w);
        check(w->kino.grounded && w->kino.standingPlatformId == 7 &&
                  w->kino.coyoteTimer == w->jump.coyote_time,
              "R landing finds the current moving platform");
        w->kino.x = 500;
        refreshGroundContact(*w);
        check(!w->kino.grounded && w->kino.standingPlatformId == -1 && w->kino.coyoteTimer == 0,
              "R airborne chord cannot inherit historical platform or coyote time");
        check(w->kino.jumpHeld && w->kino.jumpWasCut && w->kino.animState == KinoAnimState::JumpFall,
              "R support refresh preserves historical jump flags and updates animation");
        w->kino.x = 120;
        w->kino.dropThroughTimer = .2f;
        refreshGroundContact(*w);
        check(!w->kino.grounded, "R support refresh respects one-way drop timer");
        w->level.platforms.clear();
        w->kino.dropThroughTimer = 0;
        w->kino.y = 592;
        w->level.tiles[20 * mapWidth + 4] = Tile::Solid;
        w->kino.x = 136;
        refreshGroundContact(*w);
        check(w->kino.grounded, "R exit on solid floor remains supported");
        w->level.tiles[20 * mapWidth + 4] = Tile::OneWay;
        refreshGroundContact(*w);
        check(w->kino.grounded && w->kino.standingPlatformId < -1,
              "R exit can attach to a one-way tile");
        w->level.tiles.fill(Tile::Empty);
        StaticCollider floor;
        floor.bounds = {100, 640, 100, 16};
        w->level.colliders.push(floor);
        refreshGroundContact(*w);
        check(w->kino.grounded, "R exit finds an independent collider");
        w->kino.vy = -100;
        refreshGroundContact(*w);
        check(!w->kino.grounded && w->kino.vy == -100, "R upward velocity remains airborne");
        resetWorld(*w);
        w->kino.x = 1140;
        w->kino.y = 472;
        w->kino.standingPlatformId = 0;
        w->kino.grounded = true;
        float oldX = w->kino.x;
        stepPhysics(*w, {}, fixedDt);
        check(w->kino.x > oldX && w->kino.grounded, "moving platform carries rider");
        auto saved = std::make_unique<WorldState>(*w);
        for (int i = 0; i < 10; ++i)
            stepPhysics(*w, {}, fixedDt);
        float x = w->kino.x;
        restore(*w, *saved);
        for (int i = 0; i < 10; ++i)
            stepPhysics(*w, {}, fixedDt);
        check(w->kino.x == x, "moving platform deterministic after restore");

        resetWorld(*w);
        w->aster.x = 380;
        w->aster.enabled = true;
        w->kino.invulnTimer = 1000;
        Simulation sim(128ull * 1024 * 1024);
        sim.reset(*w);
        for (int i = 0; i < 180; ++i)
            sim.tick(right);
        Input fast;
        fast.fast = true;
        for (int i = 0; i < 80 && sim.world().aster.hp == 500; ++i)
            sim.tick(fast);
        check(sim.world().aster.hp < 500 && sim.world().aster.invulnTimer == 6 &&
                  sim.world().aster.knockbackActive,
              "R swept Boss damage and protection");
        check(std::abs(sim.world().aster.knockbackTargetX - sim.world().aster.knockbackStartX) <= 300,
              "bounded knockback");
        sim.tick({});
        check(sim.world().state == TimeState::Free, "R impact truncates and resumes");
        resetWorld(*w);
        w->aster.enabled = false;
        w->level.tiles.fill(Tile::Empty);
        w->level.platforms.clear();
        w->level.platforms.push(support);
        w->kino.x = 120;
        w->kino.y = 200;
        w->kino.vy = 0;
        w->kino.grounded = true;
        w->kino.standingPlatformId = 7;
        w->kino.jumpHeld = w->kino.jumpWasCut = true;
        sim.reset(*w);
        sim.world().clock = 1;
        sim.world().kino.x = 500;
        sim.world().kino.y = 400;
        sim.record();
        sim.tick(fast);
        sim.tick({});
        check(sim.world().kino.x > 400 && !sim.world().kino.grounded &&
                  sim.world().kino.standingPlatformId == -1 && sim.world().kino.coyoteTimer == 0,
              "partial R exit recomputes support at the new chord position");
        auto contactBranch = std::make_unique<WorldState>(sim.world());
        sim.tick({});
        float branchX = sim.world().kino.x, branchY = sim.world().kino.y;
        sim.restore(*contactBranch);
        sim.history().clear();
        sim.record();
        sim.tick({});
        check(branchX == sim.world().kino.x && branchY == sim.world().kino.y,
              "R contact correction is stored in the branch snapshot");
        sim.reset(*w);
        sim.world().clock = 1;
        sim.world().kino.x = 500;
        sim.world().kino.y = 400;
        sim.record();
        Input trace;
        trace.trace = true;
        sim.tick(trace);
        sim.tick({});
        check(sim.world().kino.x == 120 && sim.world().kino.grounded &&
                  sim.world().kino.standingPlatformId == 7 && sim.world().kino.jumpHeld &&
                  sim.world().kino.jumpWasCut,
              "T exit keeps the exact historical position and contact/jump flags");

        resetWorld(*w);
        w->aster.enabled = false;
        w->aster.attack.t = 2;
        Zone brake;
        brake.x = 400;
        brake.y = 560;
        brake.w = 12;
        brake.h = 80;
        brake.start = 1;
        brake.end = 5;
        brake.brake = true;
        w->aster.attack.zones.push(brake);
        sim.reset(*w);
        for (int i = 0; i < 180; ++i)
            sim.tick(right);
        for (int i = 0; i < 80; ++i)
            sim.tick(fast);
        check(sim.world().kino.x >= 410 && sim.world().state == TimeState::Free, "amber region stops R");
        sim.tick({});
        for (int i = 0; i < 120; ++i)
            sim.tick(right);
        sim.tick(fast);
        auto cfg = sim.world().rewind;
        cfg.enabled = false;
        sim.configureRewind(cfg);
        sim.tick(fast);
        check(sim.world().state != TimeState::Fast && !sim.world().rewind.enabled,
              "disable active rewind is safe");

        resetWorld(*w);
        w->aster.enabled = false;
        sim.reset(*w);
        std::size_t before = allocations;
        for (int i = 0; i < 400; ++i)
            sim.tick({});
        check(allocations == before, "steady physics/history makes zero C++ heap allocations");
        std::cout << "PASS coyote, buffer, platform replay, R Boss/brake, runtime disable, zero steady "
                     "allocations\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
