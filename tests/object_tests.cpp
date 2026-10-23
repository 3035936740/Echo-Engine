#include "echo/objects.hpp"
#include "echo/editor.hpp"
#include "echo/resources.hpp"
#include "echo/simulation.hpp"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace echo;
static void check(bool ok, const char* label) {
    if (!ok)
        throw std::runtime_error(label);
}
template <class F> static void rejects(F f, const char* label) {
    bool rejected = false;
    try {
        f();
    } catch (...) {
        rejected = true;
    }
    check(rejected, label);
}
static void fresh(WorldState& w) {
    w = makeWorld();
    w.aster.enabled = false;
    w.kino.invulnTimer = 1000;
}
int main() {
    try {
        ObjectLibrary library;
        for (int i = 0; i < 9; ++i)
            library.templates.push_back(objectPreset(i + 1, i));
        auto saved = saveObjects(library);
        check(saveObjects(loadObjects(saved)) == saved, "Template roundtrip");
        rejects([&] { loadObjects(saved + "junk"); }, "Reject trailing template data");
        auto duplicate = library;
        duplicate.templates[1].id = 1;
        rejects([&] { saveObjects(duplicate); }, "Reject duplicate identities");
        auto invalid = library;
        invalid.templates[0].attackInterval = 0;
        rejects([&] { saveObjects(invalid); }, "Reject zero interval");
        auto allocator = loadObjects(saved);
        auto allocated = allocateObjectTemplateId(allocator);
        allocator.templates.clear();
        allocator = loadObjects(saveObjects(allocator));
        check(allocateObjectTemplateId(allocator) > allocated, "Template IDs never reuse deleted identities");
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        auto patrol = library.templates[0];
        patrol.damage = 0;
        patrol.gravity = false;
        auto old = createObject(*w, patrol, {800, 400}, 0);
        auto snapshot = std::make_unique<WorldState>(*w);
        check(resolveObject(*w, old), "Create and resolve");
        check(destroyObject(*w, old) && !resolveObject(*w, old), "Destroy invalidates old handle");
        auto reused = createObject(*w, patrol, {1000, 400}, 0);
        check(reused.slot == old.slot && reused.generation != old.generation && !resolveObject(*w, old),
              "Pool reuse rejects stale handle");
        restore(*w, *snapshot);
        check(resolveObject(*w, old) && !resolveObject(*w, reused),
              "Restore identity and allocation generation");
        fresh(*w);
        ObjectPlacement p;
        p.id = 1;
        p.templateId = 1;
        p.x = 800;
        p.y = 400;
        p.speed = 12;
        p.hp = 50;
        p.overrides = OverrideSpeed | OverrideHP;
        w->level.objects.push(p);
        enterObjectMap(*w, library);
        auto& authored = w->objects.slots[0];
        check(authored.hp == 50 && authored.speed == 12, "Independent instance overrides");
        p.id = 2;
        p.templateId = 6;
        p.x = w->kino.x;
        p.y = w->kino.y;
        p.overrides = 0;
        w->level.objects.push(p);
        enterObjectMap(*w, library);
        stepObjects(*w, library, fixedDt);
        check(w->events.variables[0] == 1 && !w->objects.slots[1].alive,
              "Pickup applies once and becomes tombstone");
        w->activeMap = 1;
        w->level.objects.clear();
        enterObjectMap(*w, library);
        w->activeMap = 0;
        w->level.objects.push(p);
        enterObjectMap(*w, library);
        stepObjects(*w, library, fixedDt);
        check(w->events.variables[0] == 1, "Persistent pickup stays collected on re-entry");
        w->level.objects[0].persistent = false;
        enterObjectMap(*w, library);
        stepObjects(*w, library, fixedDt);
        check(w->events.variables[0] == 2, "Explicit respawn policy resets pickup");
        fresh(*w);
        auto falling = patrol;
        falling.gravity = true;
        falling.speed = 0;
        w->objects.slots[0].generation = UINT32_MAX;
        auto retired = createObject(*w, falling, {350, 100}, 0);
        check(retired.slot != 0, "Exhausted generation slot is retired");
        fresh(*w);
        auto floor = createObject(*w, falling, {350, 100}, 0);
        for (int i = 0; i < 180; ++i)
            stepObjects(*w, library, fixedDt);
        check(resolveObject(*w, floor)->grounded && resolveObject(*w, floor)->y <= 640,
              "Enemy floor collision");
        fresh(*w);
        auto wall = objectPreset(4, 4);
        createObject(*w, wall, {200, w->kino.y}, 0);
        Input right;
        right.move = 1;
        for (int i = 0; i < 90; ++i)
            stepPhysics(*w, right, fixedDt);
        check(w->kino.x + playerWidth <= 200.1f, "Player blocked by authored solid object");
        fresh(*w);
        for (int i = 0; i < maxObjects; ++i)
            check(resolveObject(*w, createObject(*w, patrol, {1000.f, 300.f}, 0)), "Fill pool");
        auto count = w->objects.rejected;
        check(!resolveObject(*w, createObject(*w, patrol, {1000, 300}, 0)) &&
                  w->objects.rejected == count + 1,
              "Full pool rejects deterministically");
        auto editor = std::make_unique<LevelEditor>();
        editor->tool = 22;
        editor->objectTemplate = 1;
        editor->paint(352, 320);
        editor->paint(384, 320);
        editor->end();
        check(editor->level.objects.count == 1 && editor->level.objects[0].x == 384,
              "Object placement drag transaction");
        editor->undo();
        check(editor->level.objects.count == 0, "Undo object deployment");
        editor->redo();
        check(editor->level.objects.count == 1, "Redo object deployment");
        auto oldPlacement = editor->level.objects[0].id;
        editor->paint(384, 320, true); editor->end();
        editor->paint(512, 320); editor->end();
        check(editor->level.objects[0].id > oldPlacement, "Deleted placement IDs never reuse");
        auto level = saveLevel(editor->level);
        check(saveLevel(loadLevel(level)) == level, "Version 9 object map roundtrip");
        rejects([&] { loadLevel(level + "junk"); }, "Trailing map data rejected");
        auto persisted = loadLevel(level);
        auto nextPlacement = persisted.nextObjectId;
        persisted.objects.clear();
        check(loadLevel(saveLevel(persisted)).nextObjectId == nextPlacement,
              "Placement identity allocator persists without instances");
        auto bad = editor->level;
        bad.objects.push(bad.objects[0]);
        rejects([&] { saveLevel(bad); }, "Duplicate placement IDs rejected");
        for (int hz : {30, 60, 120}) {
            fresh(*w);
            w->rewind.record_hz = hz;
            w->level.objects.clear();
            p = {};
            p.id = 1;
            p.templateId = 1;
            p.x = 1200;
            p.y = 400;
            p.overrides = OverrideDamage;
            p.damage = 0;
            w->level.objects.push(p);
            Simulation sim(12ull * 1024 * 1024);
            sim.setObjects(library);
            sim.reset(*w);
            for (int i = 0; i < 120; ++i)
                sim.tick({});
            auto h = ObjectHandle{0, sim.world().objects.slots[0].generation};
            destroyObject(sim.world(), h);
            sim.record();
            auto expected = sim.history().at(0).objects;
            sim.seekHistory(0);
            check(sim.world().objects == expected, "Oldest history fully restores object world");
            sim.tick({});
            check(sim.world().state == TimeState::Free && sim.world().objects.slots[0].alive,
                  "Continue after farthest object rewind");
        }
        fresh(*w);
        w->rewind.enabled = false;
        Simulation disabled(12ull * 1024 * 1024);
        disabled.setObjects(library);
        disabled.reset(*w);
        disabled.tick({});
        check(disabled.history().capacity() == 0, "Disabled rewind does not allocate object history");
        fresh(*w);
        auto enemy = patrol;
        enemy.hp = 1000;
        auto h = createObject(*w, enemy, {300, 300}, 0);
        w->fast.A = {100, 300};
        w->kino.x = 350;
        w->kino.y = 300;
        check(fastImpactObjects(*w, {100, 300}, {350, 300}), "Fast chord hits generic enemy");
        check(resolveObject(*w, h)->hp == 915 && resolveObject(*w, h)->invuln == 6,
              "Distance damage and protection are snapshot fields");
        fresh(*w);
        w->kino.hp = 7;
        Simulation health(16ull*1024*1024);
        health.reset(*w);
        for (int i=0;i<20;++i) health.tick({});
        health.world().kino.hp = 4;
        for (int i=0;i<20;++i) health.tick({});
        Input trace; trace.trace = true;
        health.tick(trace);
        health.tick({});
        check(health.world().kino.hp == 4, "T release preserves restored multi-HP state");
        fresh(*w);
        auto farther = createObject(*w, enemy, {600,300},0);
        auto nearer = createObject(*w, enemy, {300,300},0);
        auto impact = findFastObjectImpact(*w,{100,300,40,60},{600,0});
        check(impact.handle.slot == nearer.slot && impact.handle.slot != farther.slot &&
              std::abs(impact.entry-(160.f/600)) < .001f, "Fast sweep selects nearest enemy with custom body");
        std::cout << "PASS object lifecycle, template overrides, persistence, pool bounds, collision, "
                     "map editing and farthest rewind at 30/60/120 Hz\nWorldState "
                  << sizeof(WorldState) << " bytes\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
