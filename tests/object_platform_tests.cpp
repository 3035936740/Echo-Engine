#include "echo/objects.hpp"
#include "echo/platform_features.hpp"
#include "echo/script.hpp"
#include "echo/collision.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
using namespace echo;
static bool measuring{};
static std::size_t allocations{};
void* operator new(std::size_t n) {
    if (measuring)
        ++allocations;
    if (auto* p = std::malloc(n ? n : 1))
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
    w.objects = {};
    w.level.tiles.fill(Tile::Empty);
    w.level.features.clear();
    w.level.platforms.clear();
    w.level.colliders.clear();
    w.level.events.clear();
    w.level.dialogues.clear();
    w.level.portals.clear();
    w.level.objects.clear();
    w.level.nextFeatureId = 1;
    w.kino.x = 2000;
    w.kino.y = 592;
    w.kino.invulnTimer = 10000;
    for (int x = 0; x < mapWidth; ++x)
        w.level.tiles[20 * mapWidth + x] = Tile::Solid;
}
PlatformFeature& feature(WorldState& w, PlatformKind kind = PlatformKind::Lift,
                         Vec2 travel = {0, -192}) {
    auto f = platformPreset(kind, {64, 400, 256, 16}, w.level.nextFeatureId++);
    f.travel = travel;
    f.speed = 192;
    f.delay = .25f;
    w.level.features.push(f);
    return w.level.features[w.level.features.count - 1];
}
ObjectTemplate rider() {
    auto t = objectPreset(4, 3);
    t.width = 28;
    t.height = 32;
    t.speed = 0;
    t.mask = 1;
    t.ridesPlatforms = true;
    t.damage = 0;
    return t;
}
ObjectHandle mount(WorldState& w, const ObjectTemplate& t, int support = -100001, float x = 100,
                   std::uint64_t placement = 0) {
    auto h = createObject(w, t, {x, 368}, w.activeMap, placement);
    auto* o = resolveObject(w, h);
    check(o, "create rider");
    o->grounded = true;
    o->standingPlatformId = support;
    return h;
}
void tick(WorldState& w, int count = 1) {
    for (int i = 0; i < count; ++i) {
        stepPhysics(w, {}, fixedDt);
        if (w.state != TimeState::Hit)
            stepObjects(w, {}, fixedDt, true);
    }
}
EventMap luaMap(const std::string& source, bool condition = false) {
    EventCommand c;
    c.op = condition ? EventOp::IfScript : EventOp::Script;
    c.text.set(source);
    EventPage page;
    page.commands.push_back(c);
    if (condition) {
        EventCommand end;
        end.op = EventOp::EndIf;
        page.commands.push_back(end);
    }
    EventDefinition d;
    d.id = 1;
    d.name.set("object movement");
    d.pages.push_back(page);
    return EventMap{{d}};
}
int main(int argc, char** argv) {
    try {
        auto w = std::make_unique<WorldState>();
        auto t = rider();
        fresh(*w);
        feature(*w);
        auto h = mount(*w, t);
        tick(*w, 30);
        auto* o = resolveObject(*w, h);
        check(o && near(o->y, 272) && near(o->py, 275.2f) && near(o->homeY, 272) && o->grounded &&
                  o->standingPlatformId == -100001,
              "vertical ride preserves previous coordinate and home");
        tick(*w, 60);
        o = resolveObject(*w, h);
        check(o && o->grounded && near(o->y, 224), "rider stays through endpoint dwell and return");

        fresh(*w);
        feature(*w);
        h = mount(*w, t);
        o = resolveObject(*w, h);
        o->animation.collision = {4, 8, 18, 20, true};
        o->facing = -1;
        o->y = o->py = o->homeY = 372;
        tick(*w, 30);
        o = resolveObject(*w, h);
        check(o && near(o->y, 276) && near(objectBody(*w, *o).y + objectBody(*w, *o).h, 304) &&
                  o->grounded,
              "animated offset and mirrored collision body remains attached by its feet");
        fresh(*w);
        feature(*w, PlatformKind::Lift, {96, -128});
        h = mount(*w, t);
        w->level.features[0].speed = 160;
        w->level.features[0].repeat = false;
        tick(*w, 100);
        o = resolveObject(*w, h);
        check(o && near(o->x, 196) && near(o->y, 240) && o->grounded && w->level.features[0].stage == 2,
              "diagonal once route remains supporting object");
        fresh(*w);
        feature(*w, PlatformKind::Lift, {192, 0});
        t.solid = true;
        t.mask = 65535;
        h = mount(*w, t, -100001, 100);
        auto second = mount(*w, t, -100001, 128);
        tick(*w, 3);
        check(resolveObject(*w, h) && resolveObject(*w, second) &&
                  near(resolveObject(*w, second)->x - resolveObject(*w, h)->x, 28),
              "co-moving solid riders do not crush one another");
        t = rider();

        fresh(*w);
        MovingPlatform p;
        p.id = 0;
        p.base = {64, 400, 256, 16};
        p.pos = p.previous = {64, 400};
        p.speed = 1;
        p.amplitude = 96;
        w->level.platforms.push(p);
        h = mount(*w, t, 0);
        tick(*w, 30);
        o = resolveObject(*w, h);
        check(o && near(o->x, 100 + std::sin(.5f) * 96) && o->grounded,
              "legacy mover carries generic rider");
        fresh(*w);
        auto& belt = feature(*w, PlatformKind::Conveyor);
        belt.speed = -100;
        h = mount(*w, t);
        tick(*w, 12);
        o = resolveObject(*w, h);
        check(o && near(o->x, 80) && o->grounded, "negative conveyor moves object");
        fresh(*w);
        auto& falling = feature(*w, PlatformKind::Falling);
        falling.speed = 980;
        falling.delay = .05f;
        h = mount(*w, t);
        tick(*w, 10);
        o = resolveObject(*w, h);
        check(o && falling.stage == 1 && falling.bounds.y > 400 && o->y > 368 && o->grounded,
              "object alone triggers and rides falling platform");
        fresh(*w);
        auto& spring = feature(*w, PlatformKind::Spring);
        spring.speed = 560;
        h = mount(*w, t);
        second = mount(*w, t, -100001, 160);
        tick(*w);
        check(resolveObject(*w, h)->vy == -560 && resolveObject(*w, second)->vy == -560 &&
                  resolveObject(*w, h)->springCooldown > 0 && !resolveObject(*w, h)->grounded,
              "per-object spring timers allow simultaneous launch");

        fresh(*w);
        auto& catching = feature(*w);
        catching.speed = 2400;
        h = mount(*w, t);
        o = resolveObject(*w, h);
        o->grounded = false;
        o->standingPlatformId = -1;
        o->y = 348;
        tick(*w);
        o = resolveObject(*w, h);
        check(o && o->grounded && near(o->y, 328), "ascending platform sweeps unattached object");
        fresh(*w);
        feature(*w);
        h = mount(*w, t);
        o = resolveObject(*w, h);
        o->ridesPlatforms = false;
        tick(*w, 3);
        o = resolveObject(*w, h);
        check(o && o->standingPlatformId == -1 && o->y > 368, "riding can be disabled per object");
        fresh(*w);
        feature(*w);
        h = mount(*w, t);
        setCollisionPair(w->collisions, CollisionClass::NPC, CollisionClass::Terrain, false);
        tick(*w, 3);
        o = resolveObject(*w, h);
        check(o && o->standingPlatformId == -1 && o->y > 368, "matrix disables carry");
        fresh(*w);
        feature(*w);
        h = mount(*w, t);
        w->level.features.clear();
        tick(*w);
        check(resolveObject(*w, h)->standingPlatformId == -1, "deleted support detaches safely");

        StaticCollider ceiling;
        ceiling.bounds = {0, 352, 600, 16};
        fresh(*w);
        feature(*w);
        h = mount(*w, t, -100001, 100, 9);
        w->level.colliders.push(ceiling);
        auto before = std::make_unique<WorldState>(*w);
        tick(*w);
        check(!resolveObject(*w, h) && w->objects.slots[h.slot].crushed &&
                  w->objects.slots[h.slot].hp == 0 && w->objects.slots[h.slot].active &&
                  w->state == TimeState::Free,
              "crush death invalidates handle and retains authored tombstone without killing player");
        restore(*w, *before);
        check(resolveObject(*w, h) && !resolveObject(*w, h)->crushed,
              "restore resurrects original rider generation");
        fresh(*w);
        feature(*w);
        t.crushKills = false;
        h = mount(*w, t);
        w->level.colliders.push(ceiling);
        tick(*w);
        check(resolveObject(*w, h) && resolveObject(*w, h)->crushed &&
                  resolveObject(*w, h)->standingPlatformId == -1,
              "nonlethal crush detaches");
        t = rider();
        fresh(*w);
        auto& reverser = feature(*w, PlatformKind::Lift, {0, -2});
        reverser.speed = 240;
        reverser.delay = 0;
        h = mount(*w, t);
        ceiling.bounds = {0, 350, 600, 17};
        w->level.colliders.push(ceiling);
        tick(*w);
        check(!resolveObject(*w, h), "same-substep reversal checks intermediate object crush");

        StaticCollider floor;
        floor.bounds = {0, 400, 600, 32};
        fresh(*w);
        feature(*w, PlatformKind::Lift, {96, 192});
        h = mount(*w, t);
        w->level.colliders.push(floor);
        tick(*w, 6);
        o = resolveObject(*w, h);
        check(o && o->grounded && !o->crushed && o->standingPlatformId == -1 && near(o->y, 368) &&
                  o->x > 100,
              "diagonal descent transfers object to floor safely");

        ObjectLibrary lib;
        lib.templates.push_back(t);
        auto text = saveObjects(lib);
        check(text.starts_with("ECHO_OBJECTS_3") && saveObjects(loadObjects(text)) == text,
              "v3 rider flags roundtrip");
        auto line = text.substr(text.find('\n') + 1);
        line = line.substr(0, line.rfind(' ', line.size() - 2));
        line = line.substr(0, line.rfind(' ')); // remove two new boolean columns for a v2 fixture
        auto v2 = std::string("ECHO_OBJECTS_2 1 5\n") + line + "\n";
        check(loadObjects(v2).templates[0].ridesPlatforms, "v2 gravity object migrates to rider");
        auto anchored = objectPreset(7, 4);
        lib.templates = {anchored};
        text = saveObjects(lib);
        line = text.substr(text.find('\n') + 1);
        line = line.substr(0, line.rfind(' ', line.size() - 2));
        line = line.substr(0, line.rfind(' '));
        check(!loadObjects("ECHO_OBJECTS_2 1 8\n" + line + "\n").templates[0].ridesPlatforms,
              "v2 no-gravity anchor does not become rider");
        bool rejected = false;
        auto malformed = saveObjects(lib);
        malformed.replace(malformed.size() - 4, 1, "2");
        try {
            loadObjects(malformed);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "invalid rider flag rejected");

        fresh(*w);
        feature(*w);
        h = mount(*w, t);
        tick(*w, 41);
        auto saved = std::make_unique<WorldState>(*w);
        tick(*w, 123);
        auto expected = std::make_unique<WorldState>(*w);
        restore(*w, *saved);
        tick(*w, 123);
        check(std::memcmp(w.get(), expected.get(), sizeof(WorldState)) == 0,
              "exact object rider snapshot continuation");
        auto inactive = *resolveObject(*w, h);
        w->activeMap = 1;
        tick(*w, 10);
        check(w->objects.slots[h.slot] == inactive, "inactive map rider remains frozen");
        w->activeMap = 0;
        w->level.features[0].bounds = w->level.features[0].base;
        prepareObjectPlatforms(*w);
        check(resolveObject(*w, h)->standingPlatformId == -1,
              "reinitialized map support cannot keep stale attachment");

        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w);
                feature(*w, PlatformKind::Lift, {64, -192});
                h = mount(*w, t);
                w->rewind.record_hz = hz;
                Simulation sim(384ull * 1024 * 1024);
                sim.reset(*w);
                allocations = 0;
                measuring = true;
                for (int i = 0; i < 360; ++i)
                    sim.tick({});
                measuring = false;
                check(allocations == 0, "riders allocate no C++ heap in forward steps and recording");
                Input rewind;
                rewind.fast = fast;
                rewind.trace = !fast;
                for (int i = 0; i < 370 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(rewind);
                check(sim.history().size() == 1 && sim.world().clock < .001 &&
                          resolveObject(sim.world(), h) && near(resolveObject(sim.world(), h)->y, 368) &&
                          resolveObject(sim.world(), h)->standingPlatformId == -100001,
                      "full six-second oldest T/R restores generic support, position and generation");
                sim.tick({});
                check(resolveObject(sim.world(), h)->grounded && resolveObject(sim.world(), h)->y < 368,
                      "continue rider after oldest truncation");
            }
        fresh(*w);
        feature(*w);
        h = mount(*w, t);
        w->rewind.enabled = false;
        Simulation off;
        off.reset(*w);
        off.tick({});
        check(off.history().capacity() == 0 && resolveObject(off.world(), h)->y < 368,
              "object ride works when history disabled");

        if (argc > 1) {
            Resources resources;
            resources.root = std::filesystem::path(argv[1]) / "object-rider-lua";
            std::filesystem::create_directories(resources.root);
            writeTextAtomically(resources.root / "project.echo", "scripts=\n");
            fresh(*w);
            feature(*w);
            h = mount(*w, t);
            ScriptRuntime lua;
            lua.load(resources, *w);
            auto handle = std::to_string(h.slot) + "," + std::to_string(h.generation);
            auto map = luaMap("engine.configure_object_movement(" + handle +
                              ",{rides_platforms=false,crush_kills=false})");
            lua.prepareEventScripts({map}, {});
            lua.beginEventStep(*w);
            check(lua.eventScript(*w, {0, 1, 0, 0, false}) && !resolveObject(*w, h)->ridesPlatforms &&
                      !resolveObject(*w, h)->crushKills,
                  "Lua rider policy accepts generational handle");
            map = luaMap("local p=engine.get_object(" + handle +
                             ");p.rides_platforms=true;"
                             "return not engine.get_object(" +
                             handle + ").rides_platforms",
                         true);
            lua.prepareEventScripts({map}, {});
            lua.beginEventStep(*w);
            saved = std::make_unique<WorldState>(*w);
            bool result = false;
            check(lua.eventCondition(*w, {0, 1, 0, 0, false}, result) && result &&
                      std::memcmp(saved.get(), w.get(), sizeof(WorldState)) == 0,
                  "read-only object query returns a copy");
            map = luaMap("engine.configure_object_movement(" + handle +
                         ",{rides_platforms=true,crush_kills=123})");
            lua.prepareEventScripts({map}, {});
            lua.beginEventStep(*w);
            check(!lua.eventScript(*w, {0, 1, 0, 0, false}) && !resolveObject(*w, h)->ridesPlatforms,
                  "strict boolean rejects partial native mutation");
            auto oldHandle = h;
            destroyObject(*w, h);
            h = createObject(*w, t, {100, 368}, 0);
            map = luaMap("return engine.get_object(" + std::to_string(oldHandle.slot) + "," +
                             std::to_string(oldHandle.generation) + ")==nil",
                         true);
            lua.prepareEventScripts({map}, {});
            lua.beginEventStep(*w);
            result = false;
            check(lua.eventCondition(*w, {0, 1, 0, 0, false}, result) && result,
                  "stale Lua query cannot alias reused slot");

            fresh(*w);
            feature(*w, PlatformKind::Lift, {32, -192});
            for (int i = 0; i < maxObjects; ++i)
                mount(*w, t, -100001, 100 + (i % 4) * 32);
            Simulation stress(192ull * 1024 * 1024);
            stress.reset(*w);
            std::array<double, 600> costs{};
            allocations = 0;
            measuring = true;
            for (auto& cost : costs) {
                auto a = std::chrono::steady_clock::now();
                stress.tick({});
                cost = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a)
                           .count();
            }
            measuring = false;
            check(allocations == 0, "256 riders and complete history perform no tick allocation");
            for (const auto& object : stress.world().objects.slots)
                check(object.alive && object.grounded, "full object pool remains riding");
            std::sort(costs.begin(), costs.end());
            std::ofstream report(std::filesystem::path(argv[1]) / "object-rider-performance.txt");
            report << "riders=256\nframes=600\nworld_bytes=" << sizeof(WorldState)
                   << "\nheap_allocations=" << allocations << "\np95_ms=" << costs[570]
                   << "\np99_ms=" << costs[594] << "\nworst_ms=" << costs.back() << '\n';
        }
        std::cout << "PASS object platform ride / generic policy / carry-crush / v1-v3 / Lua handles / "
                     "six-second all-Hz T-R / zero heap\nWorldState bytes "
                  << sizeof(WorldState) << '\n';
        return 0;
    } catch (const std::exception& e) {
        measuring = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
