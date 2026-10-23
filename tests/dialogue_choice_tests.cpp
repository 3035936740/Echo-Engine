#include "echo/script.hpp"
#include "echo/input_bindings.hpp"
#include "echo/snapshot_diff.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
using namespace echo;
static bool countAllocations{};
static std::size_t allocations{};
void* operator new(std::size_t n) {
    if (countAllocations)
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
static void check(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
static void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.script = {};
    w.kino.invulnTimer = 10000;
    w.jump.gravity = 0;
    w.level.dialogues.clear();
    w.level.portals.clear();
    w.level.events.clear();
    EventPlacement event;
    event.region = {w.kino.x, w.kino.y, 32, 48};
    w.level.events.push(event);
}
static EventCommand cmd(EventOp op, int a = 0, float value = 0) {
    EventCommand c;
    c.op = op;
    c.a = a;
    c.value = value;
    return c;
}
static EventMap choices(bool lua = false) {
    EventPage p;
    auto c = cmd(EventOp::Choices);
    c.text.set("Choose");
    c.speaker.set("NPC");
    ChoiceDefinition a, locked, third, hidden;
    a.label.set("First");
    locked.label.set("Locked");
    third.label.set("Third");
    hidden.label.set("Hidden");
    locked.condition = ChoiceCondition::Switch;
    locked.id = 1;
    third.condition = ChoiceCondition::Variable;
    third.id = 1;
    third.threshold = 3;
    hidden.condition = ChoiceCondition::Switch;
    hidden.id = 2;
    hidden.hideUnavailable = true;
    if (lua) {
        third.condition = ChoiceCondition::Lua;
        third.script.set("return engine.get_variable(2) >= 3");
    }
    c.choices = {a, locked, third, hidden};
    auto branch = cmd(EventOp::IfVariable, 0, 3);
    branch.b = 1;
    p.commands = {c,
                  branch,
                  cmd(EventOp::Variable, 2, 77),
                  cmd(EventOp::Else),
                  cmd(EventOp::Variable, 2, 12),
                  cmd(EventOp::EndIf)};
    EventDefinition e;
    e.name.set("Choice");
    e.pages = {p};
    return EventMap{{e}};
}
static void press(Simulation& sim, Input input) {
    sim.tick({});
    sim.tick(input);
}
static void open(Simulation& sim) {
    Input confirm;
    confirm.confirm = true;
    sim.tick(confirm);
    sim.tick({});
    check(sim.world().dialogue.choosing && sim.world().dialogue.options.count == 3,
          "choice opens; hidden omitted, unavailable grey remains");
}
static void chooseThird(Simulation& sim) {
    Input confirm;
    confirm.confirm = true;
    press(sim, confirm); // finish prompt typewriter first
    check(sim.world().dialogue.choosing, "first confirmation reveals prompt");
    Input right;
    right.move = 1;
    press(sim, right);
    check(sim.world().dialogue.selected == 2, "navigation skips disabled row");
    for (int i = 0; i < 20; ++i)
        sim.tick(right);
    check(sim.world().dialogue.selected == 2, "held navigation does not repeat");
    press(sim, confirm);
    sim.tick({});
    check(sim.world().events.variables[0] == 3 && sim.world().events.variables[2] == 77,
          "original authored choice index drives ordinary branch");
}
int main(int argc, char** argv) {
    try {
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        auto map = choices();
        validateEvents(map);
        auto text = saveEvents(map);
        check(text.starts_with("ECHO_EVENTS_10") && saveEvents(loadEvents(text)) == text,
              "choice conditions and labels round trip");
        auto older = text;
        older.replace(0, older.find('\n'), "ECHO_EVENTS_8");
        bool rejected = false;
        try {
            loadEvents(older);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "v8 cannot accept choices");
        for (const char* replacement : {"CHOICES 0", "CHOICES 7", "BROKEN 4"}) {
            auto bad = text;
            auto at = bad.find("CHOICES 4");
            bad.replace(at, 9, replacement);
            rejected = false;
            try {
                loadEvents(bad);
            } catch (...) {
                rejected = true;
            }
            check(rejected, "bad counts/markers rejected");
        }
        auto empty = map;
        empty.events[0].pages[0].commands[0].choices[0].label.set("");
        rejected = false;
        try {
            validateEvents(empty);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "empty labels rejected");
        auto preset = eventPreset(1, 8);
        validateEvents(EventMap{{preset}});
        check(preset.pages[0].commands[0].op == EventOp::Choices, "no-script choice branch preset");

        for (int hz : {30, 60, 120}) {
            fresh(*w);
            w->rewind.record_hz = hz;
            w->events.variables[1] = 3;
            Simulation sim(384ull * 1024 * 1024);
            sim.setEvents({map}, {});
            sim.reset(*w);
            open(sim);
            auto pos = Vec2{sim.world().kino.x, sim.world().kino.y};
            chooseThird(sim);
            check(sim.world().kino.x == pos.x && sim.world().kino.y == pos.y,
                  "modal navigation never moves player");
            Input trace;
            trace.trace = true;
            for (int i = 0; i < 20 && (i == 0 || sim.world().state == TimeState::Trace); ++i)
                sim.tick(trace);
            sim.tick({});
            check(sim.world().events.variables[0] == 0,
                  "T restores choice result and future branch writes");
        }
        fresh(*w);
        w->events.variables[1] = 3;
        Simulation initial(32ull * 1024 * 1024);
        initial.setEvents({map}, {});
        initial.reset(*w);
        open(initial);
        *w = initial.world();
        w->dialogue.revealed = 6;
        w->clock = 0;
        w->tick = 0;
        w->recordAccumulator = 0;
        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                w->rewind.record_hz = hz;
                Simulation far(384ull * 1024 * 1024);
                far.setEvents({map}, {});
                far.reset(*w);
                countAllocations = true;
                for (int i = 0; i < 360; ++i)
                    far.tick({});
                countAllocations = false;
                check(allocations == 0, "steady modal simulation allocates no C++ heap");
                Input trace;
                trace.trace = !fast;
                trace.fast = fast;
                for (int i = 0; i < 365 && (i == 0 || far.world().state == TimeState::Trace ||
                                            far.world().state == TimeState::Fast);
                     ++i)
                    far.tick(trace);
                check(far.world().clock < .001 && far.world().dialogue.choosing &&
                          far.world().dialogue.selected == 0 && far.history().size() == 1,
                      "full six-second oldest snapshot restores modal state and truncates");
                far.tick({});
                Input right;
                right.move = 1;
                press(far, right);
                Input confirm;
                confirm.confirm = true;
                press(far, confirm);
                far.tick({});
                check(far.world().events.variables[0] == 3 && far.world().events.variables[2] == 77,
                      "choice and branch remain usable after oldest rewind");
            }
        fresh(*w);
        w->rewind.enabled = false;
        w->events.variables[1] = 3;
        Simulation disabled;
        disabled.setEvents({map}, {});
        disabled.reset(*w);
        open(disabled);
        chooseThird(disabled);
        check(disabled.history().capacity() == 0, "choices work with rewind disabled");
        fresh(*w);
        auto noOptions = choices();
        noOptions.events[0].pages[0].commands[0].choices.erase(
            noOptions.events[0].pages[0].commands[0].choices.begin());
        Simulation noDeadlock;
        noDeadlock.setEvents({noOptions}, {});
        noDeadlock.reset(*w);
        Input confirm;
        confirm.confirm = true;
        noDeadlock.tick(confirm);
        check(!noDeadlock.world().dialogue.choosing && noDeadlock.world().events.errors == 1 &&
                  noDeadlock.world().events.local[0].scriptFault &&
                  noDeadlock.world().events.foreground == -1,
              "all unavailable faults locally and releases ownership");

        fresh(*w);
        w->events.variables[1] = 3;
        Simulation frozen;
        frozen.setEvents({map}, {});
        frozen.reset(*w);
        open(frozen);
        frozen.world().events.switches[1] = 1;
        check(!frozen.world().dialogue.options[1].enabled,
              "availability remains snapshot truth until close");
        auto cancelled = map;
        EventPage higher;
        higher.switchId = 127;
        cancelled.events[0].pages.push_back(higher);
        frozen.setEvents({cancelled}, {});
        frozen.world().events.switches[127] = 1;
        frozen.tick({});
        check(frozen.world().dialogue.active < 0 && frozen.world().events.dialogueOwner < 0,
              "page cancellation releases modal owner");
        if (argc > 1) {
            Resources resources;
            resources.root = std::filesystem::path(argv[1]) / "choice-scripts";
            std::filesystem::create_directories(resources.root);
            writeTextAtomically(resources.root / "project.echo", "title=Choices\n");
            fresh(*w);
            w->events.variables[1] = 3;
            ScriptRuntime runtime;
            runtime.load(resources, *w);
            auto advanced = choices(true);
            Simulation scripted(32ull * 1024 * 1024);
            scripted.setHooks(&runtime);
            scripted.setEvents({advanced}, {});
            scripted.reset(*w);
            open(scripted);
            chooseThird(scripted);
            auto bad = advanced;
            bad.events[0].pages[0].commands[0].choices[2].script.set(
                "engine.set_variable(1, 999); return true");
            fresh(*w);
            w->events.variables[1] = 3;
            Simulation rollback;
            rollback.setHooks(&runtime);
            rollback.setEvents({bad}, {});
            rollback.reset(*w);
            rollback.tick(confirm);
            check(rollback.world().events.variables[0] == 0 && rollback.world().events.errors == 1 &&
                      rollback.world().dialogue.active < 0,
                  "read-only Lua rejects mutation without opening modal");
            bad.events[0].pages[0].commands[0].choices[2].script.set("return (");
            rejected = false;
            try {
                runtime.validateEventScripts(bad, false, 0);
            } catch (const std::exception& e) {
                rejected = std::string(e.what()).find("choice 3") != std::string::npos;
            }
            check(rejected, "Lua compile errors locate exact choice");
        }
        auto changed = std::make_unique<WorldState>(*w);
        changed->dialogue.selected = 5;
        changed->dialogue.options.values[5].enabled = true;
        auto diff = std::make_unique<SnapshotDiff>();
        compareSnapshots(*w, *changed, *diff);
        bool selection = false, enabled = false;
        for (const auto& e : diff->entries) {
            selection |= std::string_view(e.field.c_str()) == "dialogue.selected";
            enabled |= std::string_view(e.field.c_str()) == "dialogue.options.values[5].enabled";
        }
        check(selection && enabled, "snapshot diagnostics include inactive choice slots");
        std::cout << "PASS dialogue choices; 30/60/120 Hz oldest rewind; disabled; Lua; zero steady "
                     "allocations\n"
                  << "WorldState bytes " << sizeof(WorldState) << "; snapshot scalars "
                  << diff->inspected << '\n';
        return 0;
    } catch (const std::exception& e) {
        countAllocations = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
