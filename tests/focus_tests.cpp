#include "echo/focus.hpp"
#include "echo/script.hpp"
#include "echo/simulation.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace echo;
static void check(bool v, const char *text) {
    if (!v)
        throw std::runtime_error(text);
}
static void fresh(WorldState &w) { w = makeWorld(); }
static bool hit(WorldState &w, Vec2 a, Vec2 b, float radius = 0, bool box = false,
                std::uint8_t mask = 1) {
    float entry;
    return playerHurtSweep(w, w.kino, w.hurt, a, b, radius, box, mask, entry);
}
int main(int argc, char **argv) {
    try {
        auto w = std::make_unique<WorldState>();
        fresh(*w);
        w->kino.x = w->kino.y = 0;
        w->hurt.source = HurtSource::Custom;
        auto &r = w->hurt.regions[0];
        r.anchorX = r.anchorY = 0;
        r.radius = 5;
        check(hit(*w, {-20, 6}, {20, 6}, 1), "circle tangent included");
        w->hurt.touching = false;
        check(!hit(*w, {-20, 6}, {20, 6}, 1), "strict tangent excluded");
        w->hurt.touching = true;
        check(!hit(*w, {6, 6}, {6, 6}, 1, true), "circle versus box rejects square corner");
        check(hit(*w, {5, 0}, {5, 0}, 1, true), "circle versus box exact side");
        w->hurt.method = HurtMethod::Overlap;
        check(!hit(*w, {-20, 0}, {20, 0}), "discrete mode intentionally misses crossed area");
        w->hurt.method = HurtMethod::Swept;
        r.shape = HurtShape::Rectangle;
        r.width = 10;
        r.height = 4;
        check(hit(*w, {-20, 3}, {20, 3}, 1, true), "box overlap tangency");
        check(!hit(*w, {6, 3}, {6, 3}, 1), "rounded rectangle projectile corner excluded");
        r.shape = HurtShape::Ellipse;
        r.width = 10;
        r.height = 6;
        check(hit(*w, {-20, 0}, {20, 0}), "ellipse center swept");
        check(!hit(*w, {4, 2.9f}, {4, 2.9f}), "ellipse corner not a box");
        r.shape = HurtShape::Capsule;
        r.width = 4;
        r.height = 12;
        r.angle = 0;
        check(hit(*w, {-20, 0}, {20, 0}), "capsule sweep");
        check(!hit(*w, {2, 6}, {2, 6}), "capsule rounded cap excludes corner");
        r.angle = 1.57079632679f;
        check(hit(*w, {-20, 0}, {20, 0}), "rotated capsule sweep");
        r.shape = HurtShape::Polygon;
        r.angle = 0;
        r.vertices.push({0, -5});
        r.vertices.push({5, 5});
        r.vertices.push({-5, 5});
        validateHurt(w->hurt);
        check(hit(*w, {-20, 0}, {20, 0}), "convex polygon sweep");
        check(!hit(*w, {4, -4}, {4, -4}), "polygon excludes bounding corner");
        w->hurt.count = 2;
        w->hurt.regions[1] = HurtRegion{};
        w->hurt.regions[1].anchorX = w->hurt.regions[1].anchorY = 0;
        w->hurt.regions[1].offsetX = 30;
        check(hit(*w, {30, 0}, {30, 0}), "second region union");
        w->hurt.regions[1].mask = 4;
        check(!hit(*w, {30, 0}, {30, 0}), "per-region bullet mask");
        check(playerHurtRect(*w, {29, -1, 2, 2}, 4), "melee selects second region");
        check(!playerHurtRect(*w, {29, -1, 2, 2}, 2), "contact category filtered");
        w->hurt.regions[1].focusOnly = true;
        check(!playerHurtRect(*w, {29, -1, 2, 2}, 4), "focus-only region inactive");
        w->kino.focusActive = true;
        check(playerHurtRect(*w, {29, -1, 2, 2}, 4), "focus-only region active");
        w->kino.facing = -1;
        auto mirrored = hurtCenter(w->kino, w->hurt, w->hurt.regions[1]);
        check(mirrored.x == w->kino.width - 30, "mirror relative to actor width");
        w->kino.facing = 1;
        auto encoded = writeHurt(w->hurt);
        check(writeHurt(readHurt(encoded)) == encoded, "all shape/display fields serialized");
        bool rejected = false;
        try {
            readHurt(encoded + " garbage");
        } catch (...) {
            rejected = true;
        }
        check(rejected, "trailing record rejected");
        fresh(*w);
        w->kino.x = w->kino.y = 0;
        w->hurt.source = HurtSource::Custom;
        w->hurt.regions[0].anchorX = w->hurt.regions[0].anchorY = 0;
        w->hurt.regions[0].radius = 2;
        w->kino.invulnTimer = 0;
        w->aster.enabled = true;
        w->aster.resting = false;
        w->aster.attack.end = 10;
        w->aster.attack.zones.clear();
        Zone zone;
        zone.x = zone.y = -1;
        zone.w = zone.h = 2;
        zone.start = 0;
        zone.end = 10;
        w->aster.attack.zones.push(zone);
        w->hurt.regions[0].mask = hurtContact;
        stepBoss(*w, fixedDt, nullptr);
        check(w->state != TimeState::Hit, "blue seal respects damage category mask");
        w->hurt.regions[0].mask = hurtBullets;
        stepBoss(*w, fixedDt, nullptr);
        check(w->state == TimeState::Hit, "blue seal uses custom hit region away from original belly");
        w->state = TimeState::Free;
        w->gameplayClockPaused = false;
        w->kino.hp = 1;
        w->kino.invulnTimer = 1;
        stepBoss(*w, fixedDt, nullptr);
        check(w->state != TimeState::Hit, "blue seal respects invulnerability");
        auto c = w->focus;
        c.enabled = true;
        c.mode = FocusMode::Toggle;
        c.when = FocusWhen::Switch;
        c.gateSwitch = 2;
        c.map = 1;
        c.speedRatio = .25f;
        check(writeFocus(readFocus(writeFocus(c))) == writeFocus(c), "focus settings roundtrip");
        w->focus = c;
        w->activeMap = 1;
        w->events.switches[2] = true;
        w->kino.grounded = true;
        w->kino.previousFocus = false;
        stepFocus(*w, true, false, true);
        check(w->kino.focusActive && w->kino.focusLatched, "toggle rising edge activates");
        stepFocus(*w, true, false, true);
        check(w->kino.focusActive, "held toggle does not oscillate");
        stepFocus(*w, false, false, true);
        stepFocus(*w, true, false, true);
        check(!w->kino.focusActive, "next edge deactivates");
        stepFocus(*w, false, false, true);
        stepFocus(*w, true, false, true);
        w->events.switches[2] = false;
        stepFocus(*w, false, false, true);
        check(!w->kino.focusActive && w->kino.focusLatched, "gate suspends toggle state");
        w->events.switches[2] = true;
        stepFocus(*w, false, true, false);
        check(!w->kino.focusActive && !w->kino.focusLatched, "dialogue cancels focus ownership");
        fresh(*w);
        w->aster.enabled = false;
        w->focus.enabled = true;
        w->focus.speedRatio = .25f;
        w->kino.invulnTimer = 10000;
        w->jump.move_speed = 200;
        w->kino.vx = 200;
        Input input;
        input.move = 1;
        input.focus = true;
        stepFocus(*w, true, false, true);
        stepPhysics(*w, input, fixedDt);
        check(w->kino.vx <= 50.001f, "instant focus caps prior fast velocity");
        w->focus.enabled = false;
        stepFocus(*w, true, false, true);
        check(!w->kino.focusActive, "disable overrides held key");
        for (int hz : {30, 60, 120}) {
            fresh(*w);
            w->aster.enabled = false;
            w->kino.invulnTimer = 10000;
            w->rewind.record_hz = hz;
            w->focus.enabled = true;
            w->focus.mode = FocusMode::Toggle;
            w->hurt.source = HurtSource::Custom;
            w->hurt.regions[0].radius = 2;
            Simulation sim(192ull * 1024 * 1024);
            sim.reset(*w);
            Input act;
            act.focus = true;
            sim.tick(act);
            act.focus = false;
            for (int i = 0; i < 420; ++i)
                sim.tick(act);
            for (bool fast : {false, true}) {
                Input rewind;
                rewind.fast = fast;
                rewind.trace = !fast;
                int guard = 0;
                do {
                    sim.tick(rewind);
                    ++guard;
                } while (
                    (sim.world().state == TimeState::Fast || sim.world().state == TimeState::Trace) &&
                    guard < 1000);
                sim.tick({});
                check(guard < 1000, "farthest rewind terminates");
                check(sim.world().hurt.regions[0].radius == 2, "hurt size survives farthest rewind");
                sim.tick(act);
                check(sim.world().kino.focusLatched == sim.world().kino.focusActive,
                      "toggle snapshot edge consistent after branch");
                for (int i = 0; i < 420; ++i)
                    sim.tick(act);
            }
        }
        if (argc > 1) {
            auto root = std::filesystem::path(argv[1]) / "focus-lua-test";
            std::filesystem::create_directories(root);
            Resources resources;
            resources.root = root;
            writeTextAtomically(root / "project.echo", "scripts=focus.lua\n");
            writeTextAtomically(
                root / "focus.lua",
                "engine.configure_focus{enabled=true,mode='toggle',when='switch',switch=3,speed_ratio=."
                "3}; "
                "engine.configure_hurt{source='custom',show='always',method='swept'};engine.set_hurt_"
                "region(1,{shape='polygon',vertices={{0,-6},{6,6},{-6,6}},mask=1});engine.set_hurt_"
                "region(2,{shape='capsule',width=4,height=12,offset_x=10,focus_only=true})");
            ScriptRuntime script;
            script.load(resources, *w);
            check(w->focus.enabled && w->focus.gateSwitch == 2 && w->hurt.count == 2 &&
                      w->hurt.regions[0].vertices.count == 3,
                  "structured Lua custom hit regions");
        }
        std::cout << "PASS focus gates/toggle/instant speed, five shapes, touching/strict, "
                     "CCD/discrete, multi-region masks, all Hz farthest T/R and Lua\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
