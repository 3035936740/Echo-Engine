#include "echo/script.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace echo;
static std::string legacyText(std::string text) {
    const std::string row = "PAGE_CONDITION 0 \"\"\n";
    for (auto at = text.find(row); at != std::string::npos; at = text.find(row))
        text.erase(at, row.size());
    return text;
}
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
static EventCommand lua(const char* source) {
    EventCommand c;
    c.op = EventOp::Script;
    c.text.set(source);
    return c;
}
static EventCommand wait(float seconds) {
    EventCommand c;
    c.op = EventOp::Wait;
    c.value = seconds;
    return c;
}
static EventMap event(const char* source, EventTrigger trigger = EventTrigger::Parallel) {
    EventDefinition e;
    e.id = 1;
    e.name.set("Script test");
    EventPage p;
    p.trigger = trigger;
    p.commands = {lua(source), wait(.1f)};
    e.pages.push_back(p);
    EventMap map;
    map.events.push_back(e);
    return map;
}
static void place(WorldState& w, int id = 1) {
    EventPlacement p;
    p.id = id;
    p.region = {160, 592, 32, 48};
    w.level.events.push(p);
}
static void fresh(WorldState& w) {
    w = makeWorld();
}
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Need build directory");
        Resources resources;
        resources.root = std::filesystem::path(argv[1]) / "event-script-api";
        std::filesystem::create_directories(resources.root);
        std::ofstream(resources.root / "project.echo") << "scripts=\n";
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        w->aster.enabled = false;
        w->script = {};
        w->kino.invulnTimer = 10000;
        ScriptRuntime runtime;
        runtime.load(resources, *w);
        auto map = event("-- multiline UTF-8: \u811a\u672c\nengine.state[1]=engine.state[1]+1\n"
                         "engine.set_variable(1,engine.get_variable(1)+1)");
        auto text = saveEvents(map);
        check(text.starts_with("ECHO_EVENTS_10") && saveEvents(loadEvents(text)) == text,
              "Multiline script format round trip");
        auto old = event("ignored");
        old.events[0].pages[0].commands = {wait(.1f)};
        for (int version = 1; version <= 4; ++version) {
            auto t = legacyText(saveEvents(old));
            t.replace(0, std::strlen("ECHO_EVENTS_10"), "ECHO_EVENTS_" + std::to_string(version));
            if (version == 1) {
                // Legacy v1 has no stable reference column.
                auto last = t.rfind(" 0\n");
                check(last != std::string::npos, "Legacy command tail");
                t.erase(last, 2);
            }
            check(loadEvents(t).events.size() == 1, "Old format migration");
        }
        for (int version = 1; version <= 4; ++version) {
            auto t = legacyText(text);
            t.replace(0, std::strlen("ECHO_EVENTS_10"), "ECHO_EVENTS_" + std::to_string(version));
            bool rejected = false;
            try {
                loadEvents(t);
            } catch (...) {
                rejected = true;
            }
            check(rejected, "Old format silently accepted new script opcode");
        }
        bool rejected = false;
        try {
            validateEvents(event(" \n\t"));
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Empty event Lua accepted");
        rejected = false;
        try {
            runtime.validateEventScripts(event("local ="), false, 1);
        } catch (const std::exception& e) {
            rejected = std::string(e.what()).find("map 2 event 1 page 1 command 1") != std::string::npos;
        }
        check(rejected, "Compile error missing event location");
        runtime.prepareEventScripts({map}, {});
        check(runtime.eventScript(*w, {0, 1, 0, 0, false}) && w->script.persistent[0] == 1 &&
                  w->events.variables[0] == 1,
              "Precompiled script execution");
        rejected = false;
        try {
            runtime.prepareEventScripts({event("local =")}, {});
        } catch (...) {
            rejected = true;
        }
        check(rejected && runtime.eventScript(*w, {0, 1, 0, 0, false}) && w->script.persistent[0] == 2,
              "Failed prepare destroyed old compiled definitions");
        runtime.prepareEventScripts(
            {event("local map,common,id,page,command=engine.event_context()\n"
                   "assert(map==1 and not common and id==1 and page==1 and command==1)\n"
                   "engine.set_switch(1,true); jump.coyote_time=.11; rewind.history_seconds=12\n"
                   "engine.set_collision_pair('player','projectile',false)\n"
                   "engine.fade_bgm(.4,.3); player.hp=3\n"
                   "engine.configure_focus{enabled=true,speed_ratio=.3}")},
            {});
        check(runtime.eventScript(*w, {0, 1, 0, 0, false}) && w->events.switches[0] &&
                  w->jump.coyote_time == .11f && w->rewind.history_seconds == 12 && w->kino.hp == 3 &&
                  w->focus.speedRatio == .3f,
              "Engine API not available in event Lua");
        runtime.prepareEventScripts(
            {event("engine.set_self_switch('B',true); assert(engine.get_self_switch('B'))\n"
                   "engine.set_event_control(5,64); engine.request_transfer(1,200,300,false)")},
            {});
        check(runtime.eventScript(*w, {0, 1, 0, 0, false}) && w->events.self[0][0] == 2 &&
                  w->events.local[0].controlActive && w->events.local[0].controlAllowed == 64 &&
                  w->events.transferMap == 0 && w->events.transferPoint.x == 200 &&
                  !w->events.transferSpawn,
              "Event owned self switch/control/transfer API");
        for (auto code : {"engine.state[1]=99; hidden=1", "engine.state[1]=99; engine.state={}",
                          "engine.state[1]=99; math.sin=1", "engine.state[1]=99; engine.state[2]={}",
                          "engine.state[1]=99; while true do end",
                          "engine.state[1]=99; local x=string.rep('x',16*1024*1024)",
                          "engine.state[1]=99; engine.set_variable(1,42); error({})",
                          "engine.state[1]=99; os.execute('echo bad')"}) {
            runtime.prepareEventScripts({event(code)}, {});
            auto before = std::make_unique<WorldState>(*w);
            check(!runtime.eventScript(*w, {0, 1, 0, 0, false}) &&
                      std::memcmp(w.get(), before.get(), sizeof(WorldState)) == 0,
                  "Invalid command committed partial state or bypassed budget");
            check(runtime.eventError().find("map 1 event 1 page 1 command 1") != std::string::npos,
                  "Runtime error missing event location");
        }
        runtime.prepareEventScripts({event("local s=engine.state; s[1]=s[1]+1\n"
                                           "if s[1]==1 then _ENV={} else s[2]=engine.random() end")},
                                    {});
        w->script.persistent[0] = 0;
        check(runtime.eventScript(*w, {0, 1, 0, 0, false}) &&
                  runtime.eventScript(*w, {0, 1, 0, 0, false}) && w->script.persistent[0] == 2,
              "Rebound environment persisted outside snapshots");

        // Failure releases only its owner, retains a snapshot fault latch, and avoids retry floods.
        fresh(*w);
        w->aster.enabled = false;
        place(*w);
        auto bad = event("engine.set_variable(1,99); error('fault')");
        EventCommand lock;
        lock.op = EventOp::PlayerControl;
        lock.a = 1;
        bad.events[0].pages[0].commands.insert(bad.events[0].pages[0].commands.begin(), lock);
        runtime.prepareEventScripts({bad}, {});
        stepEvents(*w, bad, {}, false, fixedDt, &runtime);
        check(w->events.local[0].scriptFault && !w->events.local[0].controlActive &&
                  w->events.foreground == -1 && w->events.errors == 1 && w->events.variables[0] == 0 &&
                  w->events.local[0].pc == 1,
              "Fault didn't stop/release/rollback owner");
        auto fault = std::make_unique<WorldState>(*w);
        for (int i = 0; i < 30; ++i)
            stepEvents(*w, bad, {}, false, fixedDt, &runtime);
        check(w->events.errors == 1, "Fault command automatically retried each frame");
        restore(*w, *fault);
        runtime.afterRestore(*w);
        stepEvents(*w, bad, {}, false, fixedDt, &runtime);
        check(w->events.local[0].scriptFault && w->events.errors == 1, "Fault didn't restore");
        EventPage recovery;
        recovery.trigger = EventTrigger::Parallel;
        recovery.switchId = 0;
        recovery.commands = {lua("engine.set_variable(1,7)"), wait(1)};
        bad.events[0].pages.push_back(recovery);
        runtime.prepareEventScripts({bad}, {});
        w->events.switches[0] = 1;
        stepEvents(*w, bad, {}, false, fixedDt, &runtime);
        check(!w->events.local[0].scriptFault && w->events.variables[0] == 7,
              "Page switch didn't clear fault");

        fresh(*w);
        place(*w);
        auto missing = event("engine.state[1]=1");
        stepEvents(*w, missing, {}, false, fixedDt);
        check(w->events.local[0].scriptFault && w->events.errors == 1,
              "Missing Lua executor silently skipped command");
        // A faulted common child unblocks its caller and is not repeatedly requested.
        fresh(*w);
        place(*w);
        auto parent = event("ignored");
        EventCommand call;
        call.op = EventOp::CallCommon;
        call.a = 1;
        EventCommand add;
        add.op = EventOp::AddVariable;
        add.a = 1;
        add.value = 1;
        parent.events[0].pages[0].commands = {call, add, wait(1)};
        auto common = event("error('child')", EventTrigger::Action);
        runtime.prepareEventScripts({parent}, common);
        for (int i = 0; i < 5; ++i)
            stepEvents(*w, parent, common, false, fixedDt, &runtime);
        check(w->events.common[0].scriptFault && w->events.variables[1] == 1 &&
                  w->events.local[0].waitCommon < 0,
              "Faulted common child left caller waiting");

        // Parallel scripts share one step budget, not 32 unrestricted VM budgets.
        fresh(*w);
        w->script = {};
        EventMap crowded;
        for (int id = 1; id <= 4; ++id) {
            auto e = event("engine.state[1]=engine.state[1]+1").events[0];
            e.id = id;
            e.pages[0].commands.assign(16, lua("engine.state[1]=engine.state[1]+1"));
            crowded.events.push_back(e);
            place(*w, id);
        }
        runtime.prepareEventScripts({crowded}, {});
        stepEvents(*w, crowded, {}, false, fixedDt, &runtime);
        check(w->script.persistent[0] == 32 && w->events.errors == 2 && w->events.local[2].scriptFault &&
                  w->events.local[3].scriptFault,
              "Parallel event Lua bypassed shared per-step call budget");
        // A compile heap limit also preserves the previous complete cache on failure.
        EventMap huge;
        std::string program;
        for (int i = 0; i < 28; ++i)
            program += "local v" + std::to_string(i) + "='" + std::string(16, 'x') + "';";
        for (int id = 1; id <= 32; ++id) {
            auto e = event(program.c_str()).events[0];
            e.id = id;
            e.pages.assign(4, e.pages[0]);
            for (auto& page : e.pages)
                page.commands.assign(32, lua(program.c_str()));
            huge.events.push_back(e);
        }
        bool compileBudget = false;
        try {
            runtime.prepareEventScripts(std::vector<EventMap>(8, huge), {});
        } catch (const std::exception&) {
            compileBudget = true;
        }
        check(compileBudget, "Unbounded event compile cache exceeded heap budget");
        runtime.beginEventStep(*w);
        check(runtime.eventScript(*w, {0, 1, 0, 0, false}),
              "Compile exhaustion destroyed previous cache");

        // Immutable cached code across maps/common, with deterministic RNG and six-second histories.
        auto ticking = event("engine.state[1]=engine.state[1]+1\n"
                             "engine.state[2]=engine.random()\n"
                             "engine.set_variable(1,engine.get_variable(1)+1)");
        auto global = event("local map,common=engine.event_context();assert(common)\n"
                            "engine.state[3]=engine.state[3]+1");
        for (int hz : {30, 60, 120}) {
            fresh(*w);
            w->aster.enabled = false;
            w->script = {};
            w->kino.invulnTimer = 10000;
            w->rewind.record_hz = hz;
            place(*w);
            Simulation sim(384ull * 1024 * 1024);
            ScriptRuntime script;
            script.load(resources, *w);
            sim.setHooks(&script);
            sim.setEvents({ticking}, global);
            sim.reset(*w);
            for (int i = 0; i < 420; ++i)
                sim.tick({});
            check(sim.history().size() >= std::size_t(6 * hz), "Full six-second event script window");
            auto farthest = std::make_unique<WorldState>(sim.history().at(0));
            sim.seekHistory(0);
            for (int i = 0; i < 60; ++i)
                sim.tick({});
            Simulation replay(16ull * 1024 * 1024);
            ScriptRuntime replayScript;
            replayScript.load(resources, *w);
            replay.setHooks(&replayScript);
            replay.setEvents({ticking}, global);
            replay.reset(*w);
            replay.restore(*farthest);
            for (int i = 0; i < 60; ++i)
                replay.tick({});
            check(sim.world().script.persistent == replay.world().script.persistent &&
                      sim.world().events.variables == replay.world().events.variables &&
                      sim.world().randomSeed == replay.world().randomSeed,
                  "Event Lua farthest branch diverged from restored replay");
            sim.tick({.trace = true});
            sim.tick({});
            sim.tick({.fast = true});
            sim.tick({});
            check(sim.world().kino.hp > 0 && sim.world().events.errors == 0,
                  "Event Lua T/R continuation");
            auto cfg = sim.world().rewind;
            cfg.enabled = false;
            sim.configureRewind(cfg);
            sim.tick({.fast = true, .trace = true});
            sim.tick({});
            check(sim.world().events.errors == 0, "Disabled rewind event Lua");
            auto previous = sim.history().size();
            bool fail = false;
            try {
                sim.setEvents({event("local =")}, global);
            } catch (...) {
                fail = true;
            }
            check(fail && sim.history().size() == previous, "Invalid script cleared active history");
            sim.tick({});
        }
        std::cout
            << "PASS inline event Lua, v1-5 migration, compile cache/locations, readonly environment, "
               "budgets/atomic rollback, owner fault/recovery/common release, six-second all-Hz T/R and "
               "disabled\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
