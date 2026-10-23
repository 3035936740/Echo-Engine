#include "echo/collision.hpp"
#include "echo/editor.hpp"
#include "echo/presets.hpp"
#include "echo/resources.hpp"
#include "echo/simulation.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace echo;
void check(bool v, const char* s) {
    if (!v)
        throw std::runtime_error(s);
}
int main() {
    try {
        StaticCollider c;
        c.bounds = {100, 0, 5, 50};
        float t = 0;
        check(sweepCollider({0, 0, 10, 10}, {200, 0}, c, t) && std::abs(t - .45f) < .001f,
              "thin-wall sweep");
        c.kind = ColliderKind::Circle;
        c.bounds = {100, 100, 20, 20};
        check(sweepCollider({0, 105, 10, 10}, {200, 0}, c, t) && std::abs(t - .45f) < .001f,
              "exact circle sweep");
        check(!sweepCollider({0, 80, 10, 10}, {200, 0}, c, t), "circle corners are not square");
        c.kind = ColliderKind::Triangle;
        c.bounds = {100, 100, 100, 100};
        check(sweepCollider({140, 0, 20, 20}, {0, 300}, c, t) && std::abs(t - 80.0f / 300) < .001f,
              "triangle apex sweep");
        LevelEditor editor;
        editor.layer = 0;
        editor.paint(96, 96);
        editor.end();
        check(editor.level.layers[0].cells[3 * mapWidth + 3] == 1 &&
                  editor.level.at(3, 3) == Tile::Empty,
              "background art has no collision");
        editor.layer = 2;
        editor.paint(96, 96);
        editor.end();
        check(editor.level.at(3, 3) == Tile::Empty, "foreground art has no collision");
        for (int tool = 10; tool <= 13; ++tool) {
            editor.tool = tool;
            editor.paint(300 + tool * 20, 200);
            editor.paint(350 + tool * 20, 280);
            editor.end();
        }
        editor.tool = 14;
        editor.paint(500, 300);
        editor.paint(520, 310);
        editor.paint(530, 340);
        editor.paint(490, 340);
        editor.paint(500, 300);
        editor.end();
        check(editor.level.colliders.count == 5 && editor.level.colliders[4].points.count >= 3,
              "all collider authoring presets and drawn hull");
        check(editor.level.colliders[4].bounds.w == 40 && editor.level.colliders[4].bounds.h == 40,
              "drawn hull bounds cover all vertices");
        editor.paint(505, 320, true);
        editor.end();
        check(editor.level.colliders.count == 4, "drawn hull can be erased away from start point");
        editor.undo();
        editor.tool = 9;
        editor.paint(90, 90);
        editor.end();
        editor.undo();
        check(editor.level.lights.count == 0, "lamp undo");
        editor.redo();
        check(editor.level.lights.count == 1, "lamp redo");
        editor.tool = 15;
        editor.paint(400, 500);
        editor.paint(450, 560);
        editor.end();
        auto& d = editor.level.dialogues[0];
        d.speaker.set("Kino");
        d.text.set("Hello\nSecond page");
        d.portrait.set("portrait.png");
        auto encoded = saveLevel(editor.level);
        check(saveLevel(loadLevel(encoded)) == encoded,
              "layers/colliders/lamps/dialogue level roundtrip");
        auto w = std::make_unique<WorldState>(makeWorld());
        w->aster.enabled = false;
        w->kino.invulnTimer = 1000;
        DialogueDefinition dialog;
        dialog.region = {w->kino.x, w->kino.y, 50, 50};
        dialog.text.set("Hello\nSecond page");
        w->level.dialogues.push(dialog);
        Simulation sim(64ull * 1024 * 1024);
        sim.reset(*w);
        sim.tick({});
        check(sim.world().dialogue.active == 0 && sim.world().level.dialogues[0].triggered,
              "dialogue region triggers");
        for (int i = 0; i < 10; ++i)
            sim.tick({});
        auto snapshot = std::make_unique<WorldState>(sim.snapshot());
        auto reveal = sim.world().dialogue.revealed;
        for (int i = 0; i < 8; ++i)
            sim.tick({});
        sim.restore(*snapshot);
        sim.truncateToCursor(snapshot->timelineCursor);
        check(sim.world().dialogue.revealed == reveal, "typewriter is snapshot state");
        Input confirm;
        confirm.confirm = true;
        sim.tick(confirm);
        sim.tick({});
        sim.tick(confirm);
        check(sim.world().dialogue.page == 1, "no-script dialogue page advance");
        Input trace;
        trace.trace = true;
        for (int i = 0; i < 80; ++i) {
            sim.tick(trace);
            if (sim.world().state != TimeState::Trace)
                break;
        }
        check(sim.world().dialogue.active == -1 && !sim.world().level.dialogues[0].triggered,
              "rewinding cancels trigger and restores progress");
        sim.tick({});
        check(sim.world().dialogue.active == 0, "dialogue can trigger again on new branch");
        for (int i = 0; i < 4; ++i) {
            applyJumpPreset(*w, i);
            check(w->jump.jump_velocity < 0, "jump preset");
        }
        for (int i = 1; i <= 6; ++i) {
            w->bulletPreset = i;
            w->aster.attack = {};
            planBulletPreset(*w);
            check(w->aster.attack.volleys.count == 3 && w->aster.attack.volleys[0].at >= .85f,
                  "bullet preset with warning");
        }
        // Every new authoring feature survives farthest history restore, then forward.
        *w = makeWorld();
        w->level = editor.level;
        w->level.dialogues.clear();
        w->aster.enabled = false;
        w->kino.invulnTimer = 1000;
        sim.reset(*w);
        for (int i = 0; i < 480; ++i)
            sim.tick({});
        for (int i = 0; i < 480; ++i)
            sim.tick(trace);
        sim.tick({});
        sim.tick({});
        check(sim.world().level.colliders.count == 5 && sim.world().level.lights.count == 1 &&
                  sim.world().level.layers[2].cells[3 * mapWidth + 3] == 1,
              "authored systems farthest rewind resume");
        std::cout << "PASS layers, exact circle/triangle/thin-wall sweeps, hulls, lamp undo, dialogue "
                     "replay, presets\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
