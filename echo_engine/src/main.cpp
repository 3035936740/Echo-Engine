#include "echo/frame_pacer.hpp"
#include "echo/app.hpp"
#include "echo/audio_state.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <objbase.h>
#include <shellapi.h>
#include <sstream>
#include <stdexcept>
namespace echo {
void App::updateAudioOutputPolicy() {
    audio.setMusicMuted(verifyMode || smoke ||
                        (!resources.published && debugMuteMusic && !previewAudio));
}
void App::render(float dt) {
    updateAudioOutputPolicy();
    dialogueUi();
    if (editing) {
        *visual = *initial_;
        visual->level = editor.level;
        visual->objects = {};
        visual->activeMap = editMap;
        enterObjectMap(*visual, objectLibrary);
        visual->kino.x = editor.level.spawn.x;
        visual->kino.y = editor.level.spawn.y;
        visual->aster.x = editor.level.bossSpawn.x;
        visual->aster.y = editor.level.bossSpawn.y;
        visual->clock = std::fmod(
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),
            10000);
        renderer.editorDark = darkTheme;
        renderer.editorGrid = editorGrid;
        renderer.editorCollisions = editorCollisions;
        renderer.editorHidden = editorHidden;
        renderer.selectionVisible = editor.selectionValid;
        renderer.selectionRect = editor.selection;
        visual->transition = {};
        if (transitionPreview) {
            float elapsed =
                float(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                          .count() -
                      transitionPreviewStart);
            auto& t = visual->transition;
            t.config = transitionPreviewConfig;
            if (elapsed < t.config.closeSeconds) {
                t.phase = TransitionPhase::Closing;
                t.elapsed = elapsed;
            } else if (elapsed < t.config.closeSeconds + t.config.holdSeconds) {
                t.phase = TransitionPhase::Covered;
                t.elapsed = elapsed - t.config.closeSeconds;
            } else if (elapsed < t.config.closeSeconds + t.config.holdSeconds + t.config.openSeconds) {
                t.phase = TransitionPhase::Opening;
                t.elapsed = elapsed - t.config.closeSeconds - t.config.holdSeconds;
            } else {
                transitionPreview = false;
            }
        }
        renderer.draw(*visual, camera, true, editor.tool, &editor.barrage, editor.selectedEmitter);
        if (previewAudio)
            audio.update(*audioPreview);
    } else {
        auto& w = simulation->world();
        camera.update(w, dt);
        *visual = w;
        // 30 Hz history is interpolated ONLY for presentation. Fixed hash lookup
        // keeps bullet matching O(N); never let interpolation become an N^2 pass.
        if (w.state == TimeState::Trace && w.rewind.record_hz == 30 &&
            w.timelineCursor + 1 < simulation->history().size()) {
            auto& next = simulation->history().at(w.timelineCursor + 1);
            double time = w.heldRewind.startTime - w.heldRewind.heldSeconds;
            float u =
                std::clamp(float((time - w.clock) / std::max(1e-6, next.clock - w.clock)), 0.0f, 1.0f);
            visual->kino.x = w.kino.x + (next.kino.x - w.kino.x) * u;
            visual->kino.y = w.kino.y + (next.kino.y - w.kino.y) * u;
            std::array<int, 4096> lookup;
            lookup.fill(-1);
            for (std::size_t i = 0; i < next.bullets.count; ++i) {
                auto slot = next.bullets[i].id & 4095u;
                while (lookup[slot] != -1)
                    slot = (slot + 1) & 4095u;
                lookup[slot] = int(i);
            }
            for (auto& b : visual->bullets) {
                auto slot = b.id & 4095u;
                while (lookup[slot] != -1 && next.bullets[lookup[slot]].id != b.id)
                    slot = (slot + 1) & 4095u;
                if (lookup[slot] != -1) {
                    auto& n = next.bullets[lookup[slot]];
                    b.x += (n.x - b.x) * u;
                    b.y += (n.y - b.y) * u;
                }
            }
            camera.update(*visual, 0, true);
        }
        renderer.environment = w.level.background;
        renderer.draw(*visual, camera);
        audio.update(w);
        audio.pause(debugPaused || w.audio.paused || !focused);
    }
}
void App::run() {
    using Clock = std::chrono::steady_clock;
    FramePacer pacer;
    FrameGate frameGate;
    double renderElapsed = 0;
    auto previous = Clock::now();
    autosaveAt = std::chrono::duration<double>(previous.time_since_epoch()).count();
    double accumulator = 0;
    bool running = true;
    int frame = 0;
    while (running) {
        auto frameStart = Clock::now();
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                running = false;
            if (!resources.published && msg.message == WM_KEYDOWN) {
                try {
                    if (eventShortcut(UINT(msg.wParam), (GetKeyState(VK_CONTROL) & 0x8000) != 0))
                        continue;
                    if (dialogKey(msg))
                        continue;
                } catch (const std::exception& e) {
                    message = e.what();
                    MessageBoxW(eventEditor, wide(message).c_str(), L"Echo Engine",
                                MB_OK | MB_ICONWARNING);
                    continue;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running)
            break;
        auto now = Clock::now();
        double dt = std::min(.1, std::chrono::duration<double>(now - previous).count());
        previous = now;
        if ((IsIconic(window) || !focused) && !smoke) {
            audio.pause(true);
            accumulator = 0;
            renderElapsed = 0;
            Sleep(10);
            continue;
        }
        auto hot = [&](int key) { return bool(GetAsyncKeyState(key) & 1); };
        // Invalid authoring values must keep the editor and unsaved project alive.
        try {
            if (!resources.published && frameRateEditor &&
                GetAncestor(GetFocus(), GA_ROOT) == frameRateEditor) {
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && hot('S')) {
                    frameRateCommand(FrameRateApply);
                    save();
                }
                if (hot(VK_F5)) {
                    frameRateCommand(FrameRateApply);
                    save();
                    SendMessageW(frameRateEditor, WM_CLOSE, 0, 0);
                    command(Play);
                }
            }
            if (!resources.published && focusEditor && GetAncestor(GetFocus(), GA_ROOT) == focusEditor) {
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && hot('S')) {
                    focusCommand(FocusApply);
                    save();
                }
                if (hot(VK_F5)) {
                    focusCommand(FocusApply);
                    save();
                    SendMessageW(focusEditor, WM_CLOSE, 0, 0);
                    command(Play);
                }
            }
        } catch (const std::exception& e) {
            message = e.what();
            MessageBoxW(window, wide(message).c_str(), L"Echo Engine", MB_OK | MB_ICONWARNING);
        }
        if (!resources.published && transitionEditor &&
            GetAncestor(GetFocus(), GA_ROOT) == transitionEditor) {
            if (GetAsyncKeyState(VK_F5) & 1) {
                command(TransitionApply);
                save();
                command(Play);
            }
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState('S') & 1)) {
                command(TransitionApply);
                save();
            }
        }
        if (!resources.published && eventEditor && GetAncestor(GetFocus(), GA_ROOT) == eventEditor) {
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
                if (hot('S')) {
                    eventCommand(EventApply);
                    save();
                }
            if (hot(VK_F5)) {
                eventCommand(EventApply);
                SendMessageW(eventEditor, WM_CLOSE, 0, 0);
                command(Play);
            }
        }
        if (!resources.published && GetAncestor(GetFocus(), GA_ROOT) == window) {
            if ((editing || std::find(config.keys.keys.begin(), config.keys.keys.end(), VK_F2) ==
                                config.keys.keys.end()) &&
                hot(VK_F2))
                command(Edit);
            if ((editing || std::find(config.keys.keys.begin(), config.keys.keys.end(), VK_F5) ==
                                config.keys.keys.end()) &&
                hot(VK_F5))
                command(Play);
            if ((editing || std::find(config.keys.keys.begin(), config.keys.keys.end(), VK_F6) ==
                                config.keys.keys.end()) &&
                hot(VK_F6))
                command(Reload);
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                if (hot('S'))
                    command(Save);
                if (GetFocus() == canvas && hot('Z'))
                    command(Undo);
                if (GetFocus() == canvas && hot('Y'))
                    command(Redo);
                if (editing && GetFocus() == canvas && hot('C'))
                    editor.copySelection();
                if (editing && GetFocus() == canvas && hot('V'))
                    editor.pasteSelection(int(brushX / tileSize), int(brushY / tileSize));
            }
            if (GetFocus() == canvas) {
                if (editing && hot(VK_DELETE))
                    editor.clearSelection();
                for (int k = 1; k <= 9; ++k)
                    if (hot('0' + k))
                        command(k);
                if (hot('0'))
                    command(10);
                if (hot(VK_OEM_MINUS))
                    command(11);
                if (hot(VK_OEM_PLUS))
                    command(12);
            }
        }
        if (!resources.published) {
            auto root = GetAncestor(GetFocus(), GA_ROOT);
            if (root == side || root == objectsEditor || root == assetsEditor ||
                root == animationsEditor || root == barrageEditor || root == barrageTargetEditor) {
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                    if (hot('S'))
                        save();
                    wchar_t type[32]{};
                    GetClassNameW(GetFocus(), type, 32);
                    if ((root == barrageEditor || root == barrageTargetEditor) &&
                        std::wstring_view(type) != L"Edit") {
                        if (hot('Z'))
                            command(Undo);
                        if (hot('Y'))
                            command(Redo);
                    }
                }
                if (hot(VK_F5)) {
                    command(Play);
                    SetFocus(canvas);
                }
            }
        }
        if (!resources.published && databaseEditor &&
            GetAncestor(GetFocus(), GA_ROOT) == databaseEditor &&
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) && hot('S'))
            save();
        double realSeconds = std::chrono::duration<double>(now.time_since_epoch()).count();
        if (!resources.published && editing && editor.dirty && realSeconds - autosaveAt >= 60) {
            autosaveAt = realSeconds;
            try {
                autosave();
            } catch (const std::exception& e) {
                message = std::string("自动恢复点保存失败：") + e.what();
            }
        }
        auto tickBegin = Clock::now();
        if (!editing) {
            accumulator = debugPaused ? 0 : accumulator + dt;
            if (debugStepRequested) {
                simulation->tick({});
                debugStepRequested = false;
            }

            Input input;
            auto keyboardFocus = GetFocus();
            if (focused && (keyboardFocus == canvas || IsChild(canvas, keyboardFocus))) {
                std::array<bool, 256> held{};
                for (int key = 8; key < 255; ++key)
                    held[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
                input = keyboardInput(
                    config.keys, held, simulation->world().focus.key, simulation->world().hurt.showKey,
                    simulation->world().mobility.dashKey, simulation->world().mobility.climbKey);
            }
            constexpr double fixedStep = 1.0 / 60;
            while (accumulator + 1e-12 >= fixedStep) {
                simulation->tick(input);
                accumulator = std::max(0., accumulator - fixedStep);
            }
        } else {
            accumulator = 0;
            if (previewAudio) {
                audioPreview->clock += dt;
                stepAudioState(*audioPreview, float(dt));
            }
        }
        if (!editing && !scripts->eventError().empty())
            message = scripts->eventError();
        tickMS = tickMS * .95 +
                 std::chrono::duration<double, std::milli>(Clock::now() - tickBegin).count() * .05;
        if (debuggerEditor &&
            std::chrono::duration<double>(now.time_since_epoch()).count() - debugRefreshAt >= .2) {
            refreshDebugger();
            debugRefreshAt = std::chrono::duration<double>(now.time_since_epoch()).count();
        }
        renderElapsed += dt;
        bool drew = frameGate.due(std::chrono::duration<double>(now.time_since_epoch()).count(),
                                  config.frameLimit);
        if (drew) {
            auto renderBegin = Clock::now();
            render(float(renderElapsed));
            renderElapsed = 0;
            renderMS =
                renderMS * .95 +
                std::chrono::duration<double, std::milli>(Clock::now() - renderBegin).count() * .05;
        } else {
            updateAudioOutputPolicy();
            if (editing) {
                if (previewAudio)
                    audio.update(*audioPreview);
            } else {
                audio.update(simulation->world());
                audio.pause(debugPaused || simulation->world().audio.paused || !focused);
            }
        }
        if (++frame % 15 == 0 || frame == 1) {
            auto& h = simulation->history();
            auto& w = simulation->world();
            std::ostringstream s;
            s << std::fixed << std::setprecision(2);
            auto keyLabel = [&](int key) {
                auto name = keyName(key);
                int size = WideCharToMultiByte(CP_UTF8, 0, name.data(), int(name.size()), nullptr, 0,
                                               nullptr, nullptr);
                std::string label(size, 0);
                WideCharToMultiByte(CP_UTF8, 0, name.data(), int(name.size()), label.data(), size,
                                    nullptr, nullptr);
                return label;
            };
            if (resources.published) {
                if (w.state == TimeState::Hit)
                    s << "受击：" << (w.rewind.enabled ? "按住 " : "")
                      << keyLabel(config.keys.keys[w.rewind.enabled ? 6 : 7])
                      << (w.rewind.enabled ? " 撤回" : " 重开");
                else if (!w.rewind.enabled)
                    s << keyLabel(config.keys.keys[0]) << " / " << keyLabel(config.keys.keys[1])
                      << " 跑  ·  " << keyLabel(config.keys.keys[2]) << " 跳  ·  本作品已关闭回溯";
                else
                    s << keyLabel(config.keys.keys[0]) << " / " << keyLabel(config.keys.keys[1])
                      << " 跑  ·  " << keyLabel(config.keys.keys[2]) << " 跳  ·  "
                      << keyLabel(config.keys.keys[5]) << " 快退  ·  " << keyLabel(config.keys.keys[6])
                      << " 回溯   " << h.seconds() << "s";
            } else if (editing) {
                const char* tools[] = {"选择",     "画笔",     "单向平台", "尖刺",      "擦除",
                                       "玩家出生", "Boss出生", "移动平台", "移动地形",  "灯光",
                                       "圆形碰撞", "三角碰撞", "方形碰撞", "矩形碰撞",  "绘制凸轮廓",
                                       "对话区域", "传送区域", "地图事件", "矩形填充",  "油漆桶",
                                       "框选",     "吸管",     "对象",     "弹幕发射器", "平台机关"};
                const char* layers[] = {"后景", "世界", "前景"};
                s << pathUtf8(pathFromUtf8(config.maps[editMap]).stem()) << " · " << mapWidth << " × "
                  << mapHeight << " 格"
                  << "   |   " << layers[editor.layer] << "   |   "
                  << tools[std::clamp(editor.tool, 0, 24)];
                if (workspaceMode == 0)
                    s << "   |   图块 " << editor.tileIndex;
                if (editor.layerLocked[editor.layer])
                    s << "   |   图层已锁定";
                if (editor.dirty)
                    s << "   |   未保存";
                if (!message.empty())
                    s << "   |   " << message;
            } else {
                s << "试玩   |   回溯 " << h.seconds() << "/" << w.rewind.history_seconds << "s"
                  << "   |   历史 " << h.usedBytes() / 1048576.0 << " MiB / 预留 "
                  << h.allocatedBytes() / 1048576.0 << " MiB   |   弹幕 " << w.bullets.count << "/"
                  << maxBullets;
                if (debuggerEditor)
                    s << "   |   CPU " << tickMS << "ms / 绘制 " << renderMS << "ms";
                if (w.state == TimeState::Hit)
                    s << "   |   受击：" << keyLabel(config.keys.keys[w.rewind.enabled ? 6 : 7])
                      << (w.rewind.enabled ? " 撤回" : " 重开");
                else if (!w.rewind.enabled)
                    s << "   |   回溯已关闭";
            }
            setStatusText(wide(s.str()));
        }
        if (smoke && drew && ++smokeFrames >= 8) {
            renderer.capture(smokePath);
            auto uiPath = smokePath;
            uiPath.replace_extension(".bmp");
            captureUi(uiPath);
            running = false;
        }
        pacer.wait(frameStart, config.frameLimit ? std::max(60, config.frameLimit) : 0);
    }
    audio.pause(true);
}
} // namespace echo
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    std::filesystem::path diagnostics;
    try {
        using namespace echo;
        SetProcessDPIAware();
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto app = std::make_unique<App>();
        app->instance = instance;
        wchar_t path[32768]{};
        GetModuleFileNameW(nullptr, path, 32768);
        app->executable = path;
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> args(argv, argv + argc);
        LocalFree(argv);
        for (std::size_t i = 1; i + 1 < args.size(); ++i)
            if (args[i] == L"--verify" || args[i] == L"--diagnostics" || args[i] == L"--ui-tour")
                diagnostics = args[i + 1];
        if (!app->resources.openEmbedded(app->executable)) {
            auto game = app->executable.parent_path() / "game";
            if (std::filesystem::exists(game / "project.echo")) {
                app->resources.root = game;
                app->resources.published = true;
            } else
                app->resources.root = app->executable.parent_path() / "project";
        }
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == L"--project" && i + 1 < args.size()) {
                app->resources.root = args[++i];
                app->resources.published = false;
            } else if (args[i] == L"--smoke") {
                app->smoke = true;
                if (i + 1 < args.size() && args[i + 1].find(L"--") != 0)
                    app->smokePath = args[++i];
            }
        }
        if (!std::filesystem::exists(app->resources.root / "project.echo") && !app->resources.published)
            throw std::runtime_error("找不到 project.echo：请用 --project <文件夹> 指定工程");
        for (std::size_t i = 1; i + 1 < args.size(); ++i)
            if (args[i] == L"--export-public" || args[i] == L"--export-encrypted") {
                app->resources.exportGame(app->executable, args[i + 1],
                                          args[i] == L"--export-encrypted");
                return 0;
            }
        bool tour = std::find(args.begin(), args.end(), L"--ui-tour") != args.end();
        app->verifyMode = !diagnostics.empty() && !tour; // the tour needs visible windows
        app->loadProject();
        app->editing = !app->resources.published && app->executable.stem() == L"EchoEngine";
        for (auto& arg : args)
            if (arg == L"--editor")
                app->editing = true;
        app->ui();
        for (std::size_t i = 1; i + 1 < args.size(); ++i)
            if (args[i] == L"--verify") {
                app->verify(args[i + 1]);
                return 0;
            } else if (args[i] == L"--ui-tour") {
                runUiTour(*app, args[i + 1]);
                return 0;
            }
        app->run();
        return 0;
    } catch (const std::exception& e) {
        if (!diagnostics.empty()) {
            std::filesystem::create_directories(diagnostics);
            std::ofstream(diagnostics / "verification-error.txt") << e.what();
            return 1;
        }
        MessageBoxW(nullptr, echo::wide(e.what()).c_str(), L"Echo Engine", MB_ICONERROR);
        return 1;
    }
}
