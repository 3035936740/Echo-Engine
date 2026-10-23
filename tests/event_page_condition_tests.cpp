#include "echo/script.hpp"
#include "echo/snapshot_diff.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace echo;
static void check(bool yes, const char* message) {
    if (!yes)
        throw std::runtime_error(message);
}
static EventCommand command(EventOp op, int a = 0, float value = 0) {
    EventCommand c;
    c.op = op;
    c.a = a;
    c.value = value;
    return c;
}
static EventMap program(const char* source, int id = 1) {
    EventDefinition e;
    e.id = id;
    e.name.set("Page condition");
    EventPage fallback;
    fallback.trigger = EventTrigger::Parallel;
    fallback.graphic = 1;
    fallback.commands = {command(EventOp::AddVariable, 0, 1), command(EventOp::Wait, 0, .05f)};
    e.pages.push_back(fallback);
    EventPage advanced;
    advanced.trigger = EventTrigger::Parallel;
    advanced.graphic = 2;
    advanced.scriptConditionEnabled = true;
    advanced.scriptCondition.set(source);
    advanced.commands = {command(EventOp::AddVariable, 1, 1), command(EventOp::Wait, 0, .05f)};
    e.pages.push_back(advanced);
    return EventMap{{e}};
}
static void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.kino.invulnTimer = 10000;
    w.script = {};
    EventPlacement e;
    e.region = {160, 592, 32, 48};
    w.level.events.push(e);
}
static void tick(WorldState& w, const EventMap& local, ScriptRuntime* lua, const EventMap& common = {}) {
    stepEvents(w, local, common, false, fixedDt, lua);
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "binary directory required");
        Resources resources;
        resources.root = std::filesystem::path(argv[1]) / "page-condition-api";
        std::filesystem::create_directories(resources.root);
        std::ofstream(resources.root / "project.echo") << "scripts=\n";
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        ScriptRuntime lua;
        lua.load(resources, *w);
        auto map = program("-- multiline page\nreturn player.hp>0 and engine.get_switch(1)");
        auto text = saveEvents(map);
        check(text.starts_with("ECHO_EVENTS_10") && saveEvents(loadEvents(text)) == text,
              "v8 round trip");
        auto legacy = EventMap{{eventPreset(1, 0)}};
        auto old = saveEvents(legacy);
        const std::string marker = "PAGE_CONDITION 0 \"\"\n";
        old.erase(old.find(marker), marker.size());
        old.replace(0, old.find('\n'), "ECHO_EVENTS_7");
        auto migrated = loadEvents(old);
        check(!migrated.events[0].pages[0].scriptConditionEnabled &&
                  migrated.events[0].pages[0].scriptCondition.c_str()[0] == 0,
              "legacy default");
        bool rejected = false;
        try {
            auto damaged = text;
            damaged.replace(damaged.find("PAGE_CONDITION 1"), 16, "PAGE_CONDITION 2");
            loadEvents(damaged);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "invalid enabled flag");
        rejected = false;
        try {
            auto nul = text;
            nul.insert(nul.find("-- multiline page") + 2, 1, '\0');
            loadEvents(nul);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "embedded NUL page source must not silently truncate");
        rejected = false;
        try {
            validateEvents(program(" \n\t"));
        } catch (...) {
            rejected = true;
        }
        check(rejected, "empty enabled page source");

        auto disabled = program("return (");
        disabled.events[0].pages[1].scriptConditionEnabled = false;
        lua.prepareEventScripts({disabled}, {});
        tick(*w, disabled, &lua);
        check(w->events.local[0].page == 1 && !w->events.errors, "disabled script must not compile/run");
        fresh(*w);
        lua.prepareEventScripts({map}, {});
        tick(*w, map, &lua);
        check(w->events.local[0].page == 0 && w->events.variables[0] == 1 &&
                  w->level.events[0].graphic == 1,
              "false should select fallback");
        w->events.switches[0] = 1;
        tick(*w, map, &lua);
        check(w->events.local[0].page == 1 && w->events.variables[1] == 1 &&
                  w->level.events[0].graphic == 2,
              "true should switch page and reset old task");
        map.events[0].pages[1].variableId = 2;
        map.events[0].pages[1].variableMinimum = 5;
        tick(*w, map, &lua);
        check(w->events.local[0].page == 0, "builtin and Lua conjunction");
        w->events.variables[2] = 5;
        tick(*w, map, &lua);
        check(w->events.local[0].page == 1, "builtin gating");
        // Highest true page wins; a rejected built-in condition never evaluates invalid Lua.
        auto gated = program("error('should not evaluate')");
        gated.events[0].pages[1].switchId = 0;
        fresh(*w);
        lua.prepareEventScripts({gated}, {});
        tick(*w, gated, &lua);
        check(w->events.local[0].page == 0 && !w->events.errors, "built-in short circuit");
        struct Counter : EventScriptExecutor {
            int calls{};
            bool eventCondition(WorldState&, const EventScriptContext& c, bool& result) override {
                check(c.command == -1, "page condition slot");
                ++calls;
                result = true;
                return true;
            }
        } counter;
        auto action = program("return true");
        action.events[0].pages[1].trigger = EventTrigger::Action;
        fresh(*w);
        stepEvents(*w, action, {}, true, fixedDt, &counter);
        check(counter.calls == 1 && w->events.variables[1] == 1,
              "interaction and schedule must share one page sample");
        // Page context has command index 0 (no command), one-based map/event/page.
        auto context = program("local m,c,e,p,pc=engine.event_context(); return m==1 and not c and e==1 "
                               "and p==2 and pc==0");
        fresh(*w);
        lua.prepareEventScripts({context}, {});
        tick(*w, context, &lua);
        check(w->events.local[0].page == 1, "map page context");
        auto common = program(
            "local m,c,e,p,pc=engine.event_context(); return m==1 and c and e==1 and p==2 and pc==0");
        fresh(*w);
        lua.prepareEventScripts({}, common);
        tick(*w, {}, &lua, common);
        check(w->events.common[0].page == 1 && w->events.variables[1] == 1, "common page context");
        // A runtime error cannot grant the fallback, leave controls/dialogue/children alive,
        // mutate state or retry every tick. The latch survives full restore.
        for (const char* source : {"return 1", "return true,false", "player.hp=0; return true",
                                   "engine.set_variable(1,99); return true", "while true do end"}) {
            auto bad = program(source);
            fresh(*w);
            lua.prepareEventScripts({bad}, {});
            w->events.local[0].page = 1;
            w->events.local[0].running = true;
            w->events.local[0].controlActive = true;
            w->events.local[0].controlAllowed = 0;
            w->events.common[0].running = true;
            w->events.common[0].called = true;
            w->events.common[0].caller = 0;
            w->dialogue.active = 8;
            w->events.dialogueOwner = 0;
            w->gameplayClockPaused = true;
            int hp = w->kino.hp;
            tick(*w, bad, &lua);
            check(w->events.local[0].pageConditionFault == 1 && w->events.errors == 1 &&
                      !w->events.local[0].running && !w->events.common[0].running &&
                      !w->events.local[0].controlActive && w->dialogue.active == -1 &&
                      !w->gameplayClockPaused && w->kino.hp == hp && w->events.variables[0] == 0 &&
                      w->events.variables[1] == 0 && !w->level.events[0].visible &&
                      !w->level.events[0].solid,
                  "fault containment");
            auto saved = std::make_unique<WorldState>(*w);
            for (int i = 0; i < 30; ++i)
                tick(*w, bad, &lua);
            check(w->events.errors == 1, "fault retries");
            *w = *saved;
            tick(*w, bad, &lua);
            check(w->events.errors == 1, "fault restore");
            check(lua.eventError().find("page 2 page condition") != std::string::npos, "fault location");
            cancelEvents(*w, true);
            check(w->events.local[0].pageConditionFault == -1, "explicit cancellation reset");
        }
        fresh(*w);
        auto noRuntime = program("return true");
        tick(*w, noRuntime, nullptr);
        check(w->events.local[0].pageConditionFault == 1 && w->events.errors == 1, "missing executor");
        fresh(*w);
        auto badCommon = program("return 1");
        lua.prepareEventScripts({}, badCommon);
        tick(*w, {}, &lua, badCommon);
        check(w->events.common[0].pageConditionFault == 1 && w->events.errors == 1,
              "common page fault latch");
        cancelEvents(*w, false);
        tick(*w, {}, &lua, badCommon);
        check(w->events.common[0].pageConditionFault == 1 && w->events.errors == 1,
              "independent common fault survives local map cancellation");
        cancelEvents(*w, true);
        check(w->events.common[0].pageConditionFault == -1, "common restart clears page fault");
        fresh(*w);
        w->level.events[0].erased = true;
        lua.prepareEventScripts({noRuntime}, {});
        Counter erasedCounter;
        stepEvents(*w, noRuntime, {}, false, fixedDt, &erasedCounter);
        check(!erasedCounter.calls && !w->events.errors, "erased owner must not run page Lua");
        // Transactional compile keeps the old cache after syntax failure.
        fresh(*w);
        lua.prepareEventScripts({context}, {});
        rejected = false;
        try {
            lua.prepareEventScripts({program("return (")}, {});
        } catch (const std::exception& e) {
            rejected = std::string(e.what()).find("page 2 page condition") != std::string::npos;
        }
        check(rejected, "compile location");
        tick(*w, context, &lua);
        check(w->events.local[0].page == 1, "compile rollback");
        // More valid page conditions than the shared 32-call budget defer, never fault.
        fresh(*w);
        w->level.events.clear();
        EventMap busy, globals;
        for (int id = 1; id <= 32; ++id) {
            auto e = program("return true", id).events[0];
            busy.events.push_back(e);
            EventPlacement placed;
            placed.id = id;
            placed.region = {160, 592, 32, 48};
            w->level.events.push(placed);
        }
        for (int id = 1; id <= 16; ++id)
            globals.events.push_back(program("return true", id).events[0]);
        lua.prepareEventScripts({busy}, globals);
        for (int i = 0; i < 64; ++i)
            tick(*w, busy, &lua, globals);
        check(!w->events.errors && w->events.pageConditionDeferrals > 0,
              "budget deferral must not latch");
        for (const auto& r : w->events.local)
            check(r.page == 1 && r.pageConditionFault < 0, "local fairness");
        for (const auto& r : w->events.common)
            check(r.page == 1 && r.pageConditionFault < 0, "common fairness");
        // Full window farthest T/R and deterministic continuation at every record frequency.
        auto timed = program("return engine.get_variable(3)%2==0");
        for (auto& p : timed.events[0].pages) {
            p.commands.insert(p.commands.begin(), command(EventOp::AddVariable, 2, 1));
        }
        ScriptRuntime simLua;
        fresh(*w);
        simLua.load(resources, *w);
        Simulation sim(384ull * 1024 * 1024);
        sim.setHooks(&simLua);
        sim.setEvents({timed}, {});
        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w);
                w->rewind.record_hz = hz;
                sim.reset(*w);
                for (int i = 0; i < 420; ++i)
                    sim.tick({});
                check(sim.history().size() >= 6 * hz && !sim.world().events.errors,
                      "page condition history");
                auto oldest = std::make_unique<WorldState>(sim.history().at(0));
                Input input;
                input.fast = fast;
                input.trace = !fast;
                for (int i = 0; i < 500 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(input);
                check(sim.history().size() == 1 &&
                          sim.world().events.variables == oldest->events.variables,
                      "farthest page state");
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
                      "page continuation exact");
            }
        fresh(*w);
        w->rewind.enabled = false;
        sim.reset(*w);
        for (int i = 0; i < 420; ++i)
            sim.tick({});
        sim.tick({.fast = true, .trace = true});
        sim.tick({});
        check(!sim.history().size() && !sim.world().events.errors && sim.world().events.variables[2] > 0,
              "disabled rewind");
        auto report = std::make_unique<SnapshotDiff>();
        auto compareWorld = std::make_unique<WorldState>(sim.world());
        compareWorld->events.local[0].pageConditionFault = 1;
        compareWorld->events.pageConditionDeferrals += 1;
        compareSnapshots(sim.world(), *compareWorld, *report, "events.");
        check(report->differences == 2 && report->matching == 2,
              "new page fields must appear in snapshot diagnostics");
        bool faultPath = false, budgetPath = false;
        for (const auto& entry : report->entries) {
            faultPath |= std::string_view(entry.field.c_str()) == "events.local[0].pageConditionFault";
            budgetPath |= std::string_view(entry.field.c_str()) == "events.pageConditionDeferrals";
        }
        check(faultPath && budgetPath, "new page field diagnostic names");
        std::cout << "Snapshot scalar leaves=" << report->inspected << '\n';
        std::cout << "PASS page conditions: schema 1-8, conjunction/priority/context, faults/compile "
                     "rollback, fair budgets, farthest all-Hz T/R/disabled\n"
                  << "WorldState=" << sizeof(WorldState) << " EventRunner=" << sizeof(EventRunner)
                  << " EventState=" << sizeof(EventState) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
