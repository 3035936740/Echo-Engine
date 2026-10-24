#include "echo/app.hpp"
#include <fstream>
#include <thread>
// Developer tour: `--ui-tour <folder>` opens the main window states and every settings window and
// saves real screen pixels (unlike --verify, whose captures repaint controls through printUi).
namespace echo {
namespace {
void saveScreen(RECT r, const std::filesystem::path& path) {
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0)
        return;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto old = SelectObject(dc, bitmap);
    BitBlt(dc, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    auto* pixels = static_cast<std::uint32_t*>(bits);
    for (int i = 0; i < w * h; ++i)
        pixels[i] |= 0xFF000000u;
    BITMAPFILEHEADER file{0x4d42, DWORD(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + w * h * 4), 0, 0,
                          DWORD(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(static_cast<const char*>(bits), std::streamsize(w) * h * 4);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}
} // namespace
void runUiTour(App& a, const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    std::ofstream log(output / "tour.txt");
    auto pump = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            a.render();
            Sleep(16);
        }
    };
    auto shoot = [&](HWND h, const std::string& name) {
        if (!h || !IsWindow(h)) {
            log << name << ": missing window\n";
            return;
        }
        RECT r{};
        GetWindowRect(h, &r);
        saveScreen(r, output / (name + ".bmp"));
        log << name << ": " << r.right - r.left << "x" << r.bottom - r.top << "\n";
        log.flush();
    };
    // Screenshot while a modal popup menu is open, then close it with Escape.
    auto shootMenu = [&](int menu, const std::string& name) {
        DWORD thread = GetCurrentThreadId();
        RECT r{};
        GetWindowRect(a.window, &r);
        std::thread helper([=] {
            Sleep(900);
            saveScreen(r, output / (name + ".bmp"));
            PostThreadMessageW(thread, WM_KEYDOWN, VK_ESCAPE, 0);
        });
        a.openWorkspaceMenu(menu);
        helper.join();
        log << name << "\n";
    };
    struct Page {
        int command;
        HWND App::*window;
        const char* name;
    };
    const Page pages[] = {{ObjectsEditor, &App::objectsEditor, "objects"},
                          {EventEditor, &App::eventEditor, "events"},
                          {BarrageEditor, &App::barrageEditor, "barrage"},
                          {DatabaseEditor, &App::databaseEditor, "database"},
                          {AssetsEditor, &App::assetsEditor, "assets"},
                          {AnimationsEditor, &App::animationsEditor, "animations"},
                          {MapsEditor, &App::mapEditor, "maps"},
                          {MobilityEditor, &App::mobilityEditor, "mobility"},
                          {LayerEditor, &App::layerEditor, "layers"},
                          {DialogEditor, &App::dialogEditor, "dialogue"},
                          {AudioEditor, &App::audioEditor, "audio"},
                          {TransitionEditor, &App::transitionEditor, "transition"},
                          {FocusEditor, &App::focusEditor, "focus"},
                          {CollisionEditor, &App::collisionEditor, "collision"},
                          {FrameRateEditor, &App::frameRateEditor, "framerate"},
                          {InputEditor, &App::inputEditor, "input"},
                          {RewindRulesEditor, &App::rewindRulesEditor, "rewind"},
                          {MapToolsEditor, &App::mapToolsEditor, "maptools"},
                          {ProjectHealthEditor, &App::healthEditor, "health"},
                          {TilePaletteEditor, &App::tilePalette, "tiles"}};
    SetWindowPos(a.window, HWND_TOP, 0, 0, 1440, 900, SWP_SHOWWINDOW);
    a.layout();
    pump(20);
    {
        RECT w{}, c{};
        GetWindowRect(a.window, &w);
        GetClientRect(a.window, &c);
        log << "main window " << w.left << "," << w.top << " " << w.right - w.left << "x" << w.bottom - w.top
            << " client " << c.right << "x" << c.bottom << "\n";
    }
    for (int dark = 0; dark < 2; ++dark) {
        std::string theme = dark ? "-dark" : "-light";
        a.command(dark ? WorkspaceDarkTheme : WorkspaceLightTheme);
        a.command(1);
        pump(10);
        shoot(a.window, "main-terrain" + theme);
        for (auto [command, name] : std::vector<std::pair<int, const char*>>{{17, "main-events"},
                                                                            {WorkspaceObjects, "main-objects"},
                                                                            {WorkspaceBarrage, "main-barrage"},
                                                                            {FeatureSelectTool, "main-features"}}) {
            a.command(command);
            pump(6);
            shoot(a.window, name + theme);
        }
        a.command(1);
        a.command(WorkspaceInspector);
        pump(8);
        shoot(a.window, "main-inspector" + theme);
        a.command(WorkspaceInspector);
        shootMenu(4, "menu-game" + theme);
        a.command(Play);
        pump(20);
        shoot(a.window, "main-playing" + theme);
        a.command(Edit);
        pump(4);
        for (const auto& page : pages) {
            a.command(page.command);
            pump(6);
            HWND h = a.*page.window;
            if (h && IsWindow(h)) {
                SetWindowPos(h, HWND_TOP, 8, 8, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
                pump(6);
            }
            shoot(h, std::string(page.name) + theme);
            if (h && IsWindow(h))
                SendMessageW(h, WM_CLOSE, 0, 0);
            pump(2);
        }
    }
    SetWindowPos(a.window, HWND_TOP, 0, 0, 1200, 720, SWP_SHOWWINDOW);
    a.layout();
    a.command(WorkspaceLightTheme);
    pump(10);
    shoot(a.window, "main-compact-light");
    log << "done\n";
}
} // namespace echo
