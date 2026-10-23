#include "echo/history.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace echo;
void check(bool v, const char* name) {
    if (!v)
        throw std::runtime_error(name);
}
int main(int, char**) {
    try {
        auto w = std::make_unique<WorldState>(makeWorld());
        History h(361 * sizeof(WorldState));
        h.prepare(true);
        check(h.capacity() == 361, "explicit budget holds complete six-second window");
        h.record(*w);
        for (int i = 0; i < 360; ++i) {
            w->kino.x += .25f;
            w->clock += fixedDt;
            h.record(*w);
        }
        restore(*w, h.at(0));
        check(std::abs(w->kino.x - 160) < .01f, "six-second snapshot returns to start");
        h.truncateToCursor(0);
        w->kino.x = 200;
        w->clock += fixedDt;
        h.record(*w);
        check(h.size() == 2 && h.at(1).kino.x == 200, "future truncation branches");
        auto s = std::make_unique<WorldState>(*w);
        s->kino.jumpHeld = true;
        s->kino.jumpWasCut = true;
        s->bullets.push({});
        s->bullets[0].px = 77.5f;
        s->bullets[0].py = 99.5f;
        s->jump.gravity = 123;
        s->rewind.history_seconds = 12;
        restore(*w, *s);
        check(w->bullets[0].px == 77.5f && w->kino.jumpWasCut && w->jump.gravity == 123,
              "all gameplay fields restored");
        History bounded(12 * sizeof(WorldState));
        bounded.prepare(true);
        for (int i = 0; i < 120; ++i) {
            w->clock += fixedDt;
            bounded.record(*w);
        }
        check(bounded.capacity() == 12 && bounded.size() == 12 &&
              bounded.allocatedBytes() == 12 * sizeof(WorldState) && bounded.seconds() < 1,
              "small budget caps memory and available history");
        History disabled;
        disabled.prepare(false);
        w->rewind.enabled = false;
        disabled.record(*w);
        check(disabled.allocatedBytes() == 0 && disabled.size() == 0, "disabled zero allocation");
        std::cout
            << "PASS step 1: six seconds, full restore, truncation, disabled allocation\nWorldState="
            << sizeof(WorldState) << " bytes\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
