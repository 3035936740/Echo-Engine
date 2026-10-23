#include "echo/database.hpp"
#include "echo/events.hpp"
#include "echo/recovery.hpp"
#include "echo/script.hpp"
#include <cstring>
#include <cstdlib>
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
void check(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f, const char* m) {
    bool bad = false;
    try {
        f();
    } catch (...) {
        bad = true;
    }
    check(bad, m);
}
Database definitions() {
    Database db;
    ItemDefinition key;
    key.id = allocateDataId(db, false);
    key.name.set("Door key");
    key.limit = 1;
    db.items.push_back(key);
    ItemDefinition coin;
    coin.id = allocateDataId(db, false);
    coin.name.set("Coin");
    coin.limit = 99;
    db.items.push_back(coin);
    QuestDefinition q;
    q.id = allocateDataId(db, true);
    q.name.set("Find three echoes");
    q.target = 3;
    db.quests.push_back(q);
    return db;
}
void fresh(WorldState& w, const Database& db) {
    w = makeWorld();
    w.aster.enabled = false;
    w.script = {};
    w.objects = {};
    w.kino.invulnTimer = 10000;
    w.level.events.clear();
    w.level.dialogues.clear();
    w.level.portals.clear();
    w.level.features.clear();
    w.level.platforms.clear();
    applyDatabaseDefaults(w, db);
}
EventCommand cmd(EventOp op, int value = 0, std::uint64_t id = 0, int flag = 0) {
    EventCommand c;
    c.op = op;
    c.value = float(value);
    c.reference = id;
    c.b = flag;
    return c;
}
EventMap fixture() {
    EventPage p;
    p.trigger = EventTrigger::Autorun;
    p.selfSwitch = 0;
    p.selfValue = false;
    p.commands = {cmd(EventOp::ChangeItem, 1, 1),
                  cmd(EventOp::QuestStatus, 0, 1, 1),
                  cmd(EventOp::Loop, 0),
                  cmd(EventOp::ChangeItem, 1, 2),
                  cmd(EventOp::QuestProgress, 1, 1),
                  cmd(EventOp::Wait),
                  cmd(EventOp::EndLoop),
                  cmd(EventOp::IfItem, 1, 1),
                  cmd(EventOp::IfQuestProgress, 3, 1),
                  cmd(EventOp::IfQuestStatus, 0, 1, 2),
                  cmd(EventOp::AddVariable, 1),
                  cmd(EventOp::EndIf),
                  cmd(EventOp::EndIf),
                  cmd(EventOp::Else),
                  cmd(EventOp::Variable, -100),
                  cmd(EventOp::EndIf),
                  cmd(EventOp::SelfSwitch, 0, 0, 1)};
    p.commands[2].a = 3;
    p.commands[5].value = .05f;
    p.commands[10].a = 2;
    p.commands[14].a = 2;
    EventDefinition d;
    d.id = 1;
    d.name.set("inventory-quest");
    d.pages = {p};
    return EventMap{{d}};
}
void placed(WorldState& w) {
    EventPlacement p;
    p.id = 1;
    p.region = {900, 592, 32, 48};
    w.level.events.push(p);
}
EventMap luaMap(std::string source, bool condition = false) {
    EventCommand c;
    c.op = condition ? EventOp::IfScript : EventOp::Script;
    c.text.set(source);
    EventPage p;
    p.commands = {c};
    if (condition)
        p.commands.push_back(cmd(EventOp::EndIf));
    EventDefinition d;
    d.id = 1;
    d.name.set("lua");
    d.pages = {p};
    return EventMap{{d}};
}
int main(int argc, char** argv) {
    try {
        auto db = std::make_unique<Database>(definitions());
        auto w = std::make_unique<WorldState>();
        fresh(*w, *db);
        auto text = saveDatabase(*db);
        check(text.starts_with("ECHO_DATABASE_2") && saveDatabase(loadDatabase(text)) == text,
              "database v2 roundtrip");
        auto legacy = text.substr(0, text.find("ITEMS"));
        legacy.replace(0, legacy.find('\n'), "ECHO_DATABASE_1");
        check(loadDatabase(legacy).items.empty() && loadDatabase(legacy).quests.empty(),
              "v1 migration leaves new stores empty");
        auto largest = std::make_unique<Database>();
        auto fill = [](auto& e) {
            e.name.set(std::string(63, '\\'));
            e.note.set(std::string(255, '\\'));
        };
        for (auto& e : largest->switches)
            fill(e);
        for (auto& e : largest->variables)
            fill(e);
        for (int i = 0; i < 64; ++i) {
            ItemDefinition item;
            item.id = allocateDataId(*largest, false);
            fill(item);
            largest->items.push_back(item);
            QuestDefinition task;
            task.id = allocateDataId(*largest, true);
            fill(task);
            largest->quests.push_back(task);
        }
        auto maximum = saveDatabase(*largest);
        check(maximum.size() > 128 * 1024 && maximum.size() <= 256 * 1024 &&
                  saveDatabase(loadDatabase(maximum)) == maximum,
              "full-capacity escaped definitions obey 256 KiB budget");
        largest->items.push_back(largest->items.back());
        rejects([&] { saveDatabase(*largest); }, "65th item definition rejected");
        auto duplicate = std::make_unique<Database>(*db);
        duplicate->items.push_back(db->items[0]);
        rejects([&] { saveDatabase(*duplicate); }, "duplicate item identity rejected");
        rejects([&] { loadDatabase(text + "junk"); }, "database trailing data rejected");
        auto next = db->nextItemId;
        db->items.erase(db->items.begin() + 1);
        *db = loadDatabase(saveDatabase(*db));
        check(allocateDataId(*db, false) == next, "deleted identity not reused after save");
        *db = definitions();
        db->items[0].name.set("Renamed key");
        check(progressionLabel(*db, false, 1).find("Renamed") != std::string::npos &&
                  findItem(*w, 1)->count == 0,
              "renaming keeps stable runtime identity");
        check(changeItem(*w, 1, 1) == ProgressResult::Success && findItem(*w, 1)->count == 1,
              "give unique key");
        check(changeItem(*w, 1, 1) == ProgressResult::Full && findItem(*w, 1)->count == 1,
              "overflow is atomic rejection");
        check(changeItem(*w, 1, -2) == ProgressResult::Insufficient && findItem(*w, 1)->count == 1,
              "insufficient removal preserves quantity");
        check(changeItem(*w, 999, 1) == ProgressResult::Missing && w->progression.rejected == 3,
              "unknown ID returns visible failure");
        check(changeItem(*w, 1, -1) == ProgressResult::Success, "remove key");
        check(addQuestProgress(*w, 1, 1) == ProgressResult::InactiveQuest,
              "inactive quest cannot gain progress");
        check(setQuestStatus(*w, 1, QuestStatus::Active) == ProgressResult::Success, "accept quest");
        addQuestProgress(*w, 1, 2);
        setQuestStatus(*w, 1, QuestStatus::Active);
        check(findQuest(*w, 1)->progress == 2, "accepting active quest is idempotent");
        addQuestProgress(*w, 1, 1000000);
        check(findQuest(*w, 1)->status == QuestStatus::Completed && findQuest(*w, 1)->progress == 3,
              "target clamp and auto completion");
        setQuestStatus(*w, 1, QuestStatus::Failed);
        setQuestStatus(*w, 1, QuestStatus::Active);
        check(findQuest(*w, 1)->progress == 0, "reaccept resets old progress");
        addQuestProgress(*w, 1, -1000000);
        check(findQuest(*w, 1)->progress == 0, "negative progress clamps to zero");
        setQuestStatus(*w, 1, QuestStatus::Completed);
        check(findQuest(*w, 1)->progress == 3, "explicit completion fills target");
        setQuestStatus(*w, 1, QuestStatus::Inactive);
        check(findQuest(*w, 1)->progress == 0, "reset clears progress");
        db->quests[0].autoComplete = false;
        fresh(*w, *db);
        setQuestStatus(*w, 1, QuestStatus::Active);
        addQuestProgress(*w, 1, 3);
        check(findQuest(*w, 1)->status == QuestStatus::Active, "manual completion option");
        db->quests[0].autoComplete = true;
        auto map = fixture();
        validateEvents(map);
        auto events = saveEvents(map);
        check(events.starts_with("ECHO_EVENTS_10") && saveEvents(loadEvents(events)) == events,
              "new commands v10 roundtrip");
        auto v9 = events;
        v9.replace(0, v9.find('\n'), "ECHO_EVENTS_9");
        rejects([&] { loadEvents(v9); }, "v9 cannot silently accept new opcodes");
        auto oldMap = luaMap("return true", true);
        auto oldEvents = saveEvents(oldMap);
        oldEvents.replace(0, oldEvents.find('\n'), "ECHO_EVENTS_9");
        check(loadEvents(oldEvents).events.size() == 1, "v9 existing command migration");
        auto invalid = map;
        invalid.events[0].pages[0].commands[0].value = .5f;
        rejects([&] { validateEvents(invalid); }, "fractional quantity rejected");
        fresh(*w, *db);
        placed(*w);
        for (int i = 0; i < 120; ++i)
            stepEvents(*w, map, {}, false, fixedDt);
        check(findItem(*w, 1)->count == 1 && findItem(*w, 2)->count == 3 &&
                  findQuest(*w, 1)->status == QuestStatus::Completed && w->events.variables[2] == 1,
              "no-script nested item/quest conditions execute in loops");
        auto failure = luaMap("");
        failure.events[0].pages[0].commands = {cmd(EventOp::ChangeItem, 1, 1)};
        failure.events[0].pages[0].trigger = EventTrigger::Parallel;
        failure.events[0].pages[0].commands[0].c = 1;
        fresh(*w, *db);
        changeItem(*w, 1, 1);
        placed(*w);
        stepEvents(*w, failure, {}, false, fixedDt);
        check(w->events.variables[0] == int(ProgressResult::Full) && findItem(*w, 1)->count == 1,
              "failure code stored for normal event branch");
        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w, *db);
                placed(*w);
                w->rewind.record_hz = hz;
                Simulation sim(384ull * 1024 * 1024);
                sim.setEvents({map}, {});
                sim.reset(*w);
                allocations = 0;
                measuring = true;
                for (int i = 0; i < 360; ++i)
                    sim.tick({});
                measuring = false;
                check(allocations == 0, "inventory/quests and full records allocate no C++ heap");
                check(findQuest(sim.world(), 1)->status == QuestStatus::Completed,
                      "quest reaches completion before rewind");
                Input in;
                in.fast = fast;
                in.trace = !fast;
                for (int i = 0; i < 370 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(in);
                check(sim.world().clock < .001 && sim.history().size() == 1 &&
                          findItem(sim.world(), 1)->count == 0 &&
                          findQuest(sim.world(), 1)->status == QuestStatus::Inactive &&
                          sim.world().progression.rejected == 0,
                      "oldest six-second T/R restores quantities, task state and diagnostics");
                for (int i = 0; i < 120; ++i)
                    sim.tick({});
                check(findItem(sim.world(), 2)->count == 3 &&
                          findQuest(sim.world(), 1)->status == QuestStatus::Completed,
                      "new branch continues inventory and quest event after truncation");
            }
        fresh(*w, *db);
        placed(*w);
        w->rewind.enabled = false;
        Simulation off;
        off.setEvents({map}, {});
        off.reset(*w);
        for (int i = 0; i < 120; ++i)
            off.tick({});
        check(off.history().capacity() == 0 &&
                  findQuest(off.world(), 1)->status == QuestStatus::Completed,
              "history disabled still completes quest");

        fresh(*w, *db);
        auto a = std::make_unique<LevelState>(w->level), b = std::make_unique<LevelState>(w->level);
        w->mapTransition.kind = TransitionKind::None;
        Simulation across;
        across.setMaps({*a, *b});
        across.reset(*w);
        changeItem(across.world(), 1, 1);
        setQuestStatus(across.world(), 1, QuestStatus::Active);
        addQuestProgress(across.world(), 1, 2);
        across.world().events.transferMap = 1;
        across.world().events.transferSpawn = true;
        across.tick({});
        check(across.world().activeMap == 1 && findItem(across.world(), 1)->count == 1 &&
                  findQuest(across.world(), 1)->progress == 2,
              "actual map transfer preserves global progression");

        if (argc > 1) {
            Resources r;
            r.root = std::filesystem::path(argv[1]) / "progression-api";
            std::filesystem::create_directories(r.root / "levels");
            writeTextAtomically(
                r.root / "project.echo",
                "title=progression\nlevel=levels/start.echo\nmaps=levels/start.echo\nscripts=\n");
            writeTextAtomically(r.root / "database.echo", saveDatabase(*db));
            fresh(*w, *db);
            placed(*w);
            writeTextAtomically(r.root / "levels/start.echo", saveLevel(w->level));
            auto badMap = fixture();
            badMap.events[0].pages[0].commands[0].reference = 999;
            writeTextAtomically(r.root / "levels/start.echo.events", saveEvents(badMap));
            auto issues = inspectProject(r);
            check(std::any_of(issues.begin(), issues.end(),
                              [](const auto& i) {
                                  return i.error &&
                                         i.message.find("Missing item/quest") != std::string::npos;
                              }),
                  "project checker reports missing formal database references");
            ScriptRuntime lua;
            lua.load(r, *w);
            auto api = luaMap(
                "engine.change_item(1,1);engine.set_quest_status(1,1);engine.add_quest_progress(1,2)");
            lua.prepareEventScripts({api}, {});
            lua.beginEventStep(*w);
            check(lua.eventScript(*w, {0, 1, 0, 0, false}) && findItem(*w, 1)->count == 1 &&
                      findQuest(*w, 1)->progress == 2,
                  "Lua mutates snapshot-owned state");
            api = luaMap("local i=engine.get_item(1);i.count=9;local q=engine.get_quest(1);q.progress=9;"
                         "return engine.get_item_count(1)==1 and engine.get_quest(1).progress==2 and "
                         "engine.get_item(999)==nil",
                         true);
            lua.prepareEventScripts({api}, {});
            lua.beginEventStep(*w);
            auto before = std::make_unique<WorldState>(*w);
            bool result = false;
            check(lua.eventCondition(*w, {0, 1, 0, 0, false}, result) && result &&
                      std::memcmp(before.get(), w.get(), sizeof(WorldState)) == 0,
                  "Lua read-only copies cannot mutate state");
            api = luaMap("engine.change_item(1,-1);engine.add_quest_progress(1,1);error('rollback')");
            lua.prepareEventScripts({api}, {});
            lua.beginEventStep(*w);
            check(!lua.eventScript(*w, {0, 1, 0, 0, false}) && findItem(*w, 1)->count == 1 &&
                      findQuest(*w, 1)->progress == 2,
                  "Lua failure rolls back whole command");
            api = luaMap("engine.change_item(1,0.5)");
            lua.prepareEventScripts({api}, {});
            lua.beginEventStep(*w);
            check(!lua.eventScript(*w, {0, 1, 0, 0, false}), "Lua fractional delta rejected");
        }
        std::cout << "PASS inventory / quests / stable IDs / format migration / event conditions / Lua "
                     "/ map transfer / six-second all-Hz T-R / zero heap\nWorldState bytes "
                  << sizeof(WorldState) << '\n';
        return 0;
    } catch (const std::exception& e) {
        measuring = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
