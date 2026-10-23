#include "echo/editor.hpp"
#include "echo/resources.hpp"
#include "echo/simulation.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace echo;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool near(float a, float b) {
    return std::abs(a - b) < .001f;
}
BarrageLibrary definitions() {
    BarrageLibrary l;
    auto p = barragePreset(allocateBarrageId(l), 1);
    p.damage = 0;
    p.count = 4;
    p.lifetime = 2;
    l.patterns.push_back(p);
    BarrageEmitter e;
    e.id = allocateBarrageId(l);
    e.name.set("Map emitter");
    e.position = {600, 240};
    e.target = BarrageTarget::Point;
    e.targetPoint = {900, 400};
    e.waves = {{p.id, 2, 0, .85f, .2f, 15}};
    l.emitters.push_back(e);
    return l;
}
void emitterEqual(const WorldState& a, const WorldState& b) {
    const auto& x = a.barrage.emitters[0];
    const auto& y = b.barrage.emitters[0];
    check(x.id == y.id && x.elapsed == y.elapsed && x.seed == y.seed && x.shot == y.shot &&
              x.wave == y.wave && x.origin.x == y.origin.x && x.origin.y == y.origin.y &&
              x.target.x == y.target.x && x.target.y == y.target.y && x.locked == y.locked,
          "Authoring definitions changed restored emitter continuation");
    check(a.bullets.count == b.bullets.count, "Restored authored bullet count");
    for (std::size_t i = 0; i < a.bullets.count; ++i) {
        const auto& p = a.bullets[i];
        const auto& q = b.bullets[i];
        check(p.id == q.id && p.x == q.x && p.y == q.y && p.px == q.px && p.py == q.py && p.vx == q.vx &&
                  p.vy == q.vy,
              "Authored trajectory differs after restore");
    }
}
} // namespace
int main() {
    try {
        auto editor = std::make_unique<LevelEditor>();
        auto world = std::make_unique<WorldState>(makeWorld());
        editor->set(world->level);
        editor->barrage = definitions();
        auto id = editor->barrage.emitters[0].id;

        // A tile, property form and common event share one chronological undo stream.
        editor->tool = 1;
        editor->paint(400, 160);
        editor->end();
        auto mapBeforeProperties = saveLevel(editor->level);
        auto oldSpeed = editor->barrage.patterns[0].speed;
        editor->begin();
        editor->barrage.patterns[0].speed = 234;
        editor->barrage.emitters[0].targetFaction = 321;
        editor->barrage.emitters[0].targetObject = 123456;
        editor->barrage.emitters[0].targetPoint = {123, 456};
        editor->barrage.emitters[0].waves.push_back({editor->barrage.patterns[0].id});
        editor->end();
        auto propertyText = saveBarrage(editor->barrage);
        editor->begin();
        editor->common.events.push_back(eventPreset(1, 2));
        editor->end();
        editor->undo();
        check(editor->common.events.empty() && saveBarrage(editor->barrage) == propertyText,
              "Common-event undo skipped newest operation");
        editor->undo();
        check(editor->barrage.patterns[0].speed == oldSpeed &&
                  editor->barrage.emitters[0].targetFaction == 2 &&
                  editor->barrage.emitters[0].waves.size() == 1 &&
                  saveLevel(editor->level) == mapBeforeProperties,
              "Barrage property undo changed map");
        editor->undo();
        check(editor->level.layers[1].cells[5 * mapWidth + 12] == 0,
              "Tile undo order skipped barrage operation");
        editor->redo();
        editor->redo();
        check(saveBarrage(editor->barrage) == propertyText, "Barrage deep redo lost waves / target");
        editor->redo();
        check(editor->common.events.size() == 1, "Common-event redo order");

        // Click selection is read-only; positioning and an entire mouse stroke use one record.
        auto before = editor->undoCount();
        editor->tool = 23;
        editor->paint(603, 245);
        editor->end();
        check(editor->selectedEmitter == id && editor->undoCount() == before,
              "Selecting marker created undo operation");
        editor->paint(603, 245);
        editor->paint(713.5f, 222.25f);
        editor->paint(723.5f, 212.25f);
        editor->end();
        auto moved = editor->barrage.emitters[0].position;
        check(near(moved.x, 720.5f) && near(moved.y, 207.25f) && editor->undoCount() == before + 1,
              "Drag lost grab offset / stroke coalescing");
        editor->undo();
        check(editor->barrage.emitters[0].position.x == 600 &&
                  editor->barrage.emitters[0].position.y == 240,
              "Drag undo");
        editor->redo();
        check(editor->barrage.emitters[0].position.x == moved.x, "Drag redo");
        check(editor->armEmitter(id), "Arm emitter positioning");
        editor->paint(512.25f, 256.75f);
        editor->end();
        check(near(editor->barrage.emitters[0].position.x, 512.25f) &&
                  near(editor->barrage.emitters[0].position.y, 256.75f),
              "World positioning snapped physics");
        before = editor->undoCount();
        check(editor->armEmitter(id), "Re-arm marker");
        editor->paint(700, 300, true);
        check(!editor->emitterPlacementArmed && editor->undoCount() == before &&
                  editor->barrage.emitters[0].position.x == 512.25f,
              "Right cancel changed definition");

        // Parent references and world positions use the same helper as renderer markers.
        editor->level.spawn = {100, 200};
        editor->level.bossSpawn = {900, 300};
        ObjectPlacement object;
        object.id = 25;
        object.templateId = 1;
        object.x = 800;
        object.y = 400;
        editor->level.objects.push(object);
        for (auto origin : {BarrageOrigin::Player, BarrageOrigin::Boss, BarrageOrigin::Object}) {
            auto& e = editor->barrage.emitters[0];
            e.origin = origin;
            e.object = 25;
            e.position = {16, -8};
            Vec2 point;
            check(barrageEditorOrigin(editor->level, e, point), "Authored parent anchor query");
            check(editor->armEmitter(id), "Arm attached emitter");
            editor->paint(1100.5f, 333.75f);
            editor->end();
            check(e.origin == origin && e.object == 25 && barrageEditorOrigin(editor->level, e, point) &&
                      near(point.x, 1100.5f) && near(point.y, 333.75f),
                  "Positioning detached parent or used world coordinates as offsets");
            auto attached = e.position;
            editor->tool = 23;
            editor->paint(point.x + 3, point.y + 5);
            editor->paint(point.x + 53.25f, point.y + 15.5f);
            editor->end();
            check(near(e.position.x, attached.x + 50.25f) && near(e.position.y, attached.y + 10.5f),
                  "Attached drag used stale anchor");
        }
        auto& e = editor->barrage.emitters[0];
        e.object = 999;
        before = editor->undoCount();
        check(!editor->armEmitter(id), "Missing parent allowed positioning");
        editor->paint(-10, 100);
        editor->paint(NAN, 100);
        check(editor->undoCount() == before, "Invalid pointer added history");
        e.map = 1;
        e.origin = BarrageOrigin::World;
        check(!editor->armEmitter(id), "Other-map emitter was selected");
        auto definitionsBeforeMap = saveBarrage(editor->barrage);
        editor->set(world->level, {}, {}, 1);
        check(editor->mapIndex == 1 && !editor->undoCount() &&
                  saveBarrage(editor->barrage) == definitionsBeforeMap && editor->armEmitter(id),
              "Map switch cleared definitions or retained wrong history");

        // Allocator high-water marks are preserved through undo and future truncation.
        editor->begin();
        auto created = editor->barrage.emitters[0];
        created.id = allocateBarrageId(editor->barrage);
        editor->barrage.emitters.push_back(created);
        editor->end();
        auto deletedId = created.id;
        editor->undo();
        check(editor->barrage.emitters.size() == 1 && editor->barrage.nextId > deletedId,
              "Undo reused allocator IDs");
        editor->begin();
        created.id = allocateBarrageId(editor->barrage);
        editor->barrage.emitters.push_back(created);
        editor->end();
        check(created.id > deletedId && !editor->redoCount(), "New branch reused ID / retained redo");
        auto deletionText = saveBarrage(editor->barrage);
        editor->begin();
        editor->barrage.emitters.erase(editor->barrage.emitters.begin() + 1);
        editor->end();
        editor->undo();
        check(saveBarrage(editor->barrage) == deletionText, "Deleted emitter undo lost configuration");
        editor->redo();
        check(editor->barrage.emitters.size() == 1, "Deleted emitter redo");

        EventMap referenceMap;
        auto referenceEvent = eventPreset(1, 0);
        EventCommand control;
        control.op = EventOp::EmitterControl;
        control.reference = id;
        referenceEvent.pages[0].commands.push_back(control);
        referenceMap.events.push_back(referenceEvent);
        check(eventUsesEmitter(referenceMap, id) && !eventUsesEmitter(referenceMap, id + 999),
              "Deletion reference query not based on stable ID");

        // Budget counts barrage waves, event payloads and reserved UndoState storage.
        auto heavy = std::make_unique<LevelEditor>();
        heavy->barrage = definitions();
        for (int index = 1; index <= maxMapEvents; ++index) {
            EventDefinition definition;
            definition.id = index;
            definition.name.set("Budget");
            for (int page = 0; page < maxEventPages; ++page) {
                EventPage p;
                p.commands.resize(maxEventCommands);
                definition.pages.push_back(p);
            }
            heavy->events.events.push_back(definition);
        }
        for (int i = 0; i < 24; ++i) {
            heavy->begin();
            heavy->barrage.emitters[0].position.x = float(i);
            heavy->end();
            check(heavy->undoCount() <= 64 && heavy->undoBytes() <= 64ull * 1024 * 1024,
                  "Undo exceeded allocated-payload budget");
        }
        check(heavy->undoCount() < 24 && heavy->undoCount() > 1,
              "Heavy histories did not evict oldest definitions");
        while (heavy->undoCount())
            heavy->undo();
        check(heavy->redoCount() > 0 && heavy->redoBytes() <= 64ull * 1024 * 1024,
              "Redo exceeded payload budget");
        heavy->begin();
        heavy->barrage.emitters.clear();
        heavy->end();
        check(!heavy->redoCount(), "New edit did not clear redo");

        // Authored offsets and waves remain immutable in Play; all Hz recover farthest six seconds.
        auto runtime = definitions();
        editor->set(world->level);
        editor->barrage = runtime;
        check(editor->armEmitter(runtime.emitters[0].id), "Arm before Play");
        editor->paint(640.25f, 240.75f);
        editor->end();
        runtime = editor->barrage;
        for (int hz : {30, 60, 120}) {
            *world = makeWorld();
            world->aster.enabled = false;
            world->script = {};
            world->kino.invulnTimer = 10000;
            world->rewind.record_hz = hz;
            Simulation sim(384ull * 1024 * 1024);
            sim.setBarrage(runtime);
            sim.reset(*world);
            for (int i = 0; i < 420; ++i)
                sim.tick({});
            check(sim.history().size() >= std::size_t(6 * hz), "Full authored six-second window");
            auto old = std::make_unique<WorldState>(sim.history().at(0));
            sim.seekHistory(0);
            for (int i = 0; i < 60; ++i)
                sim.tick({});
            Simulation replay(16ull * 1024 * 1024);
            replay.setBarrage(runtime);
            replay.reset(*world);
            replay.restore(*old);
            for (int i = 0; i < 60; ++i)
                replay.tick({});
            emitterEqual(sim.world(), replay.world());
            check(near(sim.world().barrage.emitters[0].origin.x, 640.25f) &&
                      near(sim.world().barrage.emitters[0].origin.y, 240.75f),
                  "Edited fractional emitter origin changed in snapshots");
            editor->begin();
            editor->barrage.emitters[0].position = {1, 2};
            editor->end();
            sim.tick({});
            check(near(sim.world().barrage.emitters[0].origin.x, 640.25f),
                  "Editing definitions silently mutated active Play");
            editor->undo();
            sim.tick({.trace = true});
            sim.tick({});
            sim.tick({.fast = true});
            sim.tick({});
            check(sim.world().kino.hp > 0, "Authored farthest T/R continuation failed");
        }
        std::cout << "PASS unified map/event/barrage undo, stable IDs, bounded payloads, attached "
                     "position/drag, read-only selection/cancel, map scope and all Hz six-second T/R\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
