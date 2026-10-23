#include "echo/snapshot_diff.hpp"
#include "echo/simulation.hpp"
#include "echo/script.hpp"
#include <bit>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
using namespace echo;
static bool countAllocations;
static std::size_t allocations;
void* operator new(std::size_t n) {
    if (countAllocations)
        ++allocations;
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
static void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
static void fresh(WorldState& w) { w = makeWorld(); }
static bool contains(const SnapshotDiff& r, std::string_view path) {
    for (auto& e : r.entries)
        if (e.field.c_str() == path)
            return true;
    return false;
}
int main(int argc, char** argv) {
    try {
        auto a = std::make_unique<WorldState>(), b = std::make_unique<WorldState>();
        fresh(*a);
        *b = *a;
        auto r = std::make_unique<SnapshotDiff>();
        compareSnapshots(*a, *b, *r);
        check(r->equal() && r->inspected > 100000, "every fixed slot is inspected");
        b->kino.jumpHeld = true;
        b->kino.jumpWasCut = true;
        b->bullets.values[2047].px = 123.25f; // inactive slots still matter to full restore
        b->objects.slots[255].generation = UINT32_MAX;
        b->events.self[8][31] = 1;
        b->script.persistent[63] = .125;
        b->script.serialized.data[4095] = 'X';
        b->nextBulletId = UINT32_MAX;
        b->kino.animation.requested = UINT64_MAX;
        b->level.layers[2].cells.back() = 3;
        b->hurt.regions[7].vertices.values[15].x = .5f;
        b->barrage.emitters[63].seed = 7;
        b->audio.voices.values[15].age = 3;
        b->transition.config.direction = 3;
        b->aster.attack.zones.values[15].entered = true;
        auto beforeA = std::make_unique<WorldState>(*a), beforeB = std::make_unique<WorldState>(*b);
        countAllocations = true;
        compareSnapshots(*a, *b, *r);
        countAllocations = false;
        check(allocations == 0, "comparison has no C++ heap allocation");
        check(r->differences == 15 && r->matching == 15,
              "selected changes have named exact differences");
        check(contains(*r, "kino.jumpHeld") && contains(*r, "kino.jumpWasCut") &&
                  contains(*r, "bullets.values[2047].px") &&
                  contains(*r, "objects.slots[255].generation") && contains(*r, "events.self[8][31]") &&
                  contains(*r, "script.serialized.data[4095]") &&
                  contains(*r, "hurt.regions[7].vertices.values[15].x"),
              "nested / inactive / text tail paths");
        check(std::memcmp(a.get(), beforeA.get(), sizeof(WorldState)) == 0 &&
                  std::memcmp(b.get(), beforeB.get(), sizeof(WorldState)) == 0,
              "comparison is read-only");
        compareSnapshots(*a, *b, *r, "script.");
        check(r->differences == 15 && r->matching == 2 && r->entries.count == 2,
              "filter counts vs global counts");
        compareSnapshots(*a, *b, *r, "does-not-exist");
        check(r->differences == 15 && r->matching == 0 && !r->equal(),
              "empty filter results are not equality");
        *b = *a;
        b->kino.vx = -0.f;
        compareSnapshots(*a, *b, *r);
        check(r->differences == 1 && contains(*r, "kino.vx"), "signed zero difference is visible");
        a->kino.vx = std::bit_cast<float>(std::uint32_t(0x7fc00001));
        b->kino.vx = a->kino.vx;
        compareSnapshots(*a, *b, *r);
        check(r->equal(), "identical NaN payload is stable");
        b->kino.vx = std::bit_cast<float>(std::uint32_t(0x7fc00002));
        compareSnapshots(*a, *b, *r);
        check(r->differences == 1, "different NaN payload is visible");
        fresh(*a);
        *b = *a;
        constexpr auto padding = offsetof(AnimationBox, enabled) + sizeof(bool);
        if constexpr (padding < sizeof(AnimationBox)) {
            auto bytes = reinterpret_cast<unsigned char*>(&b->kino.animation.collision);
            bytes[padding] ^= 0x55;
            compareSnapshots(*a, *b, *r);
            check(r->equal(), "C++ padding does not create false differences");
        }
        a->level.tiles.fill(Tile::Empty);
        *b = *a;
        for (auto& tile : b->level.tiles)
            tile = Tile::Solid;
        b->kino.jumpHeld = true;
        compareSnapshots(*a, *b, *r);
        check(r->differences == mapWidth * mapHeight + 1 && r->entries.count == maxSnapshotDifferences &&
                  r->truncated(),
              "bounded output counts all differences without early scan termination");
        compareSnapshots(*a, *b, *r, "kino.jumpHeld");
        check(r->matching == 1 && !r->truncated(), "filter recovers entries beyond output cap");
        b->bullets.count = UINT32_MAX;
        compareSnapshots(*a, *b, *r, "bullets.count");
        check(r->matching == 1, "damaged counts cannot cause out-of-bounds diagnostic access");
        fresh(*a);
        History h(4 * sizeof(WorldState));
        h.prepare(true);
        h.clear(HistoryClearReason::Restart);
        h.record(*a);
        for (int i = 0; i < 10; ++i) {
            a->clock += 1. / 60;
            h.record(*a);
        }
        auto d = h.diagnostics(*a);
        check(d.budgetLimited && d.frames == 4 && d.budgetEvictions == 7 &&
                  d.lastClear == HistoryClearReason::Restart,
              "capacity / clear / eviction explanation");
        h.truncateToCursor(1);
        d = h.diagnostics(*a);
        check(d.futureDiscarded == 2 && d.building, "branch rebuild explanation");
        a->rewind.history_seconds = 1;
        h.record(*a);
        check(h.diagnostics(*a).windowReduced, "window reduction detected");
        a->state = TimeState::Hit;
        a->gameplayClockPaused = true;
        check(h.diagnostics(*a).recordingPaused, "Hit freeze explained");
        a->dialogue.active = 0;
        check(!h.diagnostics(*a).recordingPaused, "dialogue still records its text / audio clock");
        a->dialogue.active = -1;
        a->state = TimeState::Trace;
        check(h.diagnostics(*a).rewinding, "rewind cursor is distinguished from accumulating");
        for (int hz : {30, 60, 120}) {
            fresh(*a);
            a->aster.enabled = false;
            a->kino.invulnTimer = 10000;
            a->jump.gravity = 0;
            a->level.tiles.fill(Tile::Empty);
            a->level.platforms.clear();
            a->level.colliders.clear();
            a->level.events.clear();
            a->level.dialogues.clear();
            a->level.portals.clear();
            a->rewind.record_hz = hz;
            a->focus.enabled = true;
            a->script.persistent[63] = 918.5;
            a->script.program.push({OpCode::Fire, 0, 120, 2, 0});
            a->script.program.push({OpCode::Wait, .5f});
            Simulation sim(384ull * 1024 * 1024);
            sim.reset(*a);
            sim.tick({});
            *beforeB = sim.world();
            for (int i = 0; i < 420; ++i)
                sim.tick({});
            auto oldest = std::make_unique<WorldState>(sim.history().at(0));
            sim.restore(*oldest);
            compareSnapshots(*oldest, sim.world(), *r);
            check(r->equal(),
                  "farthest restore is named-field exact, including arrays / px / script state");
            sim.truncateToCursor(0);
            sim.tick({});
            check(sim.world().clock > oldest->clock && sim.world().script.persistent[63] == 918.5,
                  "continue from oldest with persistent state");
            for (int i = 0; i < 420; ++i)
                sim.tick({});
            for (bool fast : {false, true}) {
                Input rewind;
                rewind.fast = fast;
                rewind.trace = !fast;
                int guard = 0;
                double futureClock = sim.world().clock;
                do {
                    sim.tick(rewind);
                    ++guard;
                } while (
                    (sim.world().state == TimeState::Fast || sim.world().state == TimeState::Trace) &&
                    guard < 1000);
                check(guard < 1000 && sim.world().clock < futureClock, "T/R farthest ends in the past");
                sim.tick({});
                sim.tick({});
                check(sim.world().state != TimeState::Fast && sim.world().state != TimeState::Trace &&
                          sim.world().script.persistent[63] == 918.5,
                      "continue after farthest T/R with snapshot script state");
                for (int i = 0; i < 420; ++i)
                    sim.tick({});
            }
            sim.configureRewind({6, hz, false, 5, 1});
            check(sim.history().diagnostics(sim.world()).lastClear == HistoryClearReason::Disabled &&
                      !sim.history().size(),
                  "disable clear explained");
            sim.configureRewind({6, hz, true, 5, 1});
            check(sim.history().diagnostics(sim.world()).lastClear == HistoryClearReason::Enabled,
                  "enable rebuild explained");
            sim.world().rewind.enabled = false;
            sim.tick({});
            check(sim.history().diagnostics(sim.world()).lastClear == HistoryClearReason::ScriptDisabled,
                  "runtime script toggle explained");
            sim.world().rewind.enabled = true;
            sim.tick({});
            check(sim.history().diagnostics(sim.world()).lastClear == HistoryClearReason::ScriptEnabled,
                  "runtime script enable explained");
            sim.tick({.restart = true});
            check(sim.history().diagnostics(sim.world()).lastClear == HistoryClearReason::Restart,
                  "restart clear explained");
        }
        if (argc > 1) {
            auto root = std::filesystem::path(argv[1]) / "snapshot-hooks-test";
            std::filesystem::create_directories(root);
            writeTextAtomically(root / "project.echo", "scripts=serialized.lua\n");
            writeTextAtomically(root / "serialized.lua",
                                "local n=0\nfunction on_key_press(k) if k=='Space' then "
                                "n=n+1;engine.state[64]=n end end\n"
                                "function on_snapshot() return tostring(n) end\n"
                                "function on_restore(s) n=tonumber(s) or 0 end\n");
            Resources resources;
            resources.root = root;
            fresh(*a);
            ScriptRuntime script;
            script.load(resources, *a);
            for (int i = 0; i < 3; ++i)
                script.key(*a, "Space");
            script.beforeSnapshot(*a);
            *b = *a;
            script.key(*a, "Space");
            script.beforeSnapshot(*a);
            compareSnapshots(*b, *a, *r, "script.");
            check(r->matching == 2 && contains(*r, "script.persistent[63]") &&
                      contains(*r, "script.serialized.data[0]"),
                  "Lua captured state differences are named");
            restore(*a, *b);
            script.afterRestore(*a);
            script.key(*a, "Space");
            check(a->script.persistent[63] == 4, "Lua closure resumes from restored serialized state");
        }
        fresh(*a);
        a->rewind.enabled = false;
        Simulation off(64ull * 1024 * 1024);
        off.reset(*a);
        auto empty = off.history().diagnostics(off.world());
        check(!empty.enabled && !empty.reservedBytes && !empty.frames,
              "startup disabled diagnostics allocate no history");
        std::cout
            << "PASS named snapshot comparison / full arrays / exact floats / padding / bounded filters "
               "/ "
               "zero heap / clear and capacity explanations / all Hz oldest restore and T/R resume\n";
    } catch (const std::exception& e) {
        countAllocations = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
