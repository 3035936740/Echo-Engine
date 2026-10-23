#include "echo/event_edit.hpp"
#include "echo/simulation.hpp"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace echo;
void check(bool v, const char* text) {
    if (!v)
        throw std::runtime_error(text);
}
template <class F> void rejects(F f, const char* text) {
    bool bad = false;
    try {
        f();
    } catch (...) {
        bad = true;
    }
    check(bad, text);
}
EventCommand op(EventOp type, float value = 0) {
    EventCommand c;
    c.op = type;
    c.value = value;
    return c;
}
EventMap mapWith(const std::vector<EventCommand>& commands) {
    EventDefinition e;
    e.id = 1;
    e.name.set("Authoring fixture");
    e.pages.emplace_back();
    e.pages[0].trigger = EventTrigger::Parallel;
    e.pages[0].selfSwitch = 0;
    e.pages[0].selfValue = false;
    e.pages[0].commands = commands;
    EventMap m;
    m.events.push_back(e);
    validateEvents(m);
    return m;
}
void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.kino.invulnTimer = 10000;
    w.level.features.clear();
    w.level.dialogues.clear();
    w.level.portals.clear();
    w.level.events.clear();
    EventPlacement e;
    e.id = 1;
    e.region = {0, 0, 32, 32};
    w.level.events.push(e);
}
int main() {
    try {
        int all = 0;
        for (int i = 0; i <= int(EventOp::IfQuestProgress); ++i) {
            auto type = EventOp(i);
            check(!eventCommandName(type).empty(), "every opcode has an authoring name");
            check(eventCommandMatches(type, EventCategory::All, L"", false), "all commands accessible");
            ++all;
        }
        check(all == 35, "catalog matches complete event schema");
        check(eventCommandMatches(EventOp::IfScript, EventCategory::Advanced, L" LuA 条件 ", false),
              "Chinese/English multi-token search");
        check(eventCommandMatches(EventOp::IfScript, EventCategory::Flow, L"lua", false),
              "Lua condition also a flow command");
        check(eventCommandMatches(EventOp::FadeMusic, EventCategory::Audio, L"BGM 渐弱", false),
              "search uses useful aliases");
        check(!eventCommandMatches(EventOp::FadeMusic, EventCategory::Data, L"", false),
              "categories filter unrelated commands");
        check(!eventCommandMatches(EventOp::Move, EventCategory::World, L"", true),
              "common program excludes local movement");
        rejects(
            [] {
                eventCommandMatches(EventOp::Wait, EventCategory::All, std::wstring(65, L'x'), false);
            },
            "search budget");
        auto outer = op(EventOp::IfSwitch);
        outer.a = 0;
        outer.b = 1;
        auto loop = op(EventOp::Loop);
        loop.a = 3;
        auto add = op(EventOp::AddVariable, 1);
        add.a = 0;
        auto wait = op(EventOp::Wait, .03f);
        std::vector<EventCommand> nested{outer,
                                         loop,
                                         add,
                                         wait,
                                         op(EventOp::EndLoop),
                                         op(EventOp::Else),
                                         op(EventOp::Text),
                                         op(EventOp::EndIf),
                                         op(EventOp::Exit)};
        nested[6].text.set("fallback");
        mapWith(nested);
        auto original = saveEvents(mapWith(nested));
        std::bitset<maxEventCommands> folded;
        folded[0] = true;
        auto rows = eventCommandRows(nested, folded);
        check(rows.size() == 2 && rows[0].source == 0 && rows[0].hidden == 7 && rows[1].source == 8,
              "fold keeps original source identities and entire hidden block");
        folded.reset();
        folded[1] = true;
        rows = eventCommandRows(nested, folded);
        check(rows[1].depth == 1 && rows[1].hidden == 3 && rows[2].source == 5 && rows[2].depth == 0,
              "nested fold indentation and else boundary");
        check(original == saveEvents(mapWith(nested)), "display fold does not alter program");
        check(eventBlockOwner(nested, 4) == 1 && eventBlockOwner(nested, 5) == 0 &&
                  eventBlockOwner(nested, 8) == -1,
              "scope ownership includes own terminators only");
        std::vector<int> selection{5};
        auto block = copyEventCommands(nested, selection);
        check(block.size() == 8 && block.front().op == EventOp::IfSwitch &&
                  block.back().op == EventOp::EndIf,
              "copy else closes its enclosing condition");
        check(eraseEventCommands(nested, selection, false).size() == 1,
              "deleting an end marker removes the whole block");
        selection = {2, 6};
        auto fragments = copyEventCommands(nested, selection);
        check(fragments.size() == 2 && fragments[0].op == EventOp::AddVariable &&
                  fragments[1].op == EventOp::Text,
              "disjoint ordinary copy preserves source order");
        auto reduced = eraseEventCommands(nested, selection, false);
        check(reduced.size() == 7 && reduced[0].op == EventOp::IfSwitch,
              "disjoint delete leaves valid skeleton");
        rejects([&] { moveEventCommands(nested, selection, true, false); },
                "move rejects disjoint scopes");
        check(original == saveEvents(mapWith(nested)), "rejected move is atomic");
        std::vector<EventCommand> plain{wait, add, add, op(EventOp::Exit)};
        selection = {1, 2};
        int first = moveEventCommands(plain, selection, true, false);
        check(first == 0 && plain[2].op == EventOp::Wait, "contiguous group moves together");
        selection = {0, 1};
        first = moveEventCommands(plain, selection, false, false);
        check(first == 1 && plain[0].op == EventOp::Wait, "contiguous group moves back");
        auto breaking = nested;
        breaking.insert(breaking.begin() + 3, op(EventOp::BreakLoop));
        selection = {1};
        auto copiedLoop = copyEventCommands(breaking, selection);
        check(copiedLoop.size() == 5 && copiedLoop.front().op == EventOp::Loop,
              "copy loop preserves contained break");
        selection = {3};
        check(copyEventCommands(breaking, selection).size() == 5,
              "copy standalone break expands nearest loop");
        std::vector<EventCommand> inner{loop,
                                        outer,
                                        op(EventOp::BreakLoop),
                                        op(EventOp::Else),
                                        wait,
                                        op(EventOp::EndIf),
                                        op(EventOp::EndLoop),
                                        op(EventOp::Exit)};
        selection = {1};
        check(copyEventCommands(inner, selection).size() == 7,
              "copy conditional containing break includes enclosing loop");
        auto choice = op(EventOp::Choices);
        choice.a = 0;
        ChoiceDefinition option;
        option.label.set("Accept");
        option.condition = ChoiceCondition::Lua;
        option.script.set("return engine.get_variable(1)==0");
        choice.choices.push_back(option);
        std::vector<EventCommand> rich{choice, op(EventOp::Script)};
        rich[1].text.set("engine.state[1]=engine.state[1]+1");
        selection = {0, 1};
        auto richCopy = copyEventCommands(rich, selection);
        rich[0].choices[0].label.set("changed");
        rich[1].text.set("error('changed')");
        check(std::string(richCopy[0].choices[0].label.c_str()) == "Accept" &&
                  std::string(richCopy[1].text.c_str()).starts_with("engine.state"),
              "clipboard deep copies choices and advanced scripts");
        auto destination = std::vector<EventCommand>{op(EventOp::Exit)};
        check(pasteEventCommands(destination, richCopy, -1, false).size() == 3,
              "paste keeps advanced immutable command data");
        rejects(
            [&] {
                std::vector<EventCommand> move{op(EventOp::Move, 1)};
                pasteEventCommands(destination, move, -1, true);
            },
            "map-only command cannot paste into common program");
        auto full = std::vector<EventCommand>(32, wait);
        rejects([&] { pasteEventCommands(full, std::vector<EventCommand>{add}, 31, false); },
                "full page rejects paste");
        check(full.size() == 32, "failed paste leaves source unchanged");
        rejects(
            [&] {
                pasteEventCommands(destination, std::vector<EventCommand>{op(EventOp::EndIf)}, -1,
                                   false);
            },
            "orphan structural clipboard rejects");
        // Execute the authored copy, not merely its vector shape.
        auto stop = op(EventOp::SelfSwitch);
        stop.b = 1;
        std::vector<EventCommand> program{
            loop, add, wait, op(EventOp::EndLoop), stop, op(EventOp::Exit)};
        selection = {0};
        auto repeat = copyEventCommands(program, selection);
        program = pasteEventCommands(program, repeat, 3, false);
        auto map = mapWith(program);
        auto w = std::make_unique<WorldState>();
        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w);
                w->rewind.record_hz = hz;
                Simulation sim(512ull * 1024 * 1024);
                sim.setEvents({map}, {});
                sim.reset(*w);
                for (int i = 0; i < 360; ++i)
                    sim.tick({});
                check(sim.world().events.variables[0] == 6 && sim.history().seconds() > 5.9f,
                      "pasted program executes both loops with six seconds of history");
                Input input;
                if (fast)
                    input.fast = true;
                else
                    input.trace = true;
                for (int i = 0; i < 450 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(input);
                check(sim.world().clock < .001 && sim.history().size() == 1 &&
                          sim.world().events.variables[0] == 0,
                      "farthest T/R restores copied program execution state");
                sim.tick({});
                for (int i = 0; i < 60; ++i)
                    sim.tick({});
                check(sim.world().events.variables[0] == 6,
                      "branch resumes copied loops without duplicate progress");
            }
        fresh(*w);
        w->rewind.enabled = false;
        Simulation off;
        off.setEvents({map}, {});
        off.reset(*w);
        for (int i = 0; i < 90; ++i)
            off.tick({});
        check(off.history().capacity() == 0 && off.world().events.variables[0] == 6,
              "authored program also works with history disabled");
        std::cout << "PASS categorized search / structural multi-selection / deep clipboard / scopes / "
                     "capacity / folded source IDs / all-Hz full-six-second T-R continued / disabled\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
