#include "echo/events.hpp"
#include "echo/simulation.hpp"
#include "echo/editor.hpp"
#include "echo/resources.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace echo;
void check(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
void resetWorld(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.level.dialogues.clear();
    w.level.portals.clear();
    w.level.events.clear();
}
EventCommand cmd(EventOp op, int a = 0, float value = 0, int b = 0) {
    EventCommand c;
    c.op = op;
    c.a = a;
    c.value = value;
    c.b = b;
    return c;
}
void place(WorldState& w, int id) {
    EventPlacement p;
    p.id = id;
    p.region = {w.kino.x, w.kino.y, 32, 48};
    w.level.events.push(p);
}
int main() {
    try {
        auto w = std::make_unique<WorldState>();
        resetWorld(*w);
        EventMap local, common;
        auto chest = eventPreset(1, 1);
        chest.pages[0].commands.erase(
            chest.pages[0].commands.begin()); // no dialogue for deterministic reward test
        local.events.push_back(chest);
        place(*w, 1);
        stepEvents(*w, local, common, true, fixedDt);
        stepEvents(*w, local, common, false, fixedDt);
        check(w->events.variables[0] == 1 && w->events.self[0][0] == 1 && w->events.local[0].page == 1,
              "once chest switches to higher priority page");
        for (int i = 0; i < 30; ++i)
            stepEvents(*w, local, common, i % 2 == 0, fixedDt);
        check(w->events.variables[0] == 1, "opened chest cannot grant twice");
        auto serialized = saveEvents(local);
        auto parsed = loadEvents(serialized);
        check(saveEvents(parsed) == serialized, "event file round trip");
        auto level = saveLevel(w->level);
        auto loaded = loadLevel(level);
        check(loaded.events.count == 1 && loaded.events[0].id == 1, "map event placements round trip");
        bool rejected = false;
        try {
            loadEvents(serialized.substr(0, serialized.size() / 2));
        } catch (...) {
            rejected = true;
        }
        check(rejected, "truncated event definitions rejected");
        EventMap invalid = local;
        invalid.events[0].pages[0].commands = {cmd(EventOp::Else)};
        rejected = false;
        try {
            validateEvents(invalid);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "unbalanced branches rejected");
        LevelEditor editor;
        editor.set(loaded, parsed, common);
        editor.tool = 17;
        editor.paint(320, 592);
        editor.end();
        check(editor.level.events.count == 2 && editor.events.events.size() == 2, "map deploy event");
        editor.undo();
        check(editor.level.events.count == 1 && editor.events.events.size() == 1,
              "event placement and definition undo atomically");
        editor.redo();
        check(editor.level.events.count == 2, "event redo");
        resetWorld(*w);
        local = {};
        common = {};
        auto branch = eventPreset(1, 2);
        branch.pages[0].trigger = EventTrigger::Action;
        branch.pages[0].commands = {
            cmd(EventOp::IfSwitch, 2, 0, 1),   cmd(EventOp::Variable, 1, 11), cmd(EventOp::Else),
            cmd(EventOp::IfVariable, 0, 2, 0), cmd(EventOp::Variable, 1, 22), cmd(EventOp::Else),
            cmd(EventOp::Variable, 1, 33),     cmd(EventOp::EndIf),           cmd(EventOp::EndIf)};
        local.events.push_back(branch);
        place(*w, 1);
        w->events.variables[0] = 3;
        stepEvents(*w, local, common, true, fixedDt);
        check(w->events.variables[1] == 22, "nested false branch executes else");
        stepEvents(*w, local, common, false, fixedDt);
        w->events.switches[2] = 1;
        stepEvents(*w, local, common, true, fixedDt);
        check(w->events.variables[1] == 11, "true branch skips else");
        resetWorld(*w);
        local = {};
        common = {};
        auto timer = eventPreset(1, 2);
        timer.pages[0].commands[0].value = .2f;
        common.events.push_back(timer);
        auto caller = eventPreset(1, 0);
        caller.pages[0].commands = {cmd(EventOp::CallCommon, 2), cmd(EventOp::Switch, 0, 0, 1)};
        local.events.push_back(caller);
        place(*w, 1);
        auto function = eventPreset(2, 0);
        function.pages[0].commands = {cmd(EventOp::Wait, 0, .15f), cmd(EventOp::Variable, 2, 99)};
        common.events.push_back(function);
        Simulation sim(32ull * 1024 * 1024);
        sim.setEvents({local}, common);
        sim.reset(*w);
        Input interact;
        interact.confirm = true;
        sim.tick(interact);
        check(sim.world().events.common[1].running && eventLocksPlayer(sim.world()),
              "common call waits without blocking frame");
        for (int i = 0; i < 20; ++i)
            sim.tick({});
        check(sim.world().events.variables[2] == 99 && sim.world().events.switches[0] &&
                  !eventLocksPlayer(sim.world()),
              "common returns and resumes caller");
        check(sim.world().events.variables[0] > 0, "parallel global timer executes while caller waits");
        auto saved = std::make_unique<WorldState>(sim.world());
        for (int i = 0; i < 36; ++i)
            sim.tick({});
        auto replay = std::make_unique<WorldState>(sim.world());
        sim.restore(*saved);
        sim.history().clear();
        sim.record();
        for (int i = 0; i < 36; ++i)
            sim.tick({});
        check(std::memcmp(&sim.world().events, &replay->events, sizeof(EventState)) == 0,
              "global wait/pc/variables replay exactly");
        resetWorld(*w);
        local = {};
        common = {};
        auto move = eventPreset(1, 0);
        auto instruction = cmd(EventOp::Move, 0, .2f);
        instruction.x = 120;
        move.pages[0].commands = {instruction};
        local.events.push_back(move);
        place(*w, 1);
        stepEvents(*w, local, common, true, fixedDt);
        float start = w->level.events[0].region.x;
        auto halfway = std::make_unique<WorldState>(*w);
        for (int i = 0; i < 15; ++i)
            stepEvents(*w, local, common, false, fixedDt);
        check(std::abs(w->level.events[0].region.x - start - 120) < .001f,
              "event movement uses logical coordinates");
        float x = w->level.events[0].region.x;
        restore(*w, *halfway);
        for (int i = 0; i < 15; ++i)
            stepEvents(*w, local, common, false, fixedDt);
        check(w->level.events[0].region.x == x, "event movement restores timer and velocity");
        resetWorld(*w);
        local = {};
        common = {};
        auto talk = eventPreset(1, 0);
        talk.pages[0].commands[0].text.set("Count \\V[4]");
        w->events.variables[3] = 42;
        local.events.push_back(talk);
        place(*w, 1);
        sim.setEvents({local}, common);
        sim.reset(*w);
        sim.tick(interact);
        check(sim.world().dialogue.active == 8 && sim.world().events.local[0].waitDialogue,
              "event dialogue owns wait state");
        check(std::string(activeDialogue(sim.world()).text.c_str()) == "Count 42",
              "event variable text resolves into snapshot");
        sim.tick({});
        auto dialogState = std::make_unique<WorldState>(sim.world());
        for (int i = 0; i < 30; ++i)
            sim.tick({});
        check(sim.world().dialogue.revealed > 0, "event dialogue progresses");
        sim.restore(*dialogState);
        sim.history().clear();
        sim.record();
        sim.tick(interact);
        sim.tick({});
        sim.tick(interact);
        sim.tick({});
        sim.tick({});
        check(sim.world().dialogue.active < 0 && !eventLocksPlayer(sim.world()),
              "dialogue confirmation resumes chain");
        resetWorld(*w);
        local = {};
        common = {};
        auto warp = eventPreset(1, 3);
        warp.pages[0].commands[0].a = 1;
        local.events.push_back(warp);
        place(*w, 1);
        common.events.push_back(timer);
        std::vector<LevelState> maps{w->level, w->level};
        maps[1].events.clear();
        maps[1].spawn = {640, 592};
        sim.setMaps(maps);
        sim.setEvents({local, EventMap{}}, common);
        sim.reset(*w);
        sim.tick(interact);
        check(sim.world().activeMap == 1 && sim.world().events.common[0].wait > 0,
              "global wait survives map transfer");
        Input trace;
        trace.trace = true;
        sim.tick(trace);
        sim.tick({});
        check(sim.world().activeMap == 0, "T restores event map transfer");
        for (int i = 0; i < 30; ++i)
            sim.tick({});
        check(sim.world().events.variables[0] > 0, "farthest rewind then global parallel continues");
        // An accidental recursion must yield an error, never hang.
        resetWorld(*w);
        local = {};
        common = {};
        auto recursive = eventPreset(1, 0);
        recursive.pages[0].commands = {cmd(EventOp::CallCommon, 1)};
        common.events.push_back(recursive);
        auto entry = eventPreset(1, 0);
        entry.pages[0].commands = {cmd(EventOp::CallCommon, 1)};
        local.events.push_back(entry);
        place(*w, 1);
        for (int i = 0; i < 10; ++i)
            stepEvents(*w, local, common, i == 0, fixedDt);
        check(w->events.errors > 0 && !eventLocksPlayer(*w),
              "common recursion rejected and caller recovers");
        resetWorld(*w);
        local = {};
        common = {};
        auto waiting = eventPreset(1, 0);
        waiting.pages[0].commands = {cmd(EventOp::Wait, 0, 1)};
        EventPage concurrent;
        concurrent.trigger = EventTrigger::Parallel;
        concurrent.switchId = 3;
        concurrent.commands = {cmd(EventOp::Wait, 0, 1)};
        waiting.pages.push_back(concurrent);
        common.events.push_back(waiting);
        auto callerPage = eventPreset(1, 0);
        callerPage.pages[0].commands = {cmd(EventOp::CallCommon, 1), cmd(EventOp::Switch, 0, 0, 1)};
        local.events.push_back(callerPage);
        place(*w, 1);
        stepEvents(*w, local, common, true, fixedDt);
        w->events.switches[3] = 1;
        for (int i = 0; i < 5; ++i)
            stepEvents(*w, local, common, false, fixedDt);
        check(w->events.switches[0] && !eventLocksPlayer(*w),
              "cancelled common call returns even when replacement page runs in parallel");
        // Dense parallel programs must not starve later common interpreters.
        resetWorld(*w);
        local = {};
        common = {};
        for (int id = 1; id <= 32; ++id) {
            auto e = eventPreset(id, 2);
            e.pages[0].commands.clear();
            for (int c = 0; c < 32; ++c)
                e.pages[0].commands.push_back(cmd(EventOp::AddVariable, id - 1, 1));
            local.events.push_back(e);
            place(*w, id);
        }
        for (int id = 1; id <= 16; ++id) {
            auto e = eventPreset(id, 2);
            e.pages[0].commands.clear();
            for (int c = 0; c < 32; ++c)
                e.pages[0].commands.push_back(cmd(EventOp::AddVariable, 31 + id, 1));
            common.events.push_back(e);
        }
        for (int i = 0; i < 120; ++i)
            stepEvents(*w, local, common, false, fixedDt);
        for (int i = 0; i < 48; ++i)
            check(w->events.variables[i] > 0, "round robin gives every busy map/global event progress");
        check(w->events.budgetYields > 0, "event instruction budget yields instead of blocking");
        std::cout << "PASS map/global events, pages, switches, variables, branches, parallel waits, "
                     "calls, dialogue, movement, transfer, rewind, undo, bounds\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
