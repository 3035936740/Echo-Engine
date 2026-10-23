#include "echo/database.hpp"
#include "echo/editor.hpp"
#include "echo/recovery.hpp"
#include "echo/simulation.hpp"
#include <iostream>
#include <algorithm>
#include <memory>
#include <stdexcept>
using namespace echo;
static void check(bool b, const char* label) {
    if (!b)
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
}
int main(int argc, char** argv) {
    try {
        Database db;
        db.switches[2].name.set("Chest opened");
        db.variables[1].name.set("Score");
        db.variables[1].note.set("Global score");
        db.variables[1].initial = 42;
        auto saved = saveDatabase(db);
        check(saveDatabase(loadDatabase(saved)) == saved, "Named database roundtrip");
        check(dataLabel(db, true, 1).find("Score") != std::string::npos,
              "Readable labels retain stable IDs");
        rejects([] { loadDatabase("ECHO_DATABASE_1\n2\n1 0 \"a\" \"\"\n1 0 \"b\" \"\"\n0\n"); },
                "Duplicate IDs rejected");
        rejects([] { loadDatabase("ECHO_DATABASE_1\n1\n1 2 \"a\" \"\"\n0\n"); },
                "Nonboolean switch rejected");
        rejects([&] { loadDatabase(saved + "junk"); }, "Trailing database data rejected");
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        applyDatabaseDefaults(*w, db);
        check(w->events.variables[1] == 42, "Database seeds snapshot-owned values");
        Simulation sim(8ull * 1024 * 1024);
        w->aster.enabled = false;
        w->kino.invulnTimer = 1000;
        sim.reset(*w);
        for (int i = 0; i < 90; ++i)
            sim.tick({});
        auto generation = sim.world().timelineGenerationId;
        sim.world().events.variables[1] = 100;
        sim.commitRuntimeEdit();
        check(sim.history().at(sim.history().size() - 1).events.variables[1] == 100,
              "Debugger edit commits current full snapshot");
        sim.seekHistory(0);
        check(sim.world().events.variables[1] == 42 && sim.history().size() == 1 &&
                  sim.world().timelineGenerationId > generation,
              "Oldest seek restores variables and truncates future");
        sim.tick({});
        check(sim.world().state == TimeState::Free && sim.world().clock > 0, "Play after farthest seek");
        rejects([&] { sim.seekHistory(100); }, "Out of range seek rejected");
        sim.world().rewind.record_hz = 30;
        sim.reset(sim.world());
        sim.tick({});
        auto earlier = sim.history().size();
        sim.world().events.variables[1] = 77;
        sim.commitRuntimeEdit();
        check(sim.history().size() == earlier + 1,
              "Debug edit between 30 Hz captures retains previous snapshot");
        sim.seekHistory(0);
        check(sim.world().events.variables[1] == 42, "30 Hz debug branch restores previous values");
        sim.world().rewind.enabled = false;
        rejects([&] { sim.seekHistory(0); }, "Disabled seek rejected");

        auto editor = std::make_unique<LevelEditor>();
        editor->level = {};
        editor->tool = 18;
        editor->tileIndex = 5;
        editor->paint(4 * tileSize, 3 * tileSize);
        editor->paint(2 * tileSize, 1 * tileSize);
        editor->end();
        check(editor->level.layers[1].cells[mapWidth + 2] == 5 && editor->level.at(4, 3) == Tile::Solid,
              "Reverse rectangle paints art and collision");
        editor->undo();
        check(editor->level.at(4, 3) == Tile::Empty, "Rectangle is one undo transaction");
        editor->redo();
        editor->tool = 20;
        editor->paint(2 * tileSize, 1 * tileSize);
        editor->paint(3 * tileSize, 1 * tileSize);
        editor->end();
        check(editor->copySelection() && editor->pasteSelection(8, 1), "Selection copy/paste");
        editor->level.layers[1].cells[mapWidth + 8] = 2;
        editor->flipSelection(true);
        check(editor->level.layers[1].cells[mapWidth + 9] == 2, "Horizontal selection flip");
        editor->clearSelection();
        check(editor->level.at(8, 1) == Tile::Empty, "Clear selection collision");
        editor->undo();
        editor->layerLocked[1] = true;
        check(!editor->pasteSelection(0, 0), "Locked layer refuses paste");
        editor->tool = 18;
        editor->paint(0, 0);
        editor->end();
        check(editor->level.at(0, 0) == Tile::Empty, "Locked layer refuses rectangle");
        editor->layerLocked[1] = false;
        editor->level = {};
        editor->terrain = Tile::OneWay;
        editor->tileIndex = 3;
        editor->floodFill(0, 0);
        editor->end();
        check(editor->level.at(95, 23) == Tile::OneWay && editor->level.layers[1].cells.back() == 3,
              "Bounded full-map bucket fill");
        editor->undo();
        check(editor->level.at(95, 23) == Tile::Empty, "Bucket undo");
        editor->level.layers[1].cells[1] = 1;
        editor->level.layers[1].cells[2] = 1;
        editor->selectionValid = false;
        editor->retileSelection();
        check(editor->level.layers[1].cells[1] == 3 && editor->level.layers[1].cells[2] == 9,
              "Four-neighbor atlas mask");
        editor->layer = 0;
        editor->tool = 20;
        editor->paint(0, 0);
        editor->end();
        editor->copySelection();
        editor->layer = 1;
        editor->pasteSelection(0, 0);
        check(editor->level.at(0, 0) == Tile::Empty, "Background clipboard does not invent collision");
        editor->set(w->level);
        w->level = editor->level;
        w->rewind.enabled = true;
        fresh(*w);
        w->aster.enabled = false;
        w->kino.invulnTimer = 1000;
        sim.reset(*w);
        for (int i = 0; i < 90; ++i)
            sim.tick({});
        Input t;
        t.trace = true;
        for (int i = 0; i < 180; ++i)
            sim.tick(t);
        sim.tick({});
        sim.tick({});
        check(sim.world().state == TimeState::Free, "Edited map rewind to farthest then resume");

        if (argc > 1) {
            Resources r;
            r.root = pathFromUtf8(argv[1]);
            auto config = r.config();
            Recovery point;
            point.map = config.maps.front();
            point.files = {{"project.echo", r.text("project.echo")},
                           {"database.echo", saved},
                           {"events/common.echo", r.text("events/common.echo")},
                           {point.map, r.text(point.map)},
                           {point.map + ".events", r.text(point.map + ".events")}};
            auto serialized = saveRecovery(point);
            auto decoded = loadRecovery(serialized);
            validateRecovery(decoded, r);
            check(decoded.files == point.files, "Recovery multiline resources roundtrip");
            Resources candidate = r;
            candidate.setEditorOverlay(decoded.files);
            check(candidate.text("database.echo") == saved && candidate.config().maps == config.maps,
                  "Recovery overlay resource/config integration");
            auto original = r.text(point.map);
            candidate.clearEditorOverlay();
            check(candidate.text(point.map) == original, "Recovery does not overwrite authored files");
            rejects([&] { loadRecovery(serialized + "garbage"); }, "Trailing recovery data rejected");
            point.files["../escape.echo"] = "malicious";
            rejects([&] { validateRecovery(point, r); }, "Recovery path/whitelist rejected");
            point = decoded;
            point.files[point.map] = "BROKEN";
            rejects([&] { validateRecovery(point, r); }, "Invalid level recovery rejected");
            auto problems = inspectProject(r);
            for (auto& p : problems)
                check(!p.error, "Sample project resource graph validates");
            BarrageLibrary targetLibrary;
            auto targetPattern = barragePreset(allocateBarrageId(targetLibrary), 1);
            targetLibrary.patterns.push_back(targetPattern);
            BarrageEmitter targetEmitter;
            targetEmitter.id = allocateBarrageId(targetLibrary);
            targetEmitter.name.set("Broken target reference");
            targetEmitter.target = BarrageTarget::Object;
            targetEmitter.targetObject = UINT64_MAX - 7;
            targetEmitter.waves.push_back({targetPattern.id});
            targetLibrary.emitters.push_back(targetEmitter);
            auto targetFiles = decoded.files;
            targetFiles["barrage.echo"] = saveBarrage(targetLibrary);
            candidate.setEditorOverlay(targetFiles);
            problems = inspectProject(candidate);
            check(std::any_of(problems.begin(), problems.end(),
                              [](const auto& problem) {
                                  return problem.error && problem.location == "barrage.echo" &&
                                         problem.message.find("missing target object") !=
                                             std::string::npos;
                              }),
                  "Export check locates missing barrage target");
            candidate.clearEditorOverlay();
            auto db2 = db;
            db2.variables[0].name.set("Score");
            decoded.files["database.echo"] = saveDatabase(db2);
            candidate.setEditorOverlay(decoded.files);
            problems = inspectProject(candidate);
            bool duplicate = false;
            for (auto& p : problems)
                duplicate |= p.message.find("Duplicate") != std::string::npos;
            check(duplicate, "Project check finds duplicate database name");
            decoded.files["project.echo"] += "\nplayer_sprite=missing.png\n";
            candidate.setEditorOverlay(decoded.files);
            problems = inspectProject(candidate);
            bool missing = false;
            for (auto& p : problems)
                missing |= p.error && p.message.find("Missing resource") != std::string::npos;
            check(missing, "Project check finds missing asset");
        }
        std::cout << "PASS database, map transactions, oldest seek/resume, recovery and project graph\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
