#include "echo/frame_pacer.hpp"
#include "echo/resources.hpp"
#include "echo/simulation.hpp"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace echo;
static void check(bool v, const char* text) {
    if (!v)
        throw std::runtime_error(text);
}
static void fresh(WorldState& w) { w = makeWorld(); }
static LRESULT CALLBACK messageProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(
            h, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
    if (msg == WM_APP + 1) {
        *reinterpret_cast<bool*>(GetWindowLongPtrW(h, GWLP_USERDATA)) = true;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
int main(int argc, char** argv) {
    try {
        using Clock = std::chrono::steady_clock;
        FramePacer pacer;
        for (int fps : {30, 60, 90, 120, 144, 75, 1}) {
            auto start = Clock::now();
            pacer.wait(start, fps);
            double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            check(elapsed >= .8 / fps, "limiter must wait through frame deadline");
        }
        auto immediate = Clock::now();
        pacer.wait(immediate, 0);
        check(std::chrono::duration<double>(Clock::now() - immediate).count() < .1,
              "unlimited has no frame period wait");
        bool handled = false;
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = messageProc;
        wc.lpszClassName = L"EchoFrameTest";
        RegisterClassW(&wc);
        auto window = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                    wc.hInstance, &handled);
        check(window != nullptr, "message test window");
        PostMessageW(window, WM_APP + 1, 0, 0);
        pacer.wait(Clock::now(), 30);
        check(handled, "UI messages remain responsive while pacing");
        DestroyWindow(window);
        auto initial = std::make_unique<WorldState>();
        fresh(*initial);
        initial->aster.enabled = false;
        initial->kino.invulnTimer = 10000;
        initial->level.tiles.fill(Tile::Empty);
        initial->level.platforms.clear();
        initial->level.colliders.clear();
        initial->level.dialogues.clear();
        initial->level.portals.clear();
        initial->level.events.clear();
        initial->jump.gravity = 0;
        auto expected = std::make_unique<WorldState>();
        bool first = true;
        for (int fps : {1, 30, 60, 90, 120, 144, 75, 1000}) {
            Simulation sim(64ull * 1024 * 1024);
            sim.reset(*initial);
            double accumulator = 0;
            FrameGate gate;
            int draws = 0, host = std::max(60, fps);
            int steps = 0;
            Input input;
            input.move = 1;
            for (int frame = 0; frame < host * 6; ++frame) {
                draws += gate.due(double(frame) / host, fps);
                accumulator += 1. / host;
                while (accumulator + 1e-12 >= 1. / 60) {
                    sim.tick(input);
                    ++steps;
                    accumulator = std::max(0., accumulator - 1. / 60);
                }
            }
            check(draws == fps * 6, "render gate limits frames while simulation is serviced at >=60 Hz");
            check(steps == 360, "render cap still executes 360 simulation steps in six seconds");
            if (first) {
                std::memcpy(expected.get(), &sim.world(), sizeof(WorldState));
                first = false;
            } else
                check(std::memcmp(expected.get(), &sim.world(), sizeof(WorldState)) == 0,
                      "all gameplay/snapshot fields independent of render cap");
        }
        if (argc > 1) {
            auto root = std::filesystem::path(argv[1]) / "frame-config-test";
            std::filesystem::create_directories(root);
            Resources r;
            r.root = root;
            for (int fps : {0, 1, 30, 60, 90, 120, 144, 75, 1000}) {
                writeTextAtomically(root / "project.echo",
                                    "frame_limit=" + std::to_string(fps) + "\nvsync=0\n");
                check(r.config().frameLimit == fps && !r.config().vsync, "frame config serialization");
            }
            for (auto text : {"frame_limit=60.5\n", "frame_limit=60garbage\n", "frame_limit=-1\n",
                              "frame_limit=1001\n", "frame_limit=nan\n", "frame_limit=\n",
                              "vsync=2\n", "vsync=1extra\n", "texture_filter=1.5\n",
                              "upscale_filter=6\n", "anti_aliasing=2garbage\n"}) {
                bool invalid = false;
                writeTextAtomically(root / "project.echo", text);
                try {
                    r.config();
                } catch (...) {
                    invalid = true;
                }
                check(invalid, "invalid frame/sampling configuration rejected entirely");
            }
        }
        std::cout << "PASS frame deadlines/unlimited/responsive messages, 360 fixed ticks at eight "
                     "rates with exact WorldState equality, config validation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
