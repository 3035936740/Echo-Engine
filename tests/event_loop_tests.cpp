#include "echo/script.hpp"
#include "echo/snapshot_diff.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
using namespace echo;
static bool track{};
static std::size_t allocations{};
void* operator new(std::size_t n) {
    if (track)
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
static std::string legacyText(std::string text) {
    const std::string row = "PAGE_CONDITION 0 \"\"\n";
    for (auto at = text.find(row); at != std::string::npos; at = text.find(row))
        text.erase(at, row.size());
    return text;
}
static void check(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
static EventCommand cmd(EventOp op, int a = 0, float value = 0) {
    EventCommand c;
    c.op = op;
    c.a = a;
    c.value = value;
    return c;
}
static EventMap program(std::initializer_list<EventCommand> commands,
                        EventTrigger trigger = EventTrigger::Action) {
    EventDefinition e;
    e.name.set("Loop example");
    EventPage p;
    p.trigger = trigger;
    p.commands = commands;
    e.pages.push_back(p);
    return EventMap{{e}};
}
static void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.kino.invulnTimer = 10000;
    EventPlacement p;
    p.region = {160, 592, 32, 48};
    w.level.events.push(p);
}
static void start(WorldState& w) {
    w.events.local[0].requested = true;
}
static void advance(WorldState& w, const EventMap& p, int ticks = 1, const EventMap& common = {},
                    EventScriptExecutor* scripts = nullptr) {
    for (int i = 0; i < ticks; ++i)
        stepEvents(w, p, common, false, fixedDt, scripts);
}
static void invalid(const EventMap& p, const char* m) {
    bool failed = false;
    try {
        validateEvents(p);
    } catch (...) {
        failed = true;
    }
    check(failed, m);
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need build directory");
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        EventMap preset{{eventPreset(1, 7)}};
        validateEvents(preset);
        validateEvents(preset, true);
        start(*w);
        advance(*w, preset, 240);
        check(w->events.variables[0] == 3 && !w->events.local[0].running &&
                  !w->events.local[0].loopDepth,
              "No-script timed-loop preset");
        fresh(*w);
        auto finite =
            program({cmd(EventOp::Loop, 3), cmd(EventOp::Loop, 2), cmd(EventOp::AddVariable, 0, 1),
                     cmd(EventOp::EndLoop), cmd(EventOp::EndLoop), cmd(EventOp::AddVariable, 1, 1)});
        validateEvents(finite);
        auto text = saveEvents(finite);
        check(text.starts_with("ECHO_EVENTS_10") && saveEvents(loadEvents(text)) == text,
              "Loop format round trip");
        for (int version = 1; version <= 5; ++version) {
            auto legacy = legacyText(text);
            legacy.replace(0, legacy.find('\n'), "ECHO_EVENTS_" + std::to_string(version));
            bool failed = false;
            try {
                loadEvents(legacy);
            } catch (...) {
                failed = true;
            }
            check(failed, "Old event schema accepted loop opcodes");
        }
        auto scriptLegacy = program({cmd(EventOp::Script)});
        scriptLegacy.events[0].pages[0].commands[0].text.set("engine.state[1]=1");
        auto v5 = legacyText(saveEvents(scriptLegacy));
        v5.replace(0, v5.find('\n'), "ECHO_EVENTS_5");
        check(loadEvents(v5).events[0].pages[0].commands[0].op == EventOp::Script, "v5 Lua migration");
        start(*w);
        advance(*w, finite, 10);
        check(w->events.variables[0] == 6 && w->events.variables[1] == 1 &&
                  w->events.local[0].loopDepth == 0 && !w->events.local[0].running,
              "Nested finite counts");

        invalid(program({cmd(EventOp::BreakLoop)}), "Break outside loop accepted");
        invalid(program({cmd(EventOp::Loop, -1), cmd(EventOp::EndLoop)}), "Negative loop count");
        invalid(program({cmd(EventOp::Loop, 1000001), cmd(EventOp::EndLoop)}), "Unbounded count");
        invalid(program({cmd(EventOp::Loop), cmd(EventOp::Else), cmd(EventOp::EndLoop)}),
                "Else matched loop");
        invalid(program({cmd(EventOp::Loop), cmd(EventOp::EndIf)}), "Crossed ends");
        invalid(program({cmd(EventOp::IfSwitch), cmd(EventOp::Loop), cmd(EventOp::EndIf),
                         cmd(EventOp::EndLoop)}),
                "Crossed nested ends");
        invalid(program({cmd(EventOp::IfSwitch), cmd(EventOp::Else), cmd(EventOp::Else),
                         cmd(EventOp::EndIf)}),
                "Duplicate else");
        invalid(program({cmd(EventOp::Loop)}), "Unclosed loop");
        auto depth = program({});
        for (int i = 0; i < 8; ++i)
            depth.events[0].pages[0].commands.push_back(cmd(EventOp::Loop, 1));
        for (int i = 0; i < 8; ++i)
            depth.events[0].pages[0].commands.push_back(cmd(EventOp::EndLoop));
        validateEvents(depth);
        fresh(*w);
        start(*w);
        advance(*w, depth, 4);
        check(!w->events.local[0].running && !w->events.errors, "Eight loop layers");
        depth.events[0].pages[0].commands.insert(depth.events[0].pages[0].commands.begin(),
                                                 cmd(EventOp::Loop, 1));
        depth.events[0].pages[0].commands.push_back(cmd(EventOp::EndLoop));
        invalid(depth, "Nine loop layers accepted");

        auto branch =
            program({cmd(EventOp::Loop, 3), cmd(EventOp::IfSwitch, 0), cmd(EventOp::Loop),
                     cmd(EventOp::AddVariable, 0, 1), cmd(EventOp::BreakLoop), cmd(EventOp::EndLoop),
                     cmd(EventOp::Else), cmd(EventOp::AddVariable, 1, 1), cmd(EventOp::EndIf),
                     cmd(EventOp::EndLoop), cmd(EventOp::Exit)});
        branch.events[0].pages[0].commands[1].b = 1;
        validateEvents(branch);
        fresh(*w);
        start(*w);
        advance(*w, branch, 10);
        check(w->events.variables[0] == 0 && w->events.variables[1] == 3 &&
                  !w->events.local[0].loopDepth,
              "Skipped branch wrongly entered loop");
        fresh(*w);
        w->events.switches[0] = 1;
        start(*w);
        advance(*w, branch, 10);
        check(w->events.variables[0] == 3 && w->events.variables[1] == 0,
              "Nearest-loop break changed outer count");

        auto breaking = program({cmd(EventOp::Loop), cmd(EventOp::IfSwitch, 0), cmd(EventOp::BreakLoop),
                                 cmd(EventOp::Else), cmd(EventOp::Wait, 0, .1f), cmd(EventOp::EndIf),
                                 cmd(EventOp::EndLoop), cmd(EventOp::AddVariable, 1, 1)});
        breaking.events[0].pages[0].commands[1].b = 1;
        fresh(*w);
        start(*w);
        advance(*w, breaking);
        check(w->events.local[0].loopDepth == 1 && w->events.local[0].wait > 0, "Wait lost loop frame");
        auto waiting = std::make_unique<WorldState>(*w);
        w->events.switches[0] = 1;
        advance(*w, breaking, 30);
        check(w->events.variables[1] == 1 && !w->events.local[0].running &&
                  !w->events.local[0].loopDepth,
              "Break inside condition did not skip matching EndIf/EndLoop");
        restore(*w, *waiting);
        w->events.switches[0] = 1;
        advance(*w, breaking, 30);
        check(w->events.variables[1] == 1, "Wait/loop snapshot restore duplicated outcome");
        fresh(*w);
        start(*w);
        advance(*w, breaking);
        cancelEvents(*w, true);
        check(!w->events.local[0].loopDepth && w->events.local[0].loops[0].remaining == 0,
              "Cancellation retained loop");
        auto switching = breaking;
        EventPage replacement;
        replacement.switchId = 1;
        replacement.commands = {cmd(EventOp::AddVariable, 2, 1)};
        switching.events[0].pages.push_back(replacement);
        fresh(*w);
        start(*w);
        advance(*w, switching);
        w->events.switches[1] = 1;
        advance(*w, switching);
        check(!w->events.local[0].loopDepth && !w->events.local[0].running, "Page switch retained loop");

        auto common = program({cmd(EventOp::Loop, 2), cmd(EventOp::Wait, 0, .02f),
                               cmd(EventOp::AddVariable, 2, 1), cmd(EventOp::EndLoop)});
        auto calls =
            program({cmd(EventOp::Loop, 3), cmd(EventOp::CallCommon, 1), cmd(EventOp::EndLoop)});
        fresh(*w);
        start(*w);
        advance(*w, calls, 100, common);
        check(w->events.variables[2] == 6 && !w->events.local[0].loopDepth &&
                  !w->events.common[0].loopDepth,
              "Called common loops did not complete independently");
        fresh(*w);
        start(*w);
        advance(*w, calls, 2, common);
        cancelEvents(*w, false);
        check(!w->events.local[0].loopDepth && !w->events.common[0].loopDepth &&
                  !w->events.common[0].running,
              "Parent cancel retained child loop");

        auto busy = program({cmd(EventOp::Loop), cmd(EventOp::AddVariable, 0, 1), cmd(EventOp::EndLoop)},
                            EventTrigger::Parallel);
        auto globals = busy;
        globals.events[0].pages[0].commands[1].a = 1;
        fresh(*w);
        for (int id = 2; id <= 32; ++id) {
            auto e = busy.events[0];
            e.id = id;
            busy.events.push_back(e);
            EventPlacement p;
            p.id = id;
            w->level.events.push(p);
        }
        for (int id = 2; id <= 16; ++id) {
            auto e = globals.events[0];
            e.id = id;
            globals.events.push_back(e);
        }
        validateEvents(busy);
        validateEvents(globals, true);
        allocations = 0;
        track = true;
        advance(*w, busy, 600, globals);
        track = false;
        check(allocations == 0 && w->events.budgetYields > 0 && w->events.errors == 0,
              "Busy loops allocate or fail");
        for (const auto& r : w->events.local)
            check(r.running && r.loopDepth == 1, "Local loop starved");
        for (const auto& r : w->events.common)
            check(r.running && r.loopDepth == 1, "Common loop starved");
        check(w->events.variables[0] > 0 && w->events.variables[1] > 0,
              "Global budget blocked common events");

        Resources resources;
        resources.root = std::filesystem::path(argv[1]) / "event-loop-api";
        std::filesystem::create_directories(resources.root);
        std::ofstream(resources.root / "project.echo") << "scripts=\n";
        auto scripted =
            program({cmd(EventOp::Loop, 3), cmd(EventOp::Script), cmd(EventOp::Wait, 0, .03f),
                     cmd(EventOp::EndLoop), cmd(EventOp::Exit)});
        scripted.events[0].pages[0].commands[1].text.set("engine.state[1]=engine.state[1]+1");
        fresh(*w);
        ScriptRuntime script;
        script.load(resources, *w);
        script.prepareEventScripts({scripted}, {});
        start(*w);
        advance(*w, scripted, 40, {}, &script);
        check(w->script.persistent[0] == 3 && !w->events.local[0].loopDepth,
              "Inline Lua in finite loop");
        scripted.events[0].pages[0].commands[1].text.set(
            "engine.set_event_control(5,0); error('loop failed')");
        fresh(*w);
        script.prepareEventScripts({scripted}, {});
        start(*w);
        advance(*w, scripted, 3, {}, &script);
        check(w->events.local[0].scriptFault && !w->events.local[0].loopDepth &&
                  !w->events.local[0].controlActive && w->events.errors == 1,
              "Lua fault retained loop/lock");

        auto ticking =
            program({cmd(EventOp::Loop, 1000000), cmd(EventOp::Loop, 3), cmd(EventOp::AddVariable, 0, 1),
                     cmd(EventOp::Wait, 0, .1f), cmd(EventOp::EndLoop), cmd(EventOp::EndLoop)},
                    EventTrigger::Parallel);
        validateEvents(ticking);
        Simulation sim(384ull * 1024 * 1024);
        sim.setEvents({ticking}, {});
        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w);
                w->rewind.record_hz = hz;
                sim.reset(*w);
                for (int i = 0; i < 420; ++i)
                    sim.tick({});
                check(sim.history().size() >= 6 * hz, "Loop test lost six-second history");
                auto farthest = std::make_unique<WorldState>(sim.history().at(0));
                Input input;
                input.fast = fast;
                input.trace = !fast;
                for (int i = 0; i < 500 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(input);
                check(sim.history().size() == 1 && sim.world().events.local[0].loopDepth == 2 &&
                          sim.world().events.variables == farthest->events.variables &&
                          std::memcmp(sim.world().events.local[0].loops.data(),
                                      farthest->events.local[0].loops.data(),
                                      sizeof(farthest->events.local[0].loops)) == 0,
                      "Farthest T/R lost nested counters");
                sim.tick({});
                auto baseline = std::make_unique<WorldState>(sim.world());
                allocations = 0;
                track = true;
                for (int i = 0; i < 90; ++i)
                    sim.tick({});
                track = false;
                check(allocations == 0, "Loop and snapshot stable ticks allocate");
                auto expected = std::make_unique<WorldState>(sim.world());
                sim.restore(*baseline);
                sim.history().clear();
                sim.record();
                for (int i = 0; i < 90; ++i)
                    sim.tick({});
                check(std::memcmp(&sim.world().events, &expected->events, sizeof(EventState)) == 0,
                      "Farthest continuation/event state replay differs");
            }
        fresh(*w);
        w->rewind.enabled = false;
        sim.reset(*w);
        for (int i = 0; i < 420; ++i)
            sim.tick({});
        auto before = sim.world().events.variables[0];
        sim.tick({.fast = true, .trace = true});
        sim.tick({});
        check(sim.history().allocatedBytes() > 0 && sim.history().size() == 0 &&
                  sim.world().events.variables[0] >= before &&
                  sim.world().events.local[0].loopDepth == 2,
              "Runtime disabled history broke loops");
        Simulation disabled;
        disabled.setEvents({ticking}, {});
        disabled.reset(*w);
        disabled.tick({});
        check(disabled.history().allocatedBytes() == 0 &&
                  disabled.world().events.local[0].loopDepth == 2,
              "Startup disabled history allocated or broke loops");
        auto report = std::make_unique<SnapshotDiff>();
        compareSnapshots(*w, *w, *report);
        std::cout
            << "PASS event loops, structural validation/v1-6, wait/break/common/cancel/Lua, fairness/"
               "budgets/zero allocation, full six-second all-Hz T/R/disabled; world_bytes="
            << sizeof(WorldState) << " scalar_fields=" << report->inspected << '\n';
    } catch (const std::exception& e) {
        track = false;
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
