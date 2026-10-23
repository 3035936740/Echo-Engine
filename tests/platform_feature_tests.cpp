#include "echo/platform_features.hpp"
#include "echo/editor.hpp"
#include "echo/script.hpp"
#include "echo/input_bindings.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
using namespace echo;
static bool measure{};
static std::size_t allocations{};
void* operator new(std::size_t n) {
    if (measure)
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
void check(bool v, const char* message) {
    if (!v)
        throw std::runtime_error(message);
}
void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.script = {};
    w.kino.invulnTimer = 1000;
    w.level.tiles.fill(Tile::Empty);
    for (auto& l : w.level.layers)
        l.cells.fill(0);
    w.level.platforms.clear();
    w.level.colliders.clear();
    w.level.dialogues.clear();
    w.level.events.clear();
    w.level.portals.clear();
    w.objects = {};
    w.kino.x = 100;
    w.kino.y = 100;
    w.kino.grounded = false;
    w.kino.coyoteTimer = 0;
}
void add(WorldState& w, PlatformKind kind, Rect r) {
    w.level.features.push(platformPreset(kind, r, w.level.nextFeatureId++));
}
void runPhysics(WorldState& w, Input input = {}, int frames = 1) {
    for (int i = 0; i < frames; ++i)
        stepPhysics(w, input, fixedDt);
}
int main(int argc, char** argv) {
    try {
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        check(writeMobility(readMobility(writeMobility(w->mobility))) == writeMobility(w->mobility),
              "mobility settings round trip");
        for (int kind = 0; kind < 6; ++kind)
            add(*w, PlatformKind(kind), {float(kind * 160), 400, 96, 32});
        auto levelText = saveLevel(w->level);
        check(levelText.starts_with("ECHO_LEVEL_11") && saveLevel(loadLevel(levelText)) == levelText,
              "all feature definitions and IDs serialize");
        auto old = levelText.substr(0, levelText.find("PLATFORM_FEATURES"));
        old.replace(0, 13, "ECHO_LEVEL_9");
        check(loadLevel(old).features.count == 0, "v9 default migration");
        auto malformed = levelText;
        auto marker = malformed.find("PLATFORM_FEATURES");
        malformed.replace(marker, 17, "BROKEN_FEATURES");
        bool rejected = false;
        try {
            loadLevel(malformed);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "malformed feature section rejected");
        auto editor = std::make_unique<LevelEditor>();
        editor->featureKind = PlatformKind::Ladder;
        editor->tool = 24;
        editor->featurePlacementArmed = true;
        editor->paint(100, 200);
        editor->paint(140, 300);
        editor->end();
        check(editor->level.features.count == 1 && editor->level.features[0].base.h == 128,
              "rectangle feature authoring");
        auto id = editor->level.features[0].id;
        editor->undo();
        editor->redo();
        check(editor->level.features[0].id == id, "undo/redo preserves identity");
        auto undo = editor->undoCount();
        editor->paint(105, 210);
        editor->end();
        check(editor->undoCount() == undo, "selection alone does not create edit history");
        editor->paint(105, 210);
        editor->paint(170, 210);
        editor->end();
        check(editor->level.features[0].base.x > 96, "drag feature with retained offset");

        fresh(*w);
        w->kino.y = 300;
        add(*w, PlatformKind::Ladder, {96, 0, 40, 600});
        Input climb;
        climb.up = true;
        runPhysics(*w, climb, 60);
        check(w->kino.climbing && std::abs(w->kino.y - 140) < .1f && w->kino.vy < 0,
              "ladder climbs at configured speed");
        auto heldY = w->kino.y;
        runPhysics(*w, {}, 10);
        check(std::abs(w->kino.y - heldY) < .01f, "ladder holds when released");
        Input jump;
        jump.jump = true;
        runPhysics(*w, jump);
        check(!w->kino.climbing && w->kino.vy < -300, "jump releases ladder");

        fresh(*w);
        add(*w, PlatformKind::Water, {0, 0, 600, 600});
        w->kino.vy = 200;
        runPhysics(*w);
        check(w->kino.inWater && w->kino.vy <= 150, "water reduces gravity and terminal velocity");
        runPhysics(*w, jump);
        check(w->kino.vy < -200, "swim jump without floor support");
        w->collisions.masks[int(CollisionClass::Player)] &= ~(1u << int(CollisionClass::Trigger));
        w->collisions.masks[int(CollisionClass::Trigger)] &= ~(1u << int(CollisionClass::Player));
        runPhysics(*w);
        check(!w->kino.inWater, "trigger matrix controls environment regions");

        fresh(*w);
        add(*w, PlatformKind::Conveyor, {64, 400, 320, 16});
        w->kino.y = 352;
        w->kino.grounded = true;
        w->kino.standingPlatformId = featureSupportId(w->level.features[0]);
        runPhysics(*w, {}, 30);
        check(w->kino.x > 145 && w->kino.grounded, "conveyor carries player without input");
        fresh(*w);
        add(*w, PlatformKind::Spring, {64, 400, 320, 16});
        w->kino.y = 340;
        w->kino.vy = 200;
        runPhysics(*w, {}, 4);
        check(w->kino.vy < -450 && w->soundEvent == 1, "spring launches on landing exactly once");
        fresh(*w);
        add(*w, PlatformKind::Falling, {64, 400, 320, 16});
        w->kino.y = 352;
        w->kino.grounded = true;
        w->kino.standingPlatformId = featureSupportId(w->level.features[0]);
        w->level.features[0].delay = .05f;
        runPhysics(*w, {}, 10);
        check(w->level.features[0].stage == 1 && w->level.features[0].bounds.y > 400 && w->kino.y > 352,
              "falling platform delay/motion/carry");
        w->kino.x = 700;
        w->kino.grounded = false;
        w->kino.standingPlatformId = -1;
        runPhysics(*w, {}, 300);
        check(w->level.features[0].stage == 0 && w->level.features[0].bounds.y == 400,
              "falling platform bounded respawn");

        fresh(*w);
        w->mobility.dashEnabled = true;
        w->jump.gravity = 0;
        Input dash;
        dash.dash = true;
        runPhysics(*w, dash, 9);
        check(w->kino.x > 189 && w->kino.x < 191, "dash duration limits distance");
        runPhysics(*w, dash, 40);
        check(!w->kino.airDashAvailable && w->kino.dashRemaining == 0,
              "held dash does not repeat and air dash is limited");
        fresh(*w);
        w->mobility.dashEnabled = true;
        w->jump.gravity = 0;
        StaticCollider wall;
        wall.bounds = {140, 0, 2, 600};
        w->level.colliders.push(wall);
        runPhysics(*w, dash, 4);
        check(w->kino.x <= 112.001f && w->kino.dashRemaining == 0, "dash sweeps thin obstacles");
        fresh(*w);
        w->mobility.wallJumpEnabled = true;
        wall.bounds = {128, 0, 32, 600};
        w->level.colliders.push(wall);
        runPhysics(*w, jump);
        check(w->kino.vx < 0 && w->kino.vy < -300 && w->kino.wallLockRemaining > 0 &&
                  w->kino.facing == -1,
              "wall jump and direction lock");
        Input into;
        into.move = 1;
        runPhysics(*w, into);
        check(w->kino.vx < 0 && w->kino.facing == -1, "wall jump temporarily owns horizontal motion");

        fresh(*w);
        w->kino.grounded = true;
        w->kino.standingPlatformId = 0;
        w->kino.y = 300;
        MovingPlatform moving;
        moving.base = {100, 348, 128, 16};
        moving.pos = moving.previous = {100, 348};
        moving.amplitude = 96;
        moving.speed = 1;
        w->level.platforms.push(moving);
        wall.bounds = {128, 200, 32, 148};
        w->level.colliders.push(wall);
        runPhysics(*w);
        check(w->kino.crushed && w->kino.hp == 0 && w->state == TimeState::Hit,
              "rider crush against wall is explicit death");
        fresh(*w);
        w->mobility.crushKills = false;
        w->kino.grounded = true;
        w->kino.standingPlatformId = 0;
        w->kino.y = 300;
        w->level.platforms.push(moving);
        w->level.colliders.push(wall);
        runPhysics(*w);
        check(w->kino.hp > 0 && w->kino.crushed, "nonlethal crush preset");

        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w);
                w->rewind.record_hz = hz;
                w->kino.x = 160;
                w->kino.y = 592;
                w->kino.grounded = true;
                for (int x = 0; x < mapWidth; ++x)
                    w->level.tiles[20 * mapWidth + x] = Tile::Solid;
                for (int kind = 0; kind < 6; ++kind)
                    add(*w, PlatformKind(kind), {800.f + kind * 160, 300, 96, 64});
                w->level.features[4].stage = 1;
                w->level.features[4].velocity = 60;
                w->mobility.dashEnabled = true;
                w->mobility.wallJumpEnabled = true;
                Simulation sim(384ull * 1024 * 1024);
                sim.reset(*w);
                allocations = 0;
                measure = true;
                for (int i = 0; i < 360; ++i)
                    sim.tick({});
                measure = false;
                check(allocations == 0, "features and full snapshots have no steady C++ allocation");
                Input rewind;
                rewind.fast = fast;
                rewind.trace = !fast;
                for (int i = 0; i < 370 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(rewind);
                check(sim.world().clock < .001 && sim.history().size() == 1 &&
                          sim.world().level.features[4].bounds.y == 300 &&
                          sim.world().level.features[4].velocity == 60,
                      "oldest full six-second T/R restores all feature motion and config");
                sim.tick({});
                sim.tick(dash);
                check(sim.world().kino.dashRemaining > 0,
                      "continue with mobility after oldest truncation");
            }
        fresh(*w);
        w->rewind.enabled = false;
        w->mobility.dashEnabled = true;
        Simulation disabled;
        disabled.reset(*w);
        disabled.tick(dash);
        check(disabled.history().capacity() == 0 && disabled.world().kino.dashRemaining > 0,
              "mobility with history disabled");
        if (argc > 1) {
            Resources resources;
            resources.root = std::filesystem::path(argv[1]) / "platform-lua";
            std::filesystem::create_directories(resources.root);
            writeTextAtomically(resources.root / "project.echo", "scripts=scripts/mobility.lua\n");
            std::filesystem::create_directories(resources.root / "scripts");
            writeTextAtomically(resources.root / "scripts/mobility.lua",
                                "mobility.dashEnabled=true\nmobility.dashSpeed=720\nengine.configure_"
                                "platform(1,{speed=650,delay=.2})\n");
            fresh(*w);
            add(*w, PlatformKind::Spring, {64, 400, 96, 16});
            ScriptRuntime script;
            script.load(resources, *w);
            check(w->mobility.dashEnabled && w->mobility.dashSpeed == 720 &&
                      w->level.features[0].speed == 650,
                  "advanced mobility and stable-ID feature parameters");
            EventCommand query;
            query.op = EventOp::IfScript;
            query.text.set(
                "local f=engine.get_platform(1); f.speed=1; return engine.get_platform(1).speed==650 "
                "and engine.get_platform(-1)==nil and mobility.dashEnabled");
            EventCommand end;
            end.op = EventOp::EndIf;
            EventPage page;
            page.commands = {query, end};
            EventDefinition definition;
            definition.pages = {page};
            script.prepareEventScripts({EventMap{{definition}}}, {});
            script.beginEventStep(*w);
            auto before = std::make_unique<WorldState>(*w);
            bool result = false;
            check(script.eventCondition(*w, {0, 1, 0, 0, false}, result) && result &&
                      std::memcmp(before.get(), w.get(), sizeof(WorldState)) == 0,
                  "read-only feature query returns a copy without mutating snapshots");

            auto keys = KeyBindings{};
            std::array<bool, 256> held{};
            held['X'] = held['W'] = true;
            auto action = keyboardInput(keys, held, 16, 72, w->mobility.dashKey, w->mobility.climbKey);
            check(action.dash && action.up, "customizable mobility key input");
        }
        std::cout << "PASS platform features; full-window T/R/30/60/120; disabled; Lua; zero "
                     "heap\nWorldState bytes "
                  << sizeof(WorldState) << '\n';
        return 0;
    } catch (const std::exception& e) {
        measure = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
