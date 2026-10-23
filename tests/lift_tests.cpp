#include "echo/platform_features.hpp"
#include "echo/script.hpp"
#include "echo/collision.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <new>
using namespace echo;
static bool measuring{};
static std::size_t allocations{};
void* operator new(std::size_t n) {
    if (measuring)
        ++allocations;
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void check(bool ok, const char* msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
bool near(float a, float b) {
    return std::abs(a - b) < .02f;
}
void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.script = {};
    w.kino.invulnTimer = 1000;
    w.level.tiles.fill(Tile::Empty);
    w.level.platforms.clear();
    w.level.features.clear();
    w.level.colliders.clear();
    w.level.events.clear();
    w.level.dialogues.clear();
    w.level.portals.clear();
    w.objects = {};
    w.jump.gravity = 0;
    w.kino.x = 100;
    w.kino.y = 352;
    w.kino.grounded = true;
    w.kino.standingPlatformId = -100001;
}
PlatformFeature& lift(WorldState& w, Vec2 travel = {0, -192}) {
    auto f = platformPreset(PlatformKind::Lift, {64, 400, 192, 16}, 1);
    f.travel = travel;
    f.speed = 192;
    f.delay = .25f;
    w.level.nextFeatureId = 2;
    w.level.features.push(f);
    return w.level.features[0];
}
void tick(WorldState& w, int n = 1, Input in = {}) {
    for (int i = 0; i < n; ++i)
        stepPhysics(w, in, fixedDt);
}
int main(int argc, char** argv) {
    try {
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        auto& f = lift(*w);
        tick(*w, 30);
        check(near(f.bounds.y, 304) && near(w->kino.y, 256) && w->kino.grounded,
              "vertical rider preserves feet attachment");
        tick(*w, 30);
        check(near(f.bounds.y, 208) && f.stage == 1, "endpoint reached without overshoot");
        tick(*w, 9);
        check(near(f.bounds.y, 208) && f.stage == 1, "endpoint dwell holds rider");
        tick(*w, 21);
        check(f.direction == -1 && near(f.bounds.y, 256) && w->kino.grounded,
              "dwell ends then return begins");
        f.speed = 0;
        auto stopped = std::make_unique<WorldState>(*w);
        tick(*w, 40);
        check(f.progress == stopped->level.features[0].progress &&
                  f.elapsed == stopped->level.features[0].elapsed,
              "speed zero freezes phase and dwell");

        fresh(*w);
        auto& one = lift(*w, {96, -128});
        one.speed = 160;
        one.delay = 0;
        one.repeat = false;
        tick(*w, 150);
        check(one.stage == 2 && one.progress == 1 && featureSupports(one) && near(w->kino.x, 196) &&
                  near(w->kino.y, 224) && w->kino.grounded,
              "diagonal once route stops and remains solid support");
        Input jump;
        jump.jump = true;
        tick(*w, 1, jump);
        check(!w->kino.grounded && w->kino.vy < 0, "jump detaches from completed lift");

        fresh(*w);
        auto& catchLift = lift(*w);
        catchLift.speed = 2400;
        catchLift.delay = 0;
        w->kino.y = 332;
        w->kino.grounded = false;
        w->kino.standingPlatformId = -1;
        tick(*w);
        check(w->kino.grounded && near(w->kino.y, 312) &&
                  w->kino.standingPlatformId == featureSupportId(catchLift),
              "ascending top sweeps stationary player without tunneling");
        fresh(*w);
        auto& rising = lift(*w);
        rising.speed = 4000;
        w->kino.y = 300;
        w->kino.vy = -100;
        w->kino.grounded = false;
        w->kino.standingPlatformId = -1;
        tick(*w);
        check(w->kino.grounded && near(w->kino.y, 400 - 4000 * fixedDt - 48),
              "relative crossing catches slower rising player");
        fresh(*w);
        auto& pass = lift(*w);
        pass.travel = {0, 192};
        pass.speed = 2400;
        w->kino.y = 410;
        w->kino.grounded = false;
        w->kino.standingPlatformId = -1;
        tick(*w);
        check(!w->kino.grounded, "one-way lift cannot catch from below");
        float t = 0;
        check(!movingSupportContact({0, 0, 28, 48}, {0, 0, 28, 48}, {100, 60, 32, 16}, {100, 40, 32, 16},
                                    t),
              "relative contact includes horizontal overlap");

        fresh(*w);
        lift(*w);
        StaticCollider ceiling;
        ceiling.bounds = {0, 336, 600, 16};
        w->level.colliders.push(ceiling);
        tick(*w);
        check(w->state == TimeState::Hit && w->kino.crushed && w->kino.hp == 0 && near(w->kino.y, 352),
              "ascending rider ceiling crush uses sweep and Hit");
        fresh(*w);
        lift(*w);
        w->mobility.crushKills = false;
        w->level.colliders.push(ceiling);
        tick(*w);
        check(w->state != TimeState::Hit && w->kino.hp > 0 && w->kino.crushed && !w->kino.grounded,
              "nonlethal crush detaches rider");

        fresh(*w);
        auto& down = lift(*w, {0, 192});
        down.speed = 240;
        StaticCollider floor;
        floor.bounds = {0, 400, 600, 32};
        w->level.colliders.push(floor);
        w->jump.gravity = 980;
        tick(*w, 6);
        check(w->kino.hp > 0 && !w->kino.crushed && near(w->kino.y, 352) &&
                  w->kino.standingPlatformId == -1 && w->kino.grounded,
              "descending support leaves rider safely on floor");

        fresh(*w);
        lift(*w, {96, 192});
        w->level.colliders.push(floor);
        w->jump.gravity = 980;
        tick(*w, 6);
        check(w->kino.hp > 0 && !w->kino.crushed && near(w->kino.y, 352) && w->kino.x > 100,
              "diagonal descending carry transfers to floor without fatal crush");

        fresh(*w);
        auto& reverse = lift(*w, {0, -2});
        reverse.speed = 240;
        reverse.delay = 0;
        ceiling.bounds = {0, 334, 600, 17};
        w->level.colliders.push(ceiling);
        tick(*w);
        check(w->kino.crushed && w->state == TimeState::Hit,
              "within-step end and reverse cannot hide an intermediate ceiling crush");
        fresh(*w);
        lift(*w);
        Input drop;
        drop.down = true;
        tick(*w, 2, drop);
        check(!w->kino.grounded && w->kino.dropThroughTimer > 0, "down-through works on ascending lift");
        fresh(*w);
        lift(*w);
        setCollisionPair(w->collisions, CollisionClass::Player, CollisionClass::Terrain, false);
        tick(*w, 5);
        check(near(w->kino.y, 352), "terrain filter disables lift carry");
        fresh(*w);
        auto& disabled = lift(*w);
        disabled.enabled = false;
        tick(*w, 5);
        check(disabled.progress == 0 && !w->kino.grounded, "disabled lift freezes and detaches");

        fresh(*w);
        lift(*w);
        auto current = saveLevel(w->level);
        check(current.starts_with("ECHO_LEVEL_11") && saveLevel(loadLevel(current)) == current,
              "v11 route definition round trip");
        auto idx = current.find("PLATFORM_FEATURES");
        auto bad =
            current.substr(0, idx) + "PLATFORM_FEATURES 2 1\n1 5 64 400 192 16 100 0.25 2.5 1 0 0 1\n";
        bool failed = false;
        try {
            loadLevel(bad);
        } catch (...) {
            failed = true;
        }
        check(failed, "zero route rejected");
        auto legacy = current.substr(0, idx);
        legacy.replace(0, 13, "ECHO_LEVEL_10");
        legacy += "PLATFORM_FEATURES 2 1\n1 4 64 400 192 16 980 0.35 2.5 1\n";
        auto migrated = std::make_unique<LevelState>(loadLevel(legacy));
        check(migrated->features[0].kind == PlatformKind::Falling && migrated->features[0].stage == 0,
              "v10 existing features migrate");
        auto invalid = w->level.features[0];
        invalid.travel = {9000, 0};
        failed = false;
        try {
            validateFeature(invalid);
        } catch (...) {
            failed = true;
        }
        check(failed, "out-of-range route rejected");

        fresh(*w);
        auto& tiny = lift(*w, {0, -1});
        tiny.speed = 4000;
        tiny.delay = 0;
        w->kino.grounded = false;
        w->kino.standingPlatformId = -1;
        tick(*w, 120);
        check(tiny.progress >= 0 && tiny.progress <= 1 && w->kino.hp > 0,
              "one-pixel maximum speed route has bounded traversal");
        // Prove restored traversal produces the same future: route return, dwell and rider.
        fresh(*w);
        lift(*w, {80, -192});
        tick(*w, 43);
        auto saved = std::make_unique<WorldState>(*w);
        tick(*w, 123);
        auto expected = std::make_unique<WorldState>(*w);
        *w = *saved;
        tick(*w, 123);
        check(std::memcmp(w.get(), expected.get(), sizeof(WorldState)) == 0,
              "exact snapshot continuation for nontrivial route phase");

        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w);
                lift(*w, {64, -192});
                w->jump.gravity = 980;
                w->rewind.record_hz = hz;
                Simulation sim(384ull * 1024 * 1024);
                sim.reset(*w);
                measuring = true;
                allocations = 0;
                for (int i = 0; i < 360; ++i)
                    sim.tick({});
                measuring = false;
                check(allocations == 0, "lift runtime and full snapshots allocate no C++ heap");
                Input in;
                in.fast = fast;
                in.trace = !fast;
                for (int i = 0; i < 370 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(in);
                check(sim.world().clock < .001 && sim.history().size() == 1 &&
                          sim.world().level.features[0].progress == 0 &&
                          sim.world().level.features[0].direction == 1,
                      "full six-second oldest T/R restores route and truncates");
                sim.tick({});
                check(sim.world().level.features[0].progress > 0 && (fast || sim.world().kino.grounded),
                      "continue after oldest route rewind");
            }
        for (bool enabled : {false, true}) {
            fresh(*w);
            lift(*w);
            w->rewind.enabled = enabled;
            w->deathTransition.kind = TransitionKind::None;
            w->jump.gravity = 980;
            ceiling.bounds = {0, 336, 600, 16};
            w->level.colliders.push(ceiling);
            Simulation crushed;
            crushed.reset(*w);
            crushed.tick({});
            check(crushed.world().state == TimeState::Hit && crushed.world().gameplayClockPaused,
                  "crush freezes explicitly with or without history");
            Input escape;
            escape.trace = enabled;
            escape.restart = !enabled;
            crushed.tick(escape);
            check(crushed.world().kino.hp > 0, "enabled crush can T restore; disabled crush restarts");
        }

        fresh(*w);
        lift(*w);
        w->rewind.enabled = false;
        Simulation noHistory;
        noHistory.reset(*w);
        noHistory.tick({});
        check(noHistory.history().capacity() == 0 && noHistory.world().level.features[0].progress > 0,
              "lifts work with no history allocation");

        if (argc > 1) {
            Resources resources;
            resources.root = std::filesystem::path(argv[1]) / "lift-lua";
            std::filesystem::create_directories(resources.root / "scripts");
            writeTextAtomically(resources.root / "project.echo", "scripts=scripts/test.lua\n");
            writeTextAtomically(
                resources.root / "scripts/test.lua",
                "engine.configure_platform(1,{speed=0,travel_x=80,travel_y=-128,repeat_route=false})\n"
                "local p=engine.get_platform(1)\n"
                "engine.state[1]=p.travel_y\n");
            fresh(*w);
            lift(*w);
            tick(*w, 30);
            auto x = w->level.features[0].bounds.x, y = w->level.features[0].bounds.y;
            ScriptRuntime script;
            script.load(resources, *w);
            check(w->script.persistent[0] == -128 && w->level.features[0].speed == 0 &&
                      !w->level.features[0].repeat && w->level.features[0].bounds.x == x &&
                      w->level.features[0].bounds.y == y,
                  "Lua edits preserve current position");
            tick(*w, 2);
            check(w->level.features[0].bounds.x == x && w->level.features[0].bounds.y == y,
                  "paused modified route does not jump");
        }
        std::cout << "PASS lift routes / relative crossing / carry-crush / migration / Lua / oldest T-R "
                     "/ zero heap\n"
                  << "WorldState bytes " << sizeof(WorldState) << '\n';
        return 0;
    } catch (const std::exception& e) {
        measuring = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
