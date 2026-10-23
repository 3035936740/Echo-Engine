#include "echo/control.hpp"
#include "echo/simulation.hpp"
#include "echo/snapshot_diff.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
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
static std::string legacyText(std::string text) {
    const std::string row = "PAGE_CONDITION 0 \"\"\n";
    for (auto at = text.find(row); at != std::string::npos; at = text.find(row))
        text.erase(at, row.size());
    return text;
}
static void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
static EventCommand command(EventOp op, int a = 0, int b = 0, float value = 0) {
    EventCommand c;
    c.op = op;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
static void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.level.events.clear();
    w.level.portals.clear();
    w.level.dialogues.clear();
}
static void place(WorldState& w, int id) {
    EventPlacement p;
    p.id = id;
    p.region = {w.kino.x, w.kino.y, 32, 48};
    w.level.events.push(p);
}
static EventDefinition scene(int id, int profile, float wait, int switchId = -1) {
    auto e = eventPreset(id, 2);
    e.pages[0].switchId = switchId;
    e.pages[0].commands = {command(EventOp::PlayerControl, profile, allPlayerControls),
                           command(EventOp::Wait, 0, 0, wait), command(EventOp::Exit)};
    return e;
}
struct KeyHooks : Hooks {
    int jumps{}, downs{};
    void key(WorldState&, const char* key) override {
        if (std::string_view(key) == "Space")
            ++jumps;
        if (std::string_view(key) == "Down")
            ++downs;
    }
};
int main() {
    try {
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        check(controlProfile(0) == 511 && controlProfile(1) == 510 && controlProfile(2) == 480 &&
                  controlProfile(3) == 415 && controlProfile(4) == 448 &&
                  controlProfile(5, 64 | 256) == 320,
              "preset permission contract");
        EventMap local, common;
        local.events = {scene(1, 2, .1f, 0), scene(2, 3, .3f, 1)};
        place(*w, 1);
        place(*w, 2);
        w->events.switches[0] = w->events.switches[1] = 1;
        stepEvents(*w, local, common, false, fixedDt);
        auto p = resolveControl(*w);
        check(p.owners.count == 2 && !p.permits(PlayerControl::Move) &&
                  !p.permits(PlayerControl::Trace) && p.physics && p.events,
              "parallel requests intersect without pausing clocks");
        w->events.switches[0] = 0;
        for (int i = 0; i < 9; ++i)
            stepEvents(*w, local, common, false, fixedDt);
        p = resolveControl(*w);
        check(p.permits(PlayerControl::Move) && !p.permits(PlayerControl::Trace),
              "one owner release cannot unlock another owner's restriction");
        w->events.switches[1] = 0;
        stepEvents(*w, local, common, false, fixedDt);
        check(resolveControl(*w).allowed == 511,
              "condition invalidation automatically releases request");
        fresh(*w);
        local.events = {scene(1, 5, 1)};
        local.events[0].pages[0].commands[0].b = 320;
        place(*w, 1);
        stepEvents(*w, local, common, false, fixedDt);
        auto text = saveEvents(local);
        check(text.starts_with("ECHO_EVENTS_10") && saveEvents(loadEvents(text)) == text,
              "version four control commands round trip");
        bool rejected = false;
        auto old = legacyText(text);
        old.replace(0, old.find('\n'), "ECHO_EVENTS_3");
        try {
            loadEvents(old);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "old file version cannot silently accept new opcode");
        auto legacy = legacyText(saveEvents(EventMap{{eventPreset(1, 0)}}));
        legacy.replace(0, legacy.find('\n'), "ECHO_EVENTS_3");
        check(loadEvents(legacy).events.size() == 1, "version three still loads");
        auto saved = std::make_unique<WorldState>(*w);
        w->events.local[0].controlAllowed = 511;
        w->controlInput.heldBlocked = 2;
        echo::restore(*w, *saved);
        check(resolveControl(*w).allowed == 320 && !w->controlInput.heldBlocked,
              "full restore includes owned control and key gates");
        local = {};
        stepEvents(*w, local, common, false, fixedDt);
        check(resolveControl(*w).allowed == 511, "removed definition cancels orphan controller");
        // Locks take effect before physics AND before gameplay script key callbacks.
        fresh(*w);
        local.events = {scene(1, 2, .15f, 0)};
        place(*w, 1);
        w->events.switches[0] = 1;
        w->kino.jumpBufferTimer = .1f;
        Simulation sim(384ull * 1024 * 1024);
        KeyHooks hooks;
        sim.setHooks(&hooks);
        sim.setEvents({local}, {});
        sim.reset(*w);
        Input held;
        held.jump = true;
        held.down = true;
        held.focus = true;
        float y = sim.world().kino.y;
        sim.tick(held);
        check(hooks.jumps == 0 && hooks.downs == 0 && sim.world().kino.y == y &&
                  (sim.world().controlInput.heldBlocked & 2),
              "same tick scene blocks jump and Lua key callback");
        sim.world().events.switches[0] = 0;
        for (int i = 0; i < 20; ++i)
            sim.tick(held);
        check(hooks.jumps == 0 && sim.world().kino.y == y, "held jump stays disarmed after scene ends");
        sim.tick({});
        sim.tick({.jump = true});
        check(hooks.jumps == 1 && sim.world().kino.vy < 0, "physical release re-arms jump");
        // Dialogue confirmation shares E with interaction, but never leaks a held jump.
        fresh(*w);
        local.events = {eventPreset(1, 0)};
        place(*w, 1);
        local.events[0].pages[0].commands[0].text.set("Hi");
        sim.setEvents({local}, {});
        sim.reset(*w);
        hooks.jumps = 0;
        Input talking;
        talking.jump = true;
        talking.confirm = true;
        sim.tick(talking);
        check(sim.world().dialogue.active == 8 && hooks.jumps == 0,
              "dialogue acquires control before Space hook");
        sim.tick({.jump = true});
        sim.tick(talking);
        sim.tick({.jump = true});
        sim.tick(talking);
        for (int i = 0; i < 4; ++i)
            sim.tick({.jump = true});
        check(sim.world().dialogue.active < 0 && hooks.jumps == 0 && sim.world().kino.y == y,
              "closing dialogue cannot trigger a held jump");
        sim.tick({});
        sim.tick({.jump = true});
        check(hooks.jumps == 1, "dialogue release restores newly pressed jump");
        fresh(*w);
        local.events = {eventPreset(1, 0)};
        place(*w, 1);
        local.events[0].pages[0].commands.insert(local.events[0].pages[0].commands.begin(),
                                                 command(EventOp::Wait, 0, 0, .04f));
        local.events[0].pages[0].commands[1].text.set("Long confirm test");
        sim.setEvents({local}, {});
        sim.reset(*w);
        for (int i = 0; i < 4; ++i)
            sim.tick({.confirm = true});
        check(sim.world().dialogue.active == 8 && sim.world().dialogue.revealed < 2,
              "held E through foreground wait cannot auto-confirm newly opened dialogue");
        // Rewind denial also requires release; cannot automatically begin after an event releases.
        fresh(*w);
        local.events = {scene(1, 3, 1, 0)};
        place(*w, 1);
        w->events.switches[0] = 1;
        sim.setEvents({local}, {});
        sim.reset(*w);
        sim.tick({});
        sim.tick({.trace = true});
        sim.world().events.switches[0] = 0;
        for (int i = 0; i < 5; ++i)
            sim.tick({.trace = true});
        check(sim.world().state == TimeState::Free, "denied T cannot become delayed rewind");
        sim.tick({});
        sim.tick({.trace = true});
        check(sim.world().state == TimeState::Trace, "released T can rewind after lock removal");
        sim.tick({});
        // Parent cancellation removes called children, preserving unrelated globals.
        fresh(*w);
        local = {};
        common = {};
        auto parent = eventPreset(1, 0);
        parent.pages[0].commands = {command(EventOp::CallCommon, 1)};
        EventPage replacement;
        replacement.trigger = EventTrigger::Parallel;
        replacement.switchId = 0;
        replacement.commands = {command(EventOp::Wait, 0, 0, 10)};
        parent.pages.push_back(replacement);
        local.events.push_back(parent);
        place(*w, 1);
        auto child = eventPreset(1, 0);
        child.pages[0].commands = {command(EventOp::PlayerControl, 5, 0), command(EventOp::Text)};
        child.pages[0].commands[1].text.set("owned child");
        common.events.push_back(child);
        common.events.push_back(scene(2, 1, 10));
        for (int i = 0; i < 4; ++i)
            stepEvents(*w, local, common, i == 0, fixedDt);
        check(w->events.common[0].called && w->events.common[0].caller == 0 && w->dialogue.active == 8,
              "called child retains parent through initial page selection");
        w->events.switches[0] = 1;
        stepEvents(*w, local, common, false, fixedDt);
        check(!w->events.common[0].running && w->dialogue.active < 0 && w->events.common[1].running &&
                  w->events.common[1].controlActive,
              "page cancellation releases descendant dialogue, preserves independent global");
        // Leaving a map cancels local requests and local-owned calls, not independent global tasks.
        w->events.local[0].running = true;
        w->events.local[0].controlActive = true;
        w->events.local[0].controlAllowed = 0;
        w->events.common[0].requested = true;
        w->events.common[0].called = true;
        w->events.common[0].caller = 0;
        cancelEvents(*w, false);
        check(!w->events.local[0].running && !w->events.common[0].requested &&
                  w->events.common[1].running,
              "map cancellation respects call ownership");
        // Actual event transfer commits cancellation while a called child owns dialogue.
        fresh(*w);
        local = {};
        common = {};
        parent = eventPreset(1, 0);
        parent.pages[0].commands = {command(EventOp::CallCommon, 1)};
        local.events.push_back(parent);
        place(*w, 1);
        auto warp = scene(2, 0, 1, 1);
        warp.pages[0].commands = {command(EventOp::Transfer, 1, 1)};
        local.events.push_back(warp);
        place(*w, 2);
        child = eventPreset(1, 0);
        child.pages[0].commands[0].text.set("Transfer cancels child");
        common.events.push_back(child);
        common.events.push_back(scene(2, 1, 10));
        std::vector<LevelState> maps{w->level, w->level};
        maps[1].events.clear();
        maps[1].spawn = {500, 592};
        sim.setMaps(maps);
        sim.setEvents({local, EventMap{}}, common);
        sim.reset(*w);
        sim.tick({.confirm = true});
        sim.tick({});
        check(sim.world().dialogue.active == 8, "called dialogue is active before transfer");
        sim.world().events.switches[1] = 1;
        sim.tick({});
        sim.tick({});
        check(sim.world().activeMap == 1 && sim.world().dialogue.active < 0 &&
                  !sim.world().events.common[0].running && sim.world().events.common[1].running &&
                  sim.world().events.common[1].controlActive,
              "map commit cancels child, retains independent global request");
        sim.setMaps({});
        w->state = TimeState::Suppressed;
        w->dialogue = {};
        w->events.common[1].controlAllowed = 0;
        check(resolveControl(*w).allowed == 128, "finished state retains explicit restart priority");
        // Hit cannot be held hostage by a scene's custom zero-permission mask.
        sim.setHooks(nullptr);
        fresh(*w);
        sim.setEvents({}, {});
        sim.reset(*w);
        for (int i = 0; i < 90; ++i)
            sim.tick({});
        auto& live = sim.world();
        live.events.local[0].running = true;
        live.events.local[0].controlActive = true;
        live.events.local[0].controlAllowed = 0;
        live.state = TimeState::Hit;
        live.kino.hp = 0;
        live.gameplayClockPaused = true;
        check(resolveControl(live).allowed == (64 | 128), "Hit has explicit T rescue priority");
        sim.tick({.trace = true});
        check(sim.world().state == TimeState::Trace, "Hit enters T even when scene denied all controls");
        sim.tick({});
        fresh(*w);
        w->rewind.enabled = false;
        w->state = TimeState::Hit;
        check(resolveControl(*w).allowed == 128 && !resolveControl(*w).history,
              "disabled rewind Hit explicitly permits restart only");
        fresh(*w);
        w->dialogue.active = 0;
        w->gameplayClockPaused = true;
        p = resolveControl(*w);
        check(!p.physics && p.events && p.audio && p.history && p.permits(PlayerControl::Confirm),
              "dialogue clock contract");
        w->dialogue = {};
        w->transition.phase = TransitionPhase::Closing;
        p = resolveControl(*w);
        check(!p.physics && !p.events && p.audio && p.history, "transition clock contract");
        w->transition = {};
        p = resolveControl(*w);
        check(!p.physics && !p.events && !p.audio && !p.history, "gameplay pause clock contract");
        // Farthest T/R, all capture rates; ownership and release latches stay branch state.
        for (int hz : {30, 60, 120})
            for (bool fast : {false, true}) {
                fresh(*w);
                w->rewind.record_hz = hz;
                local.events = {scene(1, 1, 20)};
                place(*w, 1);
                sim.setEvents({local}, {});
                sim.reset(*w);
                for (int i = 0; i < 420; ++i)
                    sim.tick({});
                Input rewind;
                rewind.fast = fast;
                rewind.trace = !fast;
                for (int i = 0; i < 500 && (i == 0 || sim.world().state == TimeState::Fast ||
                                            sim.world().state == TimeState::Trace);
                     ++i)
                    sim.tick(rewind);
                check(sim.history().size() == 1 && sim.world().events.local[0].controlActive,
                      "farthest branch restores historical event owner");
                sim.tick({});
                for (int i = 0; i < 10; ++i)
                    sim.tick({});
                check(sim.world().events.local[0].running &&
                          !resolveControl(sim.world()).permits(PlayerControl::Move),
                      "farthest T/R continues running scene without losing lock");
                auto baseline = std::make_unique<WorldState>(sim.world());
                track = true;
                for (int i = 0; i < 60; ++i)
                    sim.tick({});
                track = false;
                check(allocations == 0, "control/coordination/record ticks allocate no C++ heap");
                auto expected = std::make_unique<WorldState>(sim.world());
                sim.restore(*baseline);
                sim.history().clear();
                sim.record();
                for (int i = 0; i < 60; ++i)
                    sim.tick({});
                // History indices differ after rebuilding the physical buffer; gameplay controls do not.
                check(sim.world().events.local[0].controlAllowed ==
                              expected->events.local[0].controlAllowed &&
                          sim.world().events.local[0].wait == expected->events.local[0].wait &&
                          sim.world().controlInput.heldBlocked == expected->controlInput.heldBlocked,
                      "post-farthest owner/wait/input replay is exact");
            }
        auto report = std::make_unique<SnapshotDiff>();
        compareSnapshots(*w, *w, *report);
        std::cout << "PASS owned controls, key gates, parent cancellation, clocks, versions, farthest "
                     "T/R, zero allocations; world_bytes="
                  << sizeof(WorldState) << " scalar_fields=" << report->inspected << '\n';
        return 0;
    } catch (const std::exception& e) {
        track = false;
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
