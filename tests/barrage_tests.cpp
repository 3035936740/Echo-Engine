#include "echo/barrage.hpp"
#include "echo/simulation.hpp"
#include "echo/resources.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <new>
#include <windows.h>
#include <psapi.h>
std::size_t allocations{};
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
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void bad(F f) {
    bool failed = false;
    try {
        f();
    } catch (...) {
        failed = true;
    }
    check(failed, "Invalid barrage accepted");
}
BarrageLibrary library() {
    BarrageLibrary l;
    auto ring = barragePreset(allocateBarrageId(l), 0);
    ring.damage = 0;
    ring.speed = 60;
    ring.acceleration = 40;
    ring.maxSpeed = 200;
    ring.lifetime = 8;
    l.patterns.push_back(ring);
    auto fan = barragePreset(allocateBarrageId(l), 1);
    fan.damage = 0;
    fan.count = 3;
    fan.group = 2;
    l.patterns.push_back(fan);
    BarrageEmitter e;
    e.id = allocateBarrageId(l);
    e.name.set("Combination");
    e.seed = 17;
    e.position = {600, 240};
    e.loopDelay = .3f;
    e.waves = {{ring.id, 3, 0, .85f, .2f, 15}, {fan.id, 2, .1f, .85f, .4f, 0}};
    l.emitters.push_back(e);
    return l;
}
void empty(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.script = {};
    w.kino.invulnTimer = 100000;
}
void run(WorldState& w, const BarrageLibrary& l, int frames) {
    SimulationStats stats;
    for (int i = 0; i < frames; ++i) {
        w.clock += fixedDt;
        ++w.tick;
        stepBarrage(w, l, fixedDt);
        stepBullets(w, fixedDt, {w.kino.x, w.kino.y}, stats, nullptr);
    }
}
void equal(const WorldState& a, const WorldState& b) {
    check(a.bullets.count == b.bullets.count && a.nextBulletId == b.nextBulletId,
          "Restored bullet count / allocator");
    for (std::size_t i = 0; i < a.bullets.count; ++i) {
        auto x = a.bullets[i], y = b.bullets[i];
        check(x.id == y.id && x.group == y.group && x.x == y.x && x.y == y.y && x.px == y.px &&
                  x.py == y.py && x.vx == y.vx && x.vy == y.vy && x.age == y.age,
              "Restored trajectory mismatch");
    }
    for (std::size_t i = 0; i < maxBarrageEmitters; ++i) {
        const auto& x = a.barrage.emitters[i];
        const auto& y = b.barrage.emitters[i];
        check(x.seed == y.seed && x.wave == y.wave && x.shot == y.shot && x.elapsed == y.elapsed &&
                  x.locked == y.locked && x.angle == y.angle && x.target.x == y.target.x &&
                  x.target.y == y.target.y && x.targetId == y.targetId &&
                  x.targetHandle.slot == y.targetHandle.slot &&
                  x.targetHandle.generation == y.targetHandle.generation &&
                  x.targetValid == y.targetValid && x.waitingForTarget == y.waitingForTarget,
              "Restored emitter mismatch");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        auto l = library();
        auto text = saveBarrage(l);
        check(saveBarrage(loadBarrage(text)) == text, "Barrage roundtrip");
        bad([&] { loadBarrage(text + "junk"); });
        bad([&] { loadBarrage(text.substr(0, text.size() / 2)); });
        auto invalid = l;
        invalid.emitters[0].waves[0].pattern = 999;
        bad([&] { validateBarrage(invalid); });
        invalid = l;
        invalid.patterns[0].damage = 1;
        invalid.emitters[0].waves[0].warning = .8f;
        bad([&] { validateBarrage(invalid); });
        invalid = l;
        invalid.emitters[0].seed = 0;
        bad([&] { validateBarrage(invalid); });
        invalid = l;
        invalid.patterns[0].count = 513;
        bad([&] { validateBarrage(invalid); });
        auto world = std::make_unique<WorldState>();
        empty(*world);
        resetBarrage(*world, l);
        auto restored = std::make_unique<WorldState>(*world);
        run(*world, l, 600);
        run(*restored, l, 600);
        equal(*world, *restored);
        *restored = *world;
        run(*world, l, 180);
        auto expected = std::make_unique<WorldState>(*world);
        *world = *restored;
        run(*world, l, 180);
        equal(*world, *expected);
        auto group1 = std::count_if(world->bullets.begin(), world->bullets.end(),
                                    [](const auto& b) { return b.group == 1; });
        check(clearBulletGroup(*world, 2) > 0 && world->bullets.count == std::uint32_t(group1),
              "Group clear isolation");
        check(setBarrageEmitter(*world, l.emitters[0].id, false), "Emitter control");
        auto volleys = world->barrage.volleys;
        run(*world, l, 180);
        check(world->barrage.volleys == volleys, "Disabled emitter fired");

        // Lock the target at warning start, then move it before release.
        auto aim = library();
        aim.patterns[0].aim = BarrageAim::AtWarning;
        aim.patterns[0].shape = BarrageShape::Stream;
        aim.patterns[0].count = 1;
        aim.patterns[0].direction = 0;
        aim.emitters[0].waves = {{aim.patterns[0].id, 1, 0, 1, 1, 0}};
        aim.emitters[0].loop = false;
        empty(*world);
        world->kino.x = 900;
        world->kino.y = 208;
        resetBarrage(*world, aim);
        run(*world, aim, 1);
        float locked = world->barrage.emitters[0].angle;
        world->kino.y = 500;
        run(*world, aim, 59);
        check(world->bullets.count == 1 && world->bullets[0].angle == locked,
              "Warning target lock changed");
        aim.patterns[0].aim = BarrageAim::AtFire;
        empty(*world);
        world->kino.x = 900;
        world->kino.y = 208;
        resetBarrage(*world, aim);
        run(*world, aim, 1);
        world->kino.y = 500;
        run(*world, aim, 59);
        check(world->bullets.count == 1 && world->bullets[0].angle > locked + .3f,
              "Fire-time target did not update");

        // Target identity, faction filtering, range and deterministic ties are independent of
        // bullet count. The author selects objects without relying on a transient pool index.
        empty(*world);
        auto targetTemplate = objectPreset(1, 2);
        targetTemplate.gravity = false;
        targetTemplate.behavior = ObjectBehavior::Idle;
        targetTemplate.faction = 7;
        targetTemplate.width = targetTemplate.height = 20;
        auto first = createObject(*world, targetTemplate, {690, 230}, 0, 101);
        auto secondTarget = createObject(*world, targetTemplate, {490, 230}, 0, 102);
        auto wrongMap = createObject(*world, targetTemplate, {590, 230}, 1, 103);
        check(resolveObject(*world, first) && resolveObject(*world, secondTarget) &&
                  resolveObject(*world, wrongMap),
              "Target fixtures");
        auto& aimingEmitter = aim.emitters[0];
        aimingEmitter.target = BarrageTarget::NearestObject;
        aimingEmitter.factionFilter = BarrageFactionFilter::Only;
        aimingEmitter.targetFaction = 7;
        BarrageTargetResult result;
        auto beforeQuery = allocations;
        check(queryBarrageTarget(*world, aimingEmitter, {600, 240}, result) &&
                  result.handle.slot == first.slot && allocations == beforeQuery,
              "Nearest target / equal-distance stable ID / allocation-free query");
        aimingEmitter.targetRange = 99;
        check(!queryBarrageTarget(*world, aimingEmitter, {600, 240}, result) && result.id == 0,
              "Nearest target exceeded range");
        aimingEmitter.targetRange = 100;
        check(queryBarrageTarget(*world, aimingEmitter, {600, 240}, result),
              "Target radius boundary was excluded");
        aimingEmitter.factionFilter = BarrageFactionFilter::Except;
        check(!queryBarrageTarget(*world, aimingEmitter, {600, 240}, result),
              "Excluded faction selected");
        aimingEmitter.factionFilter = BarrageFactionFilter::Only;
        aimingEmitter.origin = BarrageOrigin::Object;
        aimingEmitter.object = 101;
        check(queryBarrageTarget(*world, aimingEmitter, {600, 240}, result) &&
                  result.handle.slot == secondTarget.slot,
              "Parent self-target exclusion");
        aimingEmitter.excludeParent = false;
        check(queryBarrageTarget(*world, aimingEmitter, {600, 240}, result) &&
                  result.handle.slot == first.slot,
              "Optional self-target inclusion");
        aimingEmitter.origin = BarrageOrigin::World;
        aimingEmitter.target = BarrageTarget::Object;
        aimingEmitter.targetObject = 102;
        check(queryBarrageTarget(*world, aimingEmitter, {600, 240}, result) &&
                  result.handle.slot == secondTarget.slot,
              "Authored target stable placement ID");
        aimingEmitter.targetFaction = 8;
        check(!queryBarrageTarget(*world, aimingEmitter, {600, 240}, result),
              "Specific object bypassed faction filter");
        aimingEmitter.targetFaction = 7;
        aimingEmitter.target = BarrageTarget::Boss;
        check(!queryBarrageTarget(*world, aimingEmitter, {600, 240}, result),
              "Dead / disabled Boss selected");
        world->aster.enabled = true;
        world->aster.hp = 500;
        check(queryBarrageTarget(*world, aimingEmitter, {600, 240}, result), "Boss target");
        aimingEmitter.target = BarrageTarget::Point;
        aimingEmitter.targetPoint = {123, 456};
        check(queryBarrageTarget(*world, aimingEmitter, {600, 240}, result) &&
                  result.position.x == 123 && result.position.y == 456,
              "Fixed point target");
        auto targetText = saveBarrage(aim);
        check(saveBarrage(loadBarrage(targetText)) == targetText, "Target v2 exact roundtrip");
        auto legacy = saveBarrage(l);
        std::istringstream rows(legacy);
        std::ostringstream migrated;
        std::string row;
        std::getline(rows, row);
        row.replace(0, 14, "ECHO_BARRAGE_1");
        migrated << row << '\n';
        for (std::size_t i = 0; i < l.patterns.size(); ++i) {
            std::getline(rows, row);
            migrated << row << '\n';
        }
        std::getline(rows, row);
        for (int i = 0; i < 8; ++i)
            row.erase(row.find_last_of(' '));
        migrated << row << '\n' << rows.rdbuf();
        check(loadBarrage(migrated.str()).emitters[0].target == BarrageTarget::Player,
              "Legacy v1 target default migration");
        invalid = aim;
        invalid.emitters[0].target = BarrageTarget(255);
        bad([&] { validateBarrage(invalid); });
        invalid = aim;
        invalid.emitters[0].targetRange = -1;
        bad([&] { validateBarrage(invalid); });
        invalid = aim;
        invalid.emitters[0].target = BarrageTarget::Object;
        invalid.emitters[0].targetObject = 0;
        bad([&] { validateBarrage(invalid); });

        // At-warning coordinates remain valid after destruction; stale handles are never followed.
        empty(*world);
        auto dynamicTarget = createObject(*world, targetTemplate, {890, 230}, 0);
        aim.patterns[0].aim = BarrageAim::AtWarning;
        aimingEmitter.target = BarrageTarget::NearestObject;
        aimingEmitter.factionFilter = BarrageFactionFilter::Only;
        aimingEmitter.targetRange = 0;
        resetBarrage(*world, aim);
        run(*world, aim, 1);
        auto targetSnapshot = std::make_unique<WorldState>(*world);
        locked = world->barrage.emitters[0].angle;
        auto oldId = world->barrage.emitters[0].targetId;
        check(oldId && world->barrage.emitters[0].targetHandle.generation == dynamicTarget.generation,
              "Warning target identity not snapshotted");
        destroyObject(*world, dynamicTarget);
        auto reused = createObject(*world, targetTemplate, {590, 430}, 0);
        check(reused.slot == dynamicTarget.slot && reused.generation != dynamicTarget.generation,
              "Target generation fixture not reused");
        run(*world, aim, 59);
        check(world->bullets.count == 1 && world->bullets[0].angle == locked &&
                  world->barrage.emitters[0].targetId == oldId,
              "Warning followed reused target slot");
        *world = *targetSnapshot;
        check(resolveObject(*world, world->barrage.emitters[0].targetHandle),
              "Restore failed to resurrect target identity");
        run(*world, aim, 59);
        check(world->bullets.count == 1 && world->bullets[0].angle == locked,
              "Restored warning target changed");

        // At-fire loss pauses and requires a full new warning after acquisition.
        empty(*world);
        aim.patterns[0].aim = BarrageAim::AtFire;
        dynamicTarget = createObject(*world, targetTemplate, {890, 230}, 0);
        resetBarrage(*world, aim);
        run(*world, aim, 40);
        destroyObject(*world, dynamicTarget);
        run(*world, aim, 120);
        check((world->bullets.count == 0) && world->barrage.emitters[0].waitingForTarget &&
                  world->barrage.emitters[0].elapsed == 0 && world->barrage.missingTargets == 120,
              "Lost target did not pause / re-arm");
        auto waitingSnapshot = std::make_unique<WorldState>(*world);
        dynamicTarget = createObject(*world, targetTemplate, {890, 430}, 0);
        run(*world, aim, 59);
        check((world->bullets.count == 0) && !world->barrage.emitters[0].waitingForTarget &&
                  world->barrage.emitters[0].targetHandle.generation == dynamicTarget.generation,
              "Reacquisition skipped the complete warning");
        run(*world, aim, 1);
        check(world->bullets.count == 1 && world->bullets[0].angle > .3f,
              "Reacquired target not used at fire");
        *world = *waitingSnapshot;
        run(*world, aim, 60);
        check((world->bullets.count == 0) && world->barrage.emitters[0].waitingForTarget,
              "Restore lost waiting state");
        setBarrageEmitter(*world, aimingEmitter.id, true, true);
        check(!world->barrage.emitters[0].waitingForTarget && !world->barrage.emitters[0].targetValid &&
                  !world->barrage.emitters[0].targetId,
              "Emitter restart retained target lock");

        // Catch-up is bounded to one target scan per emitter per tick, with no allocation.
        empty(*world);
        createObject(*world, targetTemplate, {890, 230}, 0);
        aim.emitters[0].waves[0] = {aim.patterns[0].id, 64, 0, 0, fixedDt, 0};
        resetBarrage(*world, aim);
        beforeQuery = allocations;
        stepBarrage(*world, aim, 1);
        check(allocations == beforeQuery && world->barrage.volleys == 61 &&
                  world->barrage.targetQueries == 1 && world->barrage.targetCandidates == maxObjects,
              "Catch-up rescanned objects per salvo or allocated");
        aim.patterns[0].aim = BarrageAim::Fixed;
        resetBarrage(*world, aim);
        stepBarrage(*world, aim, 1);
        check(world->barrage.targetQueries == 0, "Fixed-direction pattern queried targets");

        // Maximum target workload: 64 emitters / 256 live candidates. Quantify query cost
        // separately from bullet movement, snapshot copies, and rendering.
        auto targetStress = library();
        targetStress.patterns[0].aim = BarrageAim::AtFire;
        targetStress.patterns[0].shape = BarrageShape::Stream;
        targetStress.patterns[0].count = 1;
        targetStress.emitters.clear();
        for (int i = 0; i < maxBarrageEmitters; ++i) {
            BarrageEmitter e;
            e.id = allocateBarrageId(targetStress);
            e.name.set("Target stress");
            e.target = BarrageTarget::NearestObject;
            e.factionFilter = BarrageFactionFilter::Only;
            e.targetFaction = 7;
            e.waves = {{targetStress.patterns[0].id, 1, 0, 60, 1, 0}};
            targetStress.emitters.push_back(e);
        }
        empty(*world);
        for (int i = 0; i < int(maxObjects); ++i)
            check(resolveObject(*world,
                                createObject(*world, targetTemplate,
                                             {700 + float(i % 16) * 8, 240 + float(i / 16) * 8}, 0)),
                  "Maximum target objects could not be created");
        resetBarrage(*world, targetStress);
        std::vector<double> targetTimes;
        targetTimes.reserve(600);
        beforeQuery = allocations;
        for (int i = 0; i < 600; ++i) {
            auto start = std::chrono::steady_clock::now();
            stepBarrage(*world, targetStress, fixedDt);
            targetTimes.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count());
            check(world->barrage.targetQueries == maxBarrageEmitters &&
                      world->barrage.targetCandidates == maxBarrageEmitters * maxObjects,
                  "Target workload exceeded / omitted bounded queries");
        }
        check(allocations == beforeQuery, "Maximum target workload allocated");
        std::sort(targetTimes.begin(), targetTimes.end());
        double targetP95 = targetTimes[targetTimes.size() * 95 / 100];
        double targetP99 = targetTimes[targetTimes.size() * 99 / 100];
        double targetWorst = targetTimes.back();

        // Terrain CCD: a single tick crosses a thin wall. A wall shields a later player hit.
        empty(*world);
        world->level.tiles.fill(Tile::Empty);
        world->level.tiles[5 * mapWidth + 5] = Tile::Solid;
        world->kino.x = 240;
        world->kino.y = 144;
        world->kino.invulnTimer = 0;
        Bullet b;
        b.x = 100;
        b.y = 176;
        b.vx = 15000;
        b.maxSpeed = 20000;
        b.terrain = 1;
        spawnBullet(*world, b);
        SimulationStats stats;
        stepBullets(*world, fixedDt, {world->kino.x, world->kino.y}, stats, nullptr);
        check(!world->bullets.count && world->kino.hp == 1 && stats.terrainCandidates > 0,
              "Wall shield / CCD failed");
        world->kino.x = 110;
        spawnBullet(*world, b);
        stepBullets(*world, fixedDt, {world->kino.x, world->kino.y}, stats, nullptr);
        check(world->state == TimeState::Hit, "Earlier player contact ignored");

        // Deterministic capacity policy, including refusal to recycle another group.
        empty(*world);
        for (int i = 0; i < maxBullets; ++i) {
            b = {};
            b.x = 600;
            b.y = 200;
            b.age = float(i);
            b.group = 1;
            spawnBullet(*world, b);
        }
        auto next = world->nextBulletId;
        b.group = 2;
        check(!spawnBullet(*world, b) && world->bulletsDropped == 1, "Reject capacity policy");
        world->bulletOverflow = BulletOverflowPolicy::RecycleGroup;
        check(!spawnBullet(*world, b), "Group recycle stole another group");
        world->bulletOverflow = BulletOverflowPolicy::RecycleOldest;
        check(spawnBullet(*world, b) && world->bullets[maxBullets - 1].id == next + 2 &&
                  world->bulletsRecycled == 1,
              "Oldest deterministic recycle");
        world->nextBulletId = UINT32_MAX;
        check(!spawnBullet(*world, b), "Bullet ID wrapped");

        // Emitter gates, parent lifetime, map reset/persistence and event form operations.
        l = library();
        empty(*world);
        resetBarrage(*world, l);
        l.emitters[0].gateSwitch = 5;
        auto allocationBefore = allocations;
        run(*world, l, 20);
        check(allocations == allocationBefore && world->barrage.emitters[0].elapsed == 0,
              "Closed gate advanced or allocated");
        world->events.switches[5] = 1;
        run(*world, l, 20);
        check(world->barrage.emitters[0].elapsed > 0, "Open gate failed");
        auto progress = world->barrage.emitters[0].elapsed;
        world->activeMap = 1;
        enterBarrageMap(*world, l);
        check(!world->barrage.emitters[0].warning, "Old map warning leaked");
        world->activeMap = 0;
        enterBarrageMap(*world, l);
        check(world->barrage.emitters[0].elapsed == 0, "Nonpersistent emitter did not reset");
        l.emitters[0].persistent = true;
        run(*world, l, 20);
        progress = world->barrage.emitters[0].elapsed;
        world->activeMap = 1;
        enterBarrageMap(*world, l);
        world->activeMap = 0;
        enterBarrageMap(*world, l);
        check(world->barrage.emitters[0].elapsed == progress, "Persistent emitter lost cursor");
        auto& emitter = l.emitters[0];
        emitter.origin = BarrageOrigin::Object;
        emitter.object = 123;
        auto t = objectPreset(1, 2);
        t.gravity = false;
        auto handle = createObject(*world, t, {600, 240}, 0, 123);
        run(*world, l, 1);
        auto clock = world->barrage.emitters[0].elapsed;
        destroyObject(*world, handle);
        run(*world, l, 1);
        check(world->barrage.emitters[0].elapsed == clock && world->barrage.missingParents > 0,
              "Deleted parent continued emission");
        emitter.origin = BarrageOrigin::World;
        emitter.gateSwitch = -1;
        resetBarrage(*world, l);
        EventCommand control;
        control.op = EventOp::EmitterControl;
        control.reference = emitter.id;
        control.b = 0;
        EventCommand clear;
        clear.op = EventOp::ClearBulletGroup;
        clear.a = 1;
        EventDefinition event;
        event.id = 1;
        event.name.set("Control barrage");
        EventPage page;
        page.trigger = EventTrigger::Parallel;
        page.commands = {control, clear};
        event.pages.push_back(page);
        EventMap common;
        common.events.push_back(event);
        check(saveEvents(loadEvents(saveEvents(common, true), true)) == saveEvents(common, true),
              "Version two event barrage reference roundtrip");
        b = {};
        b.x = 600;
        b.y = 240;
        b.group = 1;
        spawnBullet(*world, b);
        b.group = 2;
        spawnBullet(*world, b);
        stepEvents(*world, {}, common, false, fixedDt);
        check(!world->barrage.emitters[0].enabled && world->bullets.count == 1 &&
                  world->bullets[0].group == 2,
              "No-script event control / grouped clear");
        l = library();
        l.patterns[0].count = 512;
        empty(*world);
        resetBarrage(*world, l);
        for (int i = 0; i < 10; ++i)
            fireBarrage(*world, l.patterns[0], {600, 240}, 0, world->randomSeed);
        check(world->barrage.attemptBudget == 0 && world->barrage.budgetRejected == 1024 &&
                  world->bullets.count == maxBullets,
              "Bounded salvo budget diagnostic");

        // Formation growth / rotation / release and acceleration survive the farthest cursor.
        l = library();
        l.patterns[0] = barragePreset(l.patterns[0].id, 4);
        l.patterns[0].damage = 0;
        l.patterns[0].releaseSeconds = .2f;
        l.patterns[0].aim = BarrageAim::AtFire;
        l.emitters[0].target = BarrageTarget::NearestObject;
        l.emitters[0].factionFilter = BarrageFactionFilter::Only;
        l.emitters[0].targetFaction = 7;
        for (int hz : {30, 60, 120}) {
            empty(*world);
            world->rewind.history_seconds = 6;
            world->rewind.record_hz = hz;
            createObject(*world, targetTemplate, {890, 230}, 0);
            Simulation sim(384ull * 1024 * 1024);
            sim.setBarrage(l);
            sim.reset(*world);
            for (int i = 0; i < 420; ++i)
                sim.tick({});
            check(sim.history().size() >= std::size_t(6 * hz), "Full six-second target history missing");
            auto old = std::make_unique<WorldState>(sim.history().at(0));
            sim.seekHistory(0);
            for (int i = 0; i < 60; ++i)
                sim.tick({});
            auto continuation = std::make_unique<WorldState>(sim.world());
            Simulation second(384ull * 1024 * 1024);
            second.setBarrage(l);
            second.reset(*world);
            second.restore(*old);
            for (int i = 0; i < 60; ++i)
                second.tick({});
            equal(sim.world(), second.world());
            sim.tick({.trace = true});
            sim.tick({});
            sim.tick({.fast = true});
            sim.tick({});
            check(sim.world().kino.hp > 0, "Rewind hurt an immune barrage player");
        }

        // Long fixed-seed stress: measure percentiles, worst tick, collision candidates and resident
        // working set.
        std::vector<double> times;
        times.reserve(18000);
        empty(*world);
        world->level.tiles.fill(Tile::Empty);
        world->kino.invulnTimer = 0;
        world->kino.x = 2800;
        world->kino.y = 700;
        world->bulletOverflow = BulletOverflowPolicy::RecycleOldest;
        world->rewind.history_seconds = 1;
        History stressHistory(64ull * 1024 * 1024);
        stressHistory.prepare(true);
        auto initialize = [&](Bullet& bullet, int i) {
            bullet = {};
            bullet.x = 400 + float(i % 64) * 10;
            bullet.y = 100 + float(i / 64) * 8;
            bullet.vx = float(i % 13) - 6;
            bullet.vy = float(i % 7) - 3;
            bullet.expireAge = 100000;
            if (i % 4 == 0)
                bullet.motion = {float(i), 4, 5, 1};
            if (i % 8 == 0)
                bullet.terrain = 1;
        };
        for (int i = 0; i < maxBullets; ++i) {
            initialize(b, i);
            spawnBullet(*world, b);
        }
        auto allocationStart = allocations;
        for (int i = 0; i < 18000; ++i) {
            auto start = std::chrono::steady_clock::now();
            stepBullets(*world, fixedDt, {world->kino.x, world->kino.y}, stats, nullptr);
            while (world->bullets.count < maxBullets) {
                initialize(b, int(world->nextBulletId % 2048));
                spawnBullet(*world, b);
            }
            world->clock += fixedDt;
            ++world->tick;
            stressHistory.record(*world);
            times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count());
        }
        check(allocations == allocationStart,
              "Bullet movement / terrain / pool / history allocated in a tick");
        check(world->bullets.count == maxBullets, "Long stress lost its dense load");
        PROCESS_MEMORY_COUNTERS memory{};
        memory.cb = sizeof(memory);
        check(GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)),
              "Read peak resident memory");
        std::sort(times.begin(), times.end());
        std::string report =
            "ticks=18000\nsimulated_seconds=300\np95_ms=" +
            std::to_string(times[times.size() * 95 / 100]) +
            "\np99_ms=" + std::to_string(times[times.size() * 99 / 100]) +
            "\nworst_ms=" + std::to_string(times.back()) +
            "\nWorldState_bytes=" + std::to_string(sizeof(WorldState)) +
            "\nconstant_bullets=2048\nheap_allocations_in_ticks=0\nreserved_history_bytes=" +
            std::to_string(stressHistory.allocatedBytes()) +
            "\nstress_process_working_set_bytes=" + std::to_string(memory.WorkingSetSize) +
            "\npeak_process_working_set_bytes=" + std::to_string(memory.PeakWorkingSetSize) +
            "\ntarget_query_ticks=600\ntarget_emitters=64\ntarget_objects=256" +
            std::string("\ntarget_query_heap_allocations=0\ntarget_query_p95_ms=") +
            std::to_string(targetP95) + "\ntarget_query_p99_ms=" + std::to_string(targetP99) +
            "\ntarget_query_worst_ms=" + std::to_string(targetWorst) + "\n";
        if (argc > 1) {
            std::ofstream out(argv[1]);
            out << report;
        }
        std::cout << report << "Barrage tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
