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
static EventCommand cmd(EventOp op, int a = 0, float v = 0) {
    EventCommand c;
    c.op = op;
    c.a = a;
    c.value = v;
    return c;
}
static EventCommand condition(const char* source) {
    auto c = cmd(EventOp::IfScript);
    c.text.set(source);
    return c;
}
static EventCommand script(const char* source) {
    auto c = cmd(EventOp::Script);
    c.text.set(source);
    return c;
}
static EventMap program(const char* source, EventTrigger trigger = EventTrigger::Action) {
    EventPage p;
    p.trigger = trigger;
    p.commands = {condition(source),   cmd(EventOp::AddVariable, 0, 1),
                  cmd(EventOp::Else),  cmd(EventOp::AddVariable, 1, 1),
                  cmd(EventOp::EndIf), cmd(EventOp::Wait, 0, .1f)};
    EventDefinition e;
    e.name.set("Lua condition");
    e.pages.push_back(p);
    return EventMap{{e}};
}
static void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.kino.invulnTimer = 10000;
    w.script = {};
    EventPlacement p;
    p.region = {160, 592, 32, 48};
    w.level.events.push(p);
}
static void tick(WorldState& w, const EventMap& map, ScriptRuntime* script, int count = 1,
                 const EventMap& common = {}) {
    for (int i = 0; i < count; ++i)
        stepEvents(w, map, common, false, fixedDt, script);
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need build directory");
        Resources resources;
        resources.root = std::filesystem::path(argv[1]) / "event-condition-api";
        std::filesystem::create_directories(resources.root);
        std::ofstream(resources.root / "project.echo") << "scripts=\n";
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        ScriptRuntime runtime;
        runtime.load(resources, *w);
        auto map = program("-- UTF-8: \u6761\u4ef6\nlocal ready=player.hp>0\nreturn ready and not "
                           "engine.get_switch(1)");
        auto text = saveEvents(map);
        check(text.starts_with("ECHO_EVENTS_10") && saveEvents(loadEvents(text)) == text,
              "v7 round trip");
        for (int version = 1; version <= 6; ++version) {
            auto old = legacyText(text);
            old.replace(0, old.find('\n'), "ECHO_EVENTS_" + std::to_string(version));
            bool failed = false;
            try {
                loadEvents(old);
            } catch (...) {
                failed = true;
            }
            check(failed, "Old schema accepted Lua condition");
        }
        auto loops = EventMap{{eventPreset(1, 7)}};
        auto v6 = legacyText(saveEvents(loops));
        v6.replace(0, v6.find('\n'), "ECHO_EVENTS_6");
        check(loadEvents(v6).events[0].pages[0].commands[0].op == EventOp::Loop, "v6 loop migration");
        auto invalid = program("");
        bool failed = false;
        try {
            validateEvents(invalid);
        } catch (...) {
            failed = true;
        }
        check(failed, "Empty condition accepted");
        invalid = program("return true");
        invalid.events[0].pages[0].commands.pop_back();
        invalid.events[0].pages[0].commands.pop_back();
        failed = false;
        try {
            validateEvents(invalid);
        } catch (...) {
            failed = true;
        }
        check(failed, "Unclosed Lua condition");
        invalid = program("local =");
        failed = false;
        try {
            runtime.validateEventScripts(invalid, false, 1);
        } catch (const std::exception& e) {
            failed = std::string(e.what()).find("map 2 event 1 page 1 command 1") != std::string::npos;
        }
        check(failed, "Condition syntax location");
        runtime.prepareEventScripts({map}, {});
        bool result = false;
        auto before = std::make_unique<WorldState>(*w);
        check(runtime.eventCondition(*w, {0, 1, 0, 0, false}, result) && result &&
                  std::memcmp(w.get(), before.get(), sizeof(WorldState)) == 0,
              "True condition changed world");
        w->events.switches[0] = 1;
        before = std::make_unique<WorldState>(*w);
        result = true;
        check(runtime.eventCondition(*w, {0, 1, 0, 0, false}, result) && !result &&
                  std::memcmp(w.get(), before.get(), sizeof(WorldState)) == 0,
              "False treated as error or changed world");
        failed = false;
        try {
            runtime.prepareEventScripts({invalid}, {});
        } catch (...) {
            failed = true;
        }
        check(failed && runtime.eventCondition(*w, {0, 1, 0, 0, false}, result) && !result,
              "Failed compile destroyed condition cache");
        check(!runtime.eventScript(*w, {0, 1, 0, 0, false}),
              "Condition cache executed as mutating script");
        runtime.prepareEventScripts(
            {program("local map,common,id,page,pc=engine.event_context()\n"
                     "return map==1 and not common and id==1 and page==1 and pc==1 "
                     "and engine.get_variable(1)==0 and not engine.get_self_switch('B') "
                     "and engine.collision_pair('player','projectile')")},
            {});
        fresh(*w);
        runtime.beginEventStep(*w);
        check(runtime.eventCondition(*w, {0, 1, 0, 0, false}, result) && result,
              "Condition getters/context");
        auto common = program("local map,common,id=engine.event_context()\nreturn common and id==1 and "
                              "not engine.get_self_switch('A')");
        runtime.prepareEventScripts({{}, common}, common);
        runtime.beginEventStep(*w);
        check(runtime.eventCondition(*w, {1, 1, 0, 0, true}, result) && result, "Common context");
        auto onlyReads =
            program("return engine.random==nil and engine.set_variable==nil and engine.set_switch==nil "
                    "and engine.request_transfer==nil and engine.set_event_control==nil "
                    "and engine.create_object==nil and barrage==nil and fire==nil and wait==nil");
        runtime.prepareEventScripts({onlyReads}, {});
        runtime.beginEventStep(*w);
        check(runtime.eventCondition(*w, {0, 1, 0, 0, false}, result) && result,
              "Mutating condition capabilities exposed");
        for (auto source : {"return nil",
                            "return 0",
                            "return 1",
                            "return 'true'",
                            "return {}",
                            "local x=1",
                            "return true,false",
                            "return true,nil",
                            "while true do end",
                            "error({})",
                            "local x=string.rep('x',16*1024*1024);return true",
                            "player.hp=2;return true",
                            "jump.gravity=1;return true",
                            "rewind.enabled=false;return true",
                            "boss.hp=1;return true",
                            "music.volume=.2;return true",
                            "engine.state[1]=99;return true",
                            "persistent[1]=99;return true",
                            "hidden=1;return true",
                            "engine.set_variable(1,3);return true",
                            "return engine.random()",
                            "engine.state={};return true",
                            "math.sin=1;return true"}) {
            runtime.prepareEventScripts({program(source)}, {});
            runtime.beginEventStep(*w);
            before = std::make_unique<WorldState>(*w);
            result = true;
            check(!runtime.eventCondition(*w, {0, 1, 0, 0, false}, result) && !result &&
                      std::memcmp(w.get(), before.get(), sizeof(WorldState)) == 0,
                  "Bad condition committed effects or output");
            check(runtime.eventError().find("map 1 event 1 page 1 command 1") != std::string::npos,
                  "Condition error missing location");
        }
        auto pure = program("local result=player.hp>0;_ENV={};return result");
        runtime.prepareEventScripts({pure}, {});
        runtime.beginEventStep(*w);
        for (int i = 0; i < 5; ++i)
            check(runtime.eventCondition(*w, {0, 1, 0, 0, false}, result) && result,
                  "Condition ENV leaked");
        auto normal = program("return true");
        normal.events[0].pages[0].commands = {script("engine.state[1]=99;player.hp=2")};
        runtime.prepareEventScripts({normal}, {});
        runtime.beginEventStep(*w);
        check(runtime.eventScript(*w, {0, 1, 0, 0, false}) && w->script.persistent[0] == 99 &&
                  w->kino.hp == 2,
              "Condition readonly flag leaked to normal script");

        map = program("return engine.get_variable(3) >= 2");
        runtime.prepareEventScripts({map}, {});
        fresh(*w);
        w->events.local[0].requested = true;
        tick(*w, map, &runtime, 20);
        check(w->events.variables[0] == 0 && w->events.variables[1] == 1 && !w->events.errors,
              "False Else path");
        fresh(*w);
        w->events.variables[2] = 2;
        w->events.local[0].requested = true;
        tick(*w, map, &runtime, 20);
        check(w->events.variables[0] == 1 && w->events.variables[1] == 0 && !w->events.errors,
              "True path");
        auto nested = map;
        nested.events[0].pages[0].commands = {cmd(EventOp::Loop, 3),
                                              condition("return true"),
                                              condition("return false"),
                                              cmd(EventOp::AddVariable, 0, 100),
                                              cmd(EventOp::Else),
                                              cmd(EventOp::AddVariable, 0, 1),
                                              cmd(EventOp::EndIf),
                                              cmd(EventOp::Else),
                                              cmd(EventOp::AddVariable, 1, 1),
                                              cmd(EventOp::EndIf),
                                              cmd(EventOp::EndLoop)};
        validateEvents(nested);
        runtime.prepareEventScripts({nested}, {});
        fresh(*w);
        w->events.local[0].requested = true;
        tick(*w, nested, &runtime, 8);
        check(w->events.variables[0] == 3 && w->events.variables[1] == 0 &&
                  !w->events.local[0].loopDepth,
              "Nested Lua/Else/loop");
        nested.events[0].pages[0].commands[1] = condition("return false");
        runtime.prepareEventScripts({nested}, {});
        fresh(*w);
        w->events.local[0].requested = true;
        tick(*w, nested, &runtime, 8);
        check(w->events.variables[0] == 0 && w->events.variables[1] == 3, "Skip nested Lua condition");

        auto fault = program("player.hp=0;return true", EventTrigger::Parallel);
        fault.events[0].pages[0].commands.insert(fault.events[0].pages[0].commands.begin(),
                                                 cmd(EventOp::PlayerControl, 1));
        runtime.prepareEventScripts({fault}, {});
        fresh(*w);
        tick(*w, fault, &runtime, 30);
        check(w->events.local[0].scriptFault && w->events.errors == 1 &&
                  !w->events.local[0].controlActive && w->kino.hp > 0 && w->events.variables[0] == 0 &&
                  w->events.variables[1] == 0,
              "Condition fault/rollback/retry latch");
        before = std::make_unique<WorldState>(*w);
        restore(*w, *before);
        tick(*w, fault, &runtime, 10);
        check(w->events.errors == 1, "Restored condition fault retried");
        EventPage recovery;
        recovery.switchId = 1;
        recovery.trigger = EventTrigger::Parallel;
        recovery.commands = {cmd(EventOp::Wait, 0, 1)};
        fault.events[0].pages.push_back(recovery);
        runtime.prepareEventScripts({fault}, {});
        w->events.switches[1] = 1;
        tick(*w, fault, &runtime);
        check(!w->events.local[0].scriptFault, "Condition fault not released on page switch");
        auto caller = program("return true");
        caller.events[0].pages[0].commands = {cmd(EventOp::CallCommon, 1),
                                              cmd(EventOp::AddVariable, 2, 1)};
        common = program("return 1");
        runtime.prepareEventScripts({caller}, common);
        fresh(*w);
        w->events.local[0].requested = true;
        tick(*w, caller, &runtime, 10, common);
        check(w->events.common[0].scriptFault && w->events.variables[2] == 1 &&
                  !w->events.local[0].running,
              "Condition fault stranded common caller");
        fresh(*w);
        w->events.local[0].requested = true;
        tick(*w, map, nullptr, 2);
        check(w->events.local[0].scriptFault && w->events.errors == 1,
              "Missing condition executor silently false");

        auto flood = program("return true", EventTrigger::Parallel);
        flood.events[0].pages[0].commands = {};
        for (int i = 0; i < 5; ++i) {
            flood.events[0].pages[0].commands.push_back(condition("return true"));
            flood.events[0].pages[0].commands.push_back(script("engine.state[1]=engine.state[1]+1"));
            flood.events[0].pages[0].commands.push_back(cmd(EventOp::EndIf));
        }
        fresh(*w);
        for (int id = 2; id <= 4; ++id) {
            auto e = flood.events[0];
            e.id = id;
            flood.events.push_back(e);
            EventPlacement p;
            p.id = id;
            w->level.events.push(p);
        }
        runtime.prepareEventScripts({flood}, {});
        tick(*w, flood, &runtime);
        check(w->script.persistent[0] == 16 && w->events.errors == 1 && w->events.local[3].scriptFault,
              "Condition and command did not share 32-call step budget");

        auto ticking = program("return engine.get_variable(1)%2==0", EventTrigger::Parallel);
        ticking.events[0].pages[0].commands = {cmd(EventOp::Loop, 1000000),
                                               condition("return engine.get_variable(1)%2==0"),
                                               cmd(EventOp::AddVariable, 1, 1),
                                               cmd(EventOp::Else),
                                               cmd(EventOp::AddVariable, 2, 1),
                                               cmd(EventOp::EndIf),
                                               script("engine.set_variable(1,engine.get_variable(1)+1)"),
                                               cmd(EventOp::Wait, 0, .1f),
                                               cmd(EventOp::EndLoop)};
        validateEvents(ticking);
        ScriptRuntime simScript;
        simScript.load(resources, *w);
        Simulation sim(384ull * 1024 * 1024);
        sim.setHooks(&simScript);
        sim.setEvents({ticking}, {});
        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w);
                w->rewind.record_hz = hz;
                sim.reset(*w);
                for (int i = 0; i < 420; ++i)
                    sim.tick({});
                check(sim.history().size() >= 6 * hz && !sim.world().events.errors,
                      "Condition history window");
                auto farthest = std::make_unique<WorldState>(sim.history().at(0));
                Input input;
                input.fast = fast;
                input.trace = !fast;
                for (int i = 0; i < 500 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(input);
                check(sim.history().size() == 1 &&
                          sim.world().events.variables == farthest->events.variables,
                      "Farthest condition branch state lost");
                sim.tick({});
                auto baseline = std::make_unique<WorldState>(sim.world());
                for (int i = 0; i < 90; ++i)
                    sim.tick({});
                auto expected = std::make_unique<WorldState>(sim.world());
                sim.restore(*baseline);
                sim.history().clear();
                sim.record();
                for (int i = 0; i < 90; ++i)
                    sim.tick({});
                check(std::memcmp(&sim.world().events, &expected->events, sizeof(EventState)) == 0,
                      "Farthest condition continue not exact");
                auto size = sim.history().size();
                failed = false;
                try {
                    sim.setEvents({program("return (")}, {});
                } catch (...) {
                    failed = true;
                }
                check(failed && sim.history().size() == size, "Invalid condition cleared old history");
            }
        fresh(*w);
        w->rewind.enabled = false;
        sim.reset(*w);
        for (int i = 0; i < 420; ++i)
            sim.tick({});
        sim.tick({.fast = true, .trace = true});
        sim.tick({});
        check(sim.history().size() == 0 && sim.world().events.variables[0] > 0 &&
                  !sim.world().events.errors,
              "Disabled rewind condition");
        std::cout
            << "PASS Lua conditions, v1-7, exact boolean/getter environment/readonly/rollback, compile "
               "cache/context/fault/common/budgets, nested branches/loops, six-second all-Hz "
               "T/R/disabled\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
