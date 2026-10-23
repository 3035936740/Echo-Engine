#include "echo/input_bindings.hpp"
#include "echo/resources.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace echo;
static void check(bool v, const char* s) {
    if (!v)
        throw std::runtime_error(s);
}
static void fresh(WorldState& w) {
    w = makeWorld();
}
int main(int argc, char** argv) {
    try {
        KeyBindings b;
        std::array<bool, 256> held{};
        held['E'] = true;
        auto input = keyboardInput(b, held, 16, 72);
        check(input.confirm && !input.fast && !input.trace, "E confirms and never rewinds");
        held['E'] = false;
        held['R'] = true;
        check(keyboardInput(b, held, 16, 72).fast, "default fast R");
        b.keys[4] = 'K';
        b.keys[5] = 'E';
        validateKeys(b);
        held.fill(false);
        held['E'] = true;
        input = keyboardInput(b, held, 16, 72);
        check(input.fast && !input.confirm, "custom E rewind cannot confirm");
        held.fill(false);
        held['K'] = true;
        check(keyboardInput(b, held, 16, 72).confirm, "custom dialogue key");
        check(writeKeys(readKeys(writeKeys(b))) == writeKeys(b), "keyboard serialization");
        for (const auto* text : {"65 68 32 40 69 69 84 13 1", "65 68 32 40 69 82 84 13 2", "65 68",
                                 "65 68 32 40 69 82 84 13 1 extra"}) {
            bool rejected = false;
            try {
                readKeys(text);
            } catch (...) {
                rejected = true;
            }
            check(rejected, "invalid/ambiguous mappings rejected");
        }
        if (argc > 1) {
            Resources r;
            r.root = std::filesystem::path(argv[1]) / "dialogue-key-config";
            std::filesystem::create_directories(r.root);
            writeTextAtomically(r.root / "project.echo", "keyboard_bindings=" + writeKeys(b) + "\n");
            check(writeKeys(r.config().keys) == writeKeys(b), "project config carries custom bindings");
        }
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        w->aster.enabled = false;
        w->jump.gravity = 0;
        w->level.dialogues.clear();
        w->level.events.clear();
        w->level.portals.clear();
        w->level.platforms.clear();
        w->level.colliders.clear();
        w->level.tiles.fill(Tile::Empty);
        DialogueDefinition d;
        d.text.set("First page.\nSecond page.");
        w->level.dialogues.push(d);
        w->dialogue.active = 0;
        w->gameplayClockPaused = true;
        for (int hz : {30, 60, 120}) {
            w->rewind.record_hz = hz;
            Simulation sim(16ull * 1024 * 1024);
            sim.reset(*w);
            held.fill(false);
            held['K'] = true;
            input = keyboardInput(b, held, 16, 72);
            sim.tick(input);
            check(sim.world().dialogue.page == 0 && sim.world().dialogue.revealed == 11,
                  "first press completes typewriter");
            for (int i = 0; i < 30; ++i)
                sim.tick(input);
            check(sim.world().dialogue.page == 0 && sim.world().state != TimeState::Fast &&
                      sim.world().state != TimeState::Trace,
                  "held confirm never repeats or rewinds");
            sim.tick({});
            sim.tick(input);
            check(sim.world().dialogue.page == 1 && sim.world().dialogue.active == 0,
                  "next press advances page");
            sim.tick({});
            sim.tick(input);
            sim.tick({});
            sim.tick(input);
            check(sim.world().dialogue.active < 0 && !sim.world().gameplayClockPaused,
                  "last page closes");
            Input trace;
            trace.trace = true;
            sim.tick(trace);
            check(sim.world().state == TimeState::Trace && sim.world().dialogue.active == 0,
                  "rewind restores dialogue progress");
            sim.tick({});
            sim.tick(input);
            sim.tick({});
            sim.tick(input);
            check(sim.world().state != TimeState::Trace, "dialogue resumes after rewind");
        }
        // Fill the complete configured window, restore its far end, branch, and keep playing.
        for (int hz : {30, 60, 120}) {
            w->rewind.record_hz = hz;
            Simulation far(384ull * 1024 * 1024);
            far.reset(*w);
            for (int t = 0; t < 360; ++t)
                far.tick({});
            Input trace;
            trace.trace = true;
            for (int t = 0; t < 361 && (t == 0 || far.world().state == TimeState::Trace); ++t)
                far.tick(trace);
            check(far.world().clock < .001 && far.world().dialogue.page == 0,
                  "dialogue restores at the far end of the full six-second window");
            far.tick({});
            far.tick(input);
            check(far.world().dialogue.active == 0 && far.world().dialogue.revealed >= 11,
                  "confirm continues after a complete history truncation");
        }
        {
            Simulation noClear;
            fresh(*w);
            w->aster.enabled = false;
            w->level.dialogues.clear();
            Bullet bullet;
            bullet.x = w->kino.x + 30;
            bullet.y = w->kino.y + 20;
            w->bullets.clear();
            w->bullets.push(bullet);
            noClear.reset(*w);
            noClear.world().clock = 1;
            noClear.world().kino.x += 10;
            noClear.record();
            Input fast;
            fast.fast = true;
            for (int i = 0; i < 20 && (i == 0 || noClear.world().state == TimeState::Fast); ++i)
                noClear.tick(fast);
            check(noClear.world().state != TimeState::Fast && noClear.world().bullets.count == 1 &&
                      noClear.world().bullets[0].x == bullet.x,
                  "rewind exit preserves nearby restored bullets by default");
            noClear.tick({});
        }
        fresh(*w);
        w->aster.enabled = false;
        w->jump.gravity = 0;
        w->level.dialogues.clear();
        w->level.dialogues.push(d);
        w->dialogue.active = 0;
        w->gameplayClockPaused = true;
        w->rewind.enabled = false;
        Simulation off;
        off.reset(*w);
        held.fill(false);
        held['K'] = true;
        off.tick(keyboardInput(b, held, 16, 72));
        check(off.history().size() == 0 && off.world().dialogue.revealed == 11,
              "dialogue confirm works without rewind allocation");
        std::cout << "Dialogue input and configurable keys passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
