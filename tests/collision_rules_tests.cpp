#include "echo/collision.hpp"
#include "echo/simulation.hpp"
#include "echo/snapshot_diff.hpp"
#include "echo/script.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
using namespace echo;
static bool track;
static int allocations;
void* operator new(std::size_t n) {
    if (track)
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
static void check(bool b, const char* why) {
    if (!b)
        throw std::runtime_error(why);
}
static void fresh(WorldState& w) {
    w = makeWorld();
    w.level.tiles.fill(Tile::Empty);
    w.level.colliders.clear();
    w.level.platforms.clear();
    w.level.events.clear();
    w.level.dialogues.clear();
    w.level.portals.clear();
    w.aster.enabled = false;
    w.jump.gravity = 0;
    w.kino.x = 100;
    w.kino.y = 100;
    w.kino.vy = 0;
    w.kino.invulnTimer = 0;
}
static EventMap sensor(WorldState& w, EventTrigger mode) {
    EventMap m;
    EventDefinition d;
    d.id = 1;
    EventPage p;
    p.trigger = mode;
    EventCommand c;
    c.op = EventOp::AddVariable;
    c.a = 0;
    c.value = 1;
    p.commands.push_back(c);
    d.pages.push_back(p);
    m.events.push_back(d);
    EventPlacement e;
    e.region = {100, 100, 32, 48};
    w.level.events.push(e);
    return m;
}
int main(int argc, char** argv) {
    try {
        auto w = std::make_unique<WorldState>(), saved = std::make_unique<WorldState>();
        fresh(*w);
        check(collisionAllowed(*w, CollisionClass::Player, CollisionClass::Projectile),
              "defaults preserve collisions");
        setCollisionPair(w->collisions, CollisionClass::Player, CollisionClass::Projectile, false);
        check(!collisionAllowed(*w, CollisionClass::Projectile, CollisionClass::Player),
              "symmetric pair filter");
        for (int a = 0; a < 8; ++a)
            for (int b = 0; b < 8; ++b) {
                auto rules = w->collisions;
                setCollisionPair(w->collisions, CollisionClass(a), CollisionClass(b), false);
                check(!collisionAllowed(*w, CollisionClass(a), CollisionClass(b)) &&
                          !collisionAllowed(*w, CollisionClass(b), CollisionClass(a)),
                      "all category bits filter symmetrically");
                check(readCollisionRules(writeCollisionRules(w->collisions)).masks ==
                          w->collisions.masks,
                      "every pair serializes");
                w->collisions = rules;
            }
        auto text = writeCollisionRules(w->collisions);
        check(readCollisionRules(text).masks == w->collisions.masks, "matrix round trip");
        for (auto invalid :
             {"2 255 255 255 255 255 255 255 255", "1 255 255 255 255 255 255 255 256",
              "1 255 0 255 255 255 255 255 255", "1 255 255 255 255 255 255 255 255 extra"}) {
            bool rejected = false;
            try {
                readCollisionRules(invalid);
            } catch (...) {
                rejected = true;
            }
            check(rejected, "invalid matrices rejected");
        }
        // Pixel snapping never enters this thin-wall sweep.
        fresh(*w);
        StaticCollider wall;
        wall.bounds = {160, 0, 1, 400};
        w->level.colliders.push(wall);
        w->jump.move_speed = 12000;
        w->jump.ground_acceleration = 1e9f;
        w->jump.air_acceleration = 1e9f;
        stepPhysics(*w, {.move = 1}, fixedDt);
        check(w->kino.x <= 132.001f, "player CCD stops at one-unit wall");
        setCollisionPair(w->collisions, CollisionClass::Player, CollisionClass::Terrain, false);
        w->kino.x = 100;
        stepPhysics(*w, {.move = 1}, fixedDt);
        check(w->kino.x > 160, "terrain mask gates swept response");
        fresh(*w);
        w->level.colliders.push(wall);
        bool blocked = false;
        auto moved = moveEventBody(*w, 2, {10, 300, 20, 20}, {300, 80}, blocked);
        check(blocked && std::abs(moved.x - 130) < .01f && std::abs(moved.y - 80) < .01f,
              "event CCD stops thin wall and slides");
        setCollisionPair(w->collisions, CollisionClass::Trigger, CollisionClass::Terrain, false);
        blocked = false;
        moved = moveEventBody(*w, 2, {10, 300, 20, 20}, {300, 0}, blocked);
        check(!blocked && moved.x == 300, "event terrain pair disabled");
        fresh(*w);
        Bullet b;
        b.x = 114;
        b.y = 132;
        b.vx = 0;
        b.vy = 0;
        b.hitR = 3;
        b.damage = 1;
        b.expireAge = 10;
        b.visualR = 3;
        spawnBullet(*w, b);
        SimulationStats stats;
        setCollisionPair(w->collisions, CollisionClass::Player, CollisionClass::Projectile, false);
        stepBullets(*w, fixedDt, {100, 100}, stats, nullptr);
        check(w->kino.hp == 1, "projectile damage mask");
        setCollisionPair(w->collisions, CollisionClass::Player, CollisionClass::Projectile, true);
        stepBullets(*w, fixedDt, {100, 100}, stats, nullptr);
        check(w->kino.hp == 0, "projectile damage enabled");
        // Entry, leave and contact state are independent of the interpreter finishing.
        fresh(*w);
        auto m = sensor(*w, EventTrigger::Enter);
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1 && !w->level.events[0].solid, "pure trigger enters once");
        *saved = *w;
        for (int i = 0; i < 10; ++i)
            stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1, "entry does not repeat inside");
        restore(*w, *saved);
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1, "restore contact does not duplicate entry");
        w->kino.x = 400;
        stepEvents(*w, m, {}, false, fixedDt);
        w->kino.x = 100;
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 2, "reentry grants once");
        fresh(*w);
        m = sensor(*w, EventTrigger::Leave);
        stepEvents(*w, m, {}, false, fixedDt);
        w->kino.x = 400;
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1, "leave once");
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1, "outside no repeat");
        fresh(*w);
        m = sensor(*w, EventTrigger::Contact);
        for (int i = 0; i < 10; ++i)
            stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 10, "continuous contact restarts only completed chain");
        for (auto mode : {EventTrigger::Enter, EventTrigger::Leave}) {
            fresh(*w);
            m = sensor(*w, mode);
            w->kino.x = 0;
            stepEvents(*w, m, {}, false, fixedDt);
            w->kino.x = 400;
            stepEvents(*w, m, {}, false, fixedDt);
            check(w->events.variables[0] == 1, "fast pass through trigger preserves crossing");
        }
        fresh(*w);
        m = sensor(*w, EventTrigger::Enter);
        w->kino.x = 0;
        w->level.events[0].region.x = 400;
        stepEvents(*w, m, {}, false, fixedDt);
        w->level.events[0].region.x = -200;
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1, "moving trigger relative sweep");
        fresh(*w);
        m = sensor(*w, EventTrigger::Enter);
        w->events.foreground = 31;
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.local[0].triggerPending && w->events.variables[0] == 0,
              "busy foreground latches entry");
        *saved = *w;
        w->events.foreground = -1;
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1, "queued entry resumes");
        restore(*w, *saved);
        w->events.foreground = -1;
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1, "queued edge restores");
        fresh(*w);
        m = sensor(*w, EventTrigger::Enter);
        stepEvents(*w, m, {}, false, fixedDt);
        auto second = m.events[0].pages[0];
        second.switchId = 0;
        m.events[0].pages.push_back(second);
        w->events.switches[0] = 1;
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 1, "page change inside does not invent reentry");
        w->events.foreground = 0;
        w->dialogue.active = 8;
        w->events.dialogueOwner = 0;
        w->events.local[0].running = true;
        w->level.events[0].erased = true;
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.foreground == -1 && w->dialogue.active == -1 && !w->events.local[0].running,
              "erased event releases owned state");
        fresh(*w);
        m = sensor(*w, EventTrigger::Enter);
        setCollisionPair(w->collisions, CollisionClass::Player, CollisionClass::Trigger, false);
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 0, "trigger filter suppresses activation");
        fresh(*w);
        m = sensor(*w, EventTrigger::Leave);
        stepEvents(*w, m, {}, false, fixedDt);
        setCollisionPair(w->collisions, CollisionClass::Player, CollisionClass::Trigger, false);
        stepEvents(*w, m, {}, false, fixedDt);
        check(w->events.variables[0] == 0 && !w->events.local[0].triggerPending,
              "disabling sensor does not fabricate leave");
        for (int preset = 4; preset <= 6; ++preset) {
            auto e = eventPreset(1, preset);
            check(int(e.pages[0].trigger) == preset, "region presets");
        }
        // New format is explicit; previous Move ignored b and migrates as through mode.
        auto serialized = saveEvents(m);
        check(serialized.starts_with("ECHO_EVENTS_10") &&
                  saveEvents(loadEvents(serialized)) == serialized,
              "new trigger format round trip");
        check(loadEvents("ECHO_EVENTS_2\n1\n1 \"old\" 1\n3 -1 1 -1 0 -1 1 0 0 1\n14 0 1 0 1 200 0 \"\" "
                         "\"\" \"\" 0\n")
                      .events[0]
                      .pages[0]
                      .commands[0]
                      .b == 0,
              "legacy move remains through");
        auto common = m;
        bool rejected = false;
        try {
            validateEvents(common, true);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "region triggers forbidden in common events");
        // Execute authored collision route and restore in its middle.
        fresh(*w);
        m = sensor(*w, EventTrigger::Parallel);
        w->level.events[0].region = {10, 300, 20, 20};
        w->level.colliders.push(wall);
        auto& c = m.events[0].pages[0].commands[0];
        c.op = EventOp::Move;
        c.x = 600;
        c.y = 0;
        c.value = .1f;
        c.b = 1;
        stepEvents(*w, m, {}, false, fixedDt);
        stepEvents(*w, m, {}, false, fixedDt);
        *saved = *w;
        track = true;
        allocations = 0;
        for (int i = 0; i < 8; ++i)
            stepEvents(*w, m, {}, false, fixedDt);
        track = false;
        check(!allocations && w->level.events[0].region.x <= 140.01f && w->events.local[0].moveBlocked,
              "blocked route is bounded/no heap");
        auto result = std::make_unique<WorldState>(*w);
        restore(*w, *saved);
        for (int i = 0; i < 8; ++i)
            stepEvents(*w, m, {}, false, fixedDt);
        auto report = std::make_unique<SnapshotDiff>();
        compareSnapshots(*w, *result, *report);
        check(report->equal(), "route restore replays entire state");
        for (int hz : {30, 60, 120}) {
            fresh(*w);
            w->rewind.record_hz = hz;
            w->kino.invulnTimer = 1000;
            m = sensor(*w, EventTrigger::Enter);
            Simulation sim(384ull * 1024 * 1024);
            sim.setEvents({m}, {});
            sim.reset(*w);
            for (int i = 0; i < 420; ++i)
                sim.tick({});
            check(sim.history().seconds() > 5.9, "full six second trigger history");
            sim.seekHistory(0);
            int reward = sim.world().events.variables[0];
            sim.tick({});
            sim.tick({});
            check(sim.world().events.variables[0] == reward, "oldest restore no extra entry");
            for (bool fast : {false, true}) {
                for (int i = 0; i < 420; ++i)
                    sim.tick({});
                Input input;
                input.fast = fast;
                input.trace = !fast;
                int guard = 0;
                do {
                    sim.tick(input);
                    ++guard;
                } while (
                    (sim.world().state == TimeState::Fast || sim.world().state == TimeState::Trace) &&
                    guard < 1000);
                check(guard < 1000, "farthest T/R ends");
                sim.tick({});
                sim.tick({});
                check(sim.world().kino.hp == 1, "continue after T/R trigger rewind");
            }
        }
        if (argc > 1) {
            auto root = std::filesystem::path(argv[1]) / "collision-rules-project";
            std::filesystem::create_directories(root);
            writeTextAtomically(root / "project.echo",
                                "scripts=pair.lua\ncollision_rules=" + text + "\n");
            writeTextAtomically(
                root / "pair.lua",
                "assert(not engine.collision_pair('player','projectile'))\nfunction on_key_press(k) "
                "engine.set_collision_pair('player','projectile',true) end\n");
            Resources resources;
            resources.root = root;
            fresh(*w);
            ScriptRuntime lua;
            lua.load(resources, *w);
            check(!collisionAllowed(*w, CollisionClass::Player, CollisionClass::Projectile),
                  "loaded project pair");
            *saved = *w;
            lua.key(*w, "Space");
            check(collisionAllowed(*w, CollisionClass::Player, CollisionClass::Projectile),
                  "Lua edits current snapshot rules");
            restore(*w, *saved);
            lua.afterRestore(*w);
            check(!collisionAllowed(*w, CollisionClass::Player, CollisionClass::Projectile),
                  "Lua future config restored");
        }
        std::cout << "PASS pair rules / thin wall CCD / trigger edges and pending state / route "
                     "response / legacy migration / all Hz farthest T/R / Lua; WorldState bytes "
                  << sizeof(WorldState) << '\n';
    } catch (const std::exception& e) {
        track = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
