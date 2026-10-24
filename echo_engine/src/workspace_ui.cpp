#include "echo/app.hpp"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cwctype>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <sstream>
namespace echo {
namespace {
constexpr UINT_PTR tooltipTimer = 0xEC01;
int act(WorkspaceAction a) {
    return int(a);
}
} // namespace
void App::workspaceTooltip(HWND h, const wchar_t* text) {
    if (!tooltips || !h)
        return;
    TOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = window;
    info.uId = reinterpret_cast<UINT_PTR>(h);
    info.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tooltips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}
void App::initWorkspace() {
    tooltips =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
    SendMessageW(tooltips, TTM_SETMAXTIPWIDTH, 0, 360);
    // Tools, layers and tiles moved to the drawn toolbar and dock; the scene settings window
    // keeps the hidden controls so existing command handlers continue to work.
    for (int id : {1, 2, 3, 4, 5, 6, 7, 8, 17, int(Layer), int(TileIndex), int(ImportTiles)})
        ShowWindow(controls[id], SW_HIDE);
    for (auto h = GetWindow(side, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        wchar_t text[128]{};
        GetWindowTextW(h, text, 128);
        if (std::wstring_view(text) == L"关卡工具")
            SetWindowTextW(h, L"基础手感 / 弹幕预设");
    }
    // Close the space vacated by the old always-visible tool grid.
    for (auto h = GetWindow(side, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        RECT r{};
        GetWindowRect(h, &r);
        POINT point{r.left, r.top};
        ScreenToClient(side, &point);
        if (point.y >= 300)
            MoveWindow(h, point.x, point.y - 144, r.right - r.left, r.bottom - r.top, TRUE);
    }
    loadTileAtlas();
    setTheme(darkTheme);
}
void App::saveWorkspaceSettings() {
    if (resources.published || !initialized)
        return;
    auto folder = resources.root / ".echo";
    std::filesystem::create_directories(folder);
    std::ostringstream out;
    // Width is stored in 96-DPI units so the preference survives display scaling changes.
    out << "ECHO_WORKSPACE_2 " << std::lround(workspaceWidth / uiScale) << " " << int(darkTheme) << " "
        << int(editorGrid) << " " << int(editorCollisions) << " " << int(debugMuteMusic) << "\n";
    writeTextAtomically(folder / "workspace.echo", out.str());
}
void App::loadWorkspaceSettings() {
    if (resources.published || !initialized)
        return;
    int width = 272, dark = 0, grid = 1, collisions = 1, mute = 1;
    auto path = resources.root / ".echo/workspace.echo";
    if (std::filesystem::exists(path)) {
        if (std::filesystem::file_size(path) > 4096) {
            message = "工作区偏好文件超过 4 KiB，已使用默认值";
        } else {
            std::ifstream in(path, std::ios::binary);
            std::string magic, extra;
            int nextWidth{}, nextDark{}, nextGrid{}, nextCollision{}, nextMute = 1;
            if (in >> magic >> nextWidth >> nextDark >> nextGrid >> nextCollision &&
                (magic == "ECHO_WORKSPACE_1" || magic == "ECHO_WORKSPACE_2") &&
                (magic == "ECHO_WORKSPACE_1" || bool(in >> nextMute)) && nextMute >= 0 &&
                nextMute <= 1 && nextWidth >= 256 && nextWidth <= 380 && nextDark >= 0 &&
                nextDark <= 1 && nextGrid >= 0 && nextGrid <= 1 && nextCollision >= 0 &&
                nextCollision <= 1 && !(in >> extra)) {
                width = nextWidth;
                dark = nextDark;
                grid = nextGrid;
                collisions = nextCollision;
                mute = nextMute;
            } else
                message = "工作区偏好无效，已使用默认值";
        }
    }
    workspaceWidth = ui::dp(float(width), uiScale);
    editorGrid = grid != 0;
    editorCollisions = collisions != 0;
    debugMuteMusic = mute != 0;
    updateAudioOutputPolicy();
    setTheme(dark != 0);
    layout();
}
void App::showSceneInspector() {
    if (!side)
        return;
    if (!verifyMode && !smoke)
        ShowWindow(side, SW_SHOW);
    SetForegroundWindow(side);
    RedrawWindow(side, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
WorkspaceModel App::workspaceModel() {
    WorkspaceModel m;
    m.dark = darkTheme;
    m.editing = editing;
    m.dirty = editor.dirty;
    m.canUndo = editor.undoCount() > 0;
    m.canRedo = editor.redoCount() > 0;
    m.mode = workspaceMode;
    int tool = editor.tool;
    m.tool = tool >= 1 && tool <= 4 ? 0 : tool >= 18 && tool <= 21 ? tool - 17 : -1;
    m.brush = tool == 4 ? 3 : std::clamp(int(editor.terrain) - 1, 0, 2);
    m.layer = editor.layer;
    m.tile = editor.tileIndex - 1;
    m.tileCount = tileAtlas && config.tileFrame > 0
                      ? std::min(16, tileAtlasW / config.tileFrame * (tileAtlasH / config.tileFrame))
                      : 16;
    m.project = wide(config.title);
    if (editMap >= 0 && editMap < int(config.maps.size()))
        m.map = pathFromUtf8(config.maps[editMap]).stem().wstring();
    m.status = statusText;
    m.openMenu = openMenu;
    m.inspector = inspectorOpen;
    if (workspaceMode == 1) {
        for (std::uint32_t i = 0; i < editor.level.events.count; ++i) {
            const auto& placed = editor.level.events[i];
            auto* definition = findEvent(editor.events, placed.id);
            m.items.push_back({definition ? wide(definition->name.c_str()) : L"未关联事件",
                               L"#" + std::to_wstring(placed.id)});
            if (int(i) == editor.selectedEvent)
                m.selectedItem = int(m.items.size()) - 1;
        }
    } else if (workspaceMode == 2) {
        for (std::size_t i = 0; i < objectLibrary.templates.size(); ++i) {
            const auto& t = objectLibrary.templates[i];
            m.items.push_back({wide(t.name.c_str()), L""});
            if (t.id == editor.objectTemplate)
                m.selectedItem = int(i);
        }
    } else if (workspaceMode == 3) {
        for (std::size_t i = 0; i < editor.barrage.emitters.size(); ++i) {
            const auto& e = editor.barrage.emitters[i];
            if (e.map != editMap)
                continue;
            m.items.push_back({wide(e.name.c_str()), L""});
            if (e.id == editor.selectedEmitter)
                m.selectedItem = int(m.items.size()) - 1;
        }
    } else if (workspaceMode == 4) {
        const wchar_t* names[] = {L"梯子", L"水域", L"传送带", L"弹簧", L"落下平台", L"升降 / 路线平台"};
        for (std::uint32_t i = 0; i < editor.level.features.count; ++i) {
            const auto& f = editor.level.features[i];
            m.items.push_back({names[std::clamp(int(f.kind), 0, 5)], L"#" + std::to_wstring(f.id)});
            if (int(i) == editor.selectedFeature)
                m.selectedItem = int(i);
        }
    }
    for (std::size_t i = 0; i < config.maps.size(); ++i) {
        m.maps.push_back(pathFromUtf8(config.maps[i]).stem().wstring());
        if (config.maps[i] == config.level)
            m.startMap = int(i);
    }
    m.editMap = editMap;
    if (inspectorOpen)
        m.properties = sceneProperties();
    return m;
}
void App::refreshWorkspace(bool) {
    if (resources.published || !initialized)
        return;
    workspaceMode = editor.tool == 17   ? 1
                    : editor.tool == 22 ? 2
                    : editor.tool == 23 ? 3
                    : editor.tool == 24 ? 4
                                        : 0;
    dockItems.clear();
    if (workspaceMode == 1)
        for (std::uint32_t i = 0; i < editor.level.events.count; ++i)
            dockItems.push_back(int(i));
    else if (workspaceMode == 2)
        for (std::size_t i = 0; i < objectLibrary.templates.size(); ++i)
            dockItems.push_back(int(i));
    else if (workspaceMode == 3) {
        for (std::size_t i = 0; i < editor.barrage.emitters.size(); ++i)
            if (editor.barrage.emitters[i].map == editMap)
                dockItems.push_back(int(i));
    } else if (workspaceMode == 4)
        for (std::uint32_t i = 0; i < editor.level.features.count; ++i)
            dockItems.push_back(int(i));
    RECT client{};
    GetClientRect(window, &client);
    chromeLayout = workspaceLayout(client.right, client.bottom, workspaceWidth, uiScale, inspectorOpen);
    chromeSurface.resize(client.right, client.bottom);
    buildWorkspace(chrome, chromeSurface, uiFonts, workspaceModel(), chromeLayout);
    InvalidateRect(window, nullptr, FALSE);
}
void App::paintWorkspace(HDC dc, const RECT* area) {
    RECT client{};
    GetClientRect(window, &client);
    chromeSurface.resize(client.right, client.bottom);
    const auto& t = ui::theme(darkTheme);
    TileAtlas atlas{tileAtlas, tileAtlasW, tileAtlasH, config.tileFrame};
    chromeSurface.fill({0, 0, client.right, client.bottom}, t.window);
    chrome.paint(chromeSurface, t, uiFonts, [&](ui::Painter& p, const ui::Node& n, ui::Box b) {
        paintWorkspaceCustom(p, n, b, chrome, t, uiFonts, atlas);
    });
    ui::Box region{0, 0, client.right, client.bottom};
    if (area)
        region = ui::Box{area->left, area->top, area->right - area->left, area->bottom - area->top}.intersect(region);
    chromeSurface.present(dc, region, region.x, region.y);
}
void App::setStatusText(const std::wstring& text) {
    if (resources.published) {
        SetWindowTextW(status, text.c_str());
        return;
    }
    if (text == statusText)
        return;
    statusText = text;
    if (!initialized)
        return;
    buildWorkspace(chrome, chromeSurface, uiFonts, workspaceModel(), chromeLayout);
    const auto& s = chromeLayout.status;
    RECT r{s.x, s.y, s.right(), s.bottom()};
    InvalidateRect(window, &r, FALSE);
}
bool App::workspaceWheel(POINT screen, int delta) {
    POINT p = screen;
    ScreenToClient(window, &p);
    if (!chromeLayout.dock.contains(p.x, p.y) && !chromeLayout.inspector.contains(p.x, p.y))
        return false;
    if (chrome.wheel(p.x, p.y, delta))
        InvalidateRect(window, nullptr, FALSE);
    return true;
}
bool App::workspaceMouse(UINT m, WPARAM w, LPARAM l, LRESULT& result) {
    result = 0;
    int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
    auto key = [](const ui::Node* n) { return n ? ui::Key{n->action, n->value} : ui::Key{}; };
    auto redraw = [&] { InvalidateRect(window, nullptr, FALSE); };
    switch (m) {
    case WM_MOUSEMOVE: {
        if (splittingWorkspace) {
            workspaceWidth = std::clamp(x, ui::dp(256, uiScale), ui::dp(380, uiScale));
            layout();
            return true;
        }
        if (sliderDrag.action) {
            if (const auto* n = chrome.find(sliderDrag.action, sliderDrag.value)) {
                float next = ui::sliderValue(*n, chrome.screenBox(*n), x, uiScale);
                if (next != sliderDragValue) {
                    sliderDragValue = next;
                    // Fog and wind are free to apply live; the rest create undo steps or
                    // reallocate history, so they apply once on release.
                    if (sliderDrag.value == Fog || sliderDrag.value == Wind)
                        applyProperty(sliderDrag.value, next);
                    else
                        refreshWorkspace();
                }
            }
            return true;
        }
        if (!chromeTracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            chromeTracking = true;
        }
        // Disabled controls still get hover so their tooltip can explain why.
        auto next = key(chrome.hit(x, y, true));
        if (!(next == chrome.hot)) {
            chrome.hot = next;
            chromeTip.hide();
            KillTimer(window, tooltipTimer);
            if (const auto* n = chrome.find(next.action, next.value);
                n && !n->tip.empty() && !verifyMode && !smoke)
                SetTimer(window, tooltipTimer, 450, nullptr);
            redraw();
        }
        return true;
    }
    case WM_MOUSELEAVE:
        chromeTracking = false;
        chromeTip.hide();
        KillTimer(window, tooltipTimer);
        if (!(chrome.hot == ui::Key{}) && GetCapture() != window) {
            chrome.hot = {};
            redraw();
        }
        return true;
    case WM_TIMER:
        if (w != tooltipTimer)
            return false;
        KillTimer(window, tooltipTimer);
        if (const auto* n = chrome.find(chrome.hot.action, chrome.hot.value);
            n && !n->tip.empty() && GetCapture() != window) {
            auto b = chrome.screenBox(*n);
            POINT at{b.x, b.bottom() + ui::dp(6, uiScale)};
            ClientToScreen(window, &at);
            if (n->kind == ui::Kind::Row) {
                // Rows are wide; anchor the tip at the pointer instead of the row's left edge.
                GetCursorPos(&at);
                at.y += ui::dp(20, uiScale);
            }
            chromeTip.show(window, at, n->tip, darkTheme, uiFonts);
        }
        return true;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        chromeTip.hide();
        KillTimer(window, tooltipTimer);
        const auto* n = chrome.hit(x, y);
        if (!n)
            return true;
        auto action = WorkspaceAction(n->action);
        int value = n->value;
        if (action == WorkspaceAction::Splitter) {
            finishBrush();
            splittingWorkspace = true;
            chrome.pressed = key(n);
            SetCapture(window);
            return true;
        }
        if (action == WorkspaceAction::Menu) {
            openWorkspaceMenu(value);
            return true;
        }
        if (n->kind == ui::Kind::Slider) {
            // Only the track band starts a drag; clicking the label line must not jump the value.
            if (y < ui::sliderTrack(chrome.screenBox(*n), uiScale).y - ui::dp(10, uiScale))
                return true;
            sliderDrag = key(n);
            sliderDragValue = ui::sliderValue(*n, chrome.screenBox(*n), x, uiScale);
            chrome.pressed = sliderDrag;
            SetCapture(window);
            refreshWorkspace();
            return true;
        }
        if (n->kind == ui::Kind::Dropdown) {
            applyProperty(value);
            return true;
        }
        if (m == WM_LBUTTONDBLCLK && (action == WorkspaceAction::Item || action == WorkspaceAction::MapRow)) {
            workspaceAction(n->action, value, true);
            return true;
        }
        chrome.pressed = key(n);
        SetCapture(window);
        redraw();
        return true;
    }
    case WM_LBUTTONUP: {
        if (splittingWorkspace) {
            splittingWorkspace = false;
            chrome.pressed = {};
            ReleaseCapture();
            saveWorkspaceSettings();
            redraw();
            return true;
        }
        if (sliderDrag.action) {
            auto drag = sliderDrag;
            sliderDrag = {};
            chrome.pressed = {};
            if (GetCapture() == window)
                ReleaseCapture();
            applyProperty(drag.value, sliderDragValue);
            return true;
        }
        auto pressed = chrome.pressed;
        chrome.pressed = {};
        if (GetCapture() == window)
            ReleaseCapture();
        redraw();
        if (pressed.action && key(chrome.hit(x, y)) == pressed)
            workspaceAction(pressed.action, pressed.value);
        return true;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if (splittingWorkspace) {
            splittingWorkspace = false;
            saveWorkspaceSettings();
        }
        if (!(chrome.pressed == ui::Key{})) {
            chrome.pressed = {};
            redraw();
        }
        if (sliderDrag.action) {
            // Interrupted drag (Alt+Tab, another capture): show the value that is actually applied.
            sliderDrag = {};
            refreshWorkspace();
        }
        return false;
    case WM_MOUSEWHEEL:
        return workspaceWheel({GET_X_LPARAM(l), GET_Y_LPARAM(l)}, GET_WHEEL_DELTA_WPARAM(w));
    case WM_SYSCOMMAND: {
        // Alt opens the drawn menu bar; Alt+letter picks a menu. Alt+Space keeps the system menu.
        if ((w & 0xFFF0) != SC_KEYMENU || l == ' ')
            return false;
        if (!editing)
            return true; // a stray Alt must not freeze gameplay in menu mode
        const wchar_t keys[] = L"FEVDGTH";
        int index = l == 0 ? 0 : -1;
        for (int i = 0; i < workspaceMenuCount && l; ++i)
            if (std::towupper(wchar_t(l)) == keys[i])
                index = i;
        if (index >= 0)
            openWorkspaceMenu(index, true);
        return true;
    }
    case WM_SETCURSOR: {
        if (LOWORD(l) != HTCLIENT)
            return false;
        POINT p{};
        GetCursorPos(&p);
        ScreenToClient(window, &p);
        const auto* n = chrome.hit(p.x, p.y);
        if (splittingWorkspace || (n && n->action == act(WorkspaceAction::Splitter))) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            result = TRUE;
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}
void App::setEditLayer(int layer) {
    layer = std::clamp(layer, 0, 2);
    if (controls.contains(Layer))
        SendMessageW(controls[Layer], CB_SETCURSEL, layer, 0);
    editor.layer = layer;
    if (mapToolsEditor) {
        SendMessageW(controls[MapLock], BM_SETCHECK, editor.layerLocked[editor.layer], 0);
        SendMessageW(controls[MapHide], BM_SETCHECK, editorHidden[editor.layer], 0);
    }
    refreshChrome();
    SetFocus(canvas);
}
void App::selectDockItem(int row, bool open) {
    if (row < 0 || row >= int(dockItems.size()))
        return;
    int index = dockItems[row];
    if (workspaceMode == 1 && index >= 0 && index < int(editor.level.events.count)) {
        finishBrush();
        editor.selectedEvent = index;
        auto r = editor.level.events[index].region;
        camera.logicalX = std::clamp(r.x - 640.f, 0.f, mapWidth * tileSize - 1280.f);
        camera.logicalY = std::clamp(r.y - 360.f, 0.f, std::max(0.f, mapHeight * tileSize - 720.f));
        camera.renderX = std::floor(camera.logicalX * pixelsPerUnit) / pixelsPerUnit;
        camera.renderY = std::floor(camera.logicalY * pixelsPerUnit) / pixelsPerUnit;
    } else if (workspaceMode == 2 && index >= 0 && index < int(objectLibrary.templates.size())) {
        objectTemplateIndex = index;
        editor.objectTemplate = objectLibrary.templates[index].id;
    } else if (workspaceMode == 3 && index >= 0 && index < int(editor.barrage.emitters.size())) {
        barrageEmitterIndex = index;
        editor.selectedEmitter = editor.barrage.emitters[index].id;
        Vec2 point;
        if (barrageEditorOrigin(editor.level, editor.barrage.emitters[index], point)) {
            camera.logicalX = std::clamp(point.x - 640.f, 0.f, mapWidth * tileSize - 1280.f);
            camera.renderX = std::floor(camera.logicalX * pixelsPerUnit) / pixelsPerUnit;
        }
    } else if (workspaceMode == 4 && index >= 0 && index < int(editor.level.features.count)) {
        finishBrush();
        editor.selectedFeature = index;
        auto r = editor.level.features[index].base;
        camera.logicalX = std::clamp(r.x - 640.f, 0.f, mapWidth * tileSize - 1280.f);
        camera.renderX = std::floor(camera.logicalX * pixelsPerUnit) / pixelsPerUnit;
    }
    if (open)
        workspaceCommand(WorkspaceItemEdit, 0);
    refreshChrome();
}
void App::workspaceAction(int action, int value, bool doubleClick) {
    switch (WorkspaceAction(action)) {
    case WorkspaceAction::Menu:
        openWorkspaceMenu(value);
        break;
    case WorkspaceAction::New:
        command(New);
        break;
    case WorkspaceAction::Open:
        command(Open);
        break;
    case WorkspaceAction::Save:
        command(Save);
        break;
    case WorkspaceAction::Undo:
        command(Undo);
        break;
    case WorkspaceAction::Redo:
        command(Redo);
        break;
    case WorkspaceAction::Mode: {
        const int modes[] = {int(editor.terrain), 17, WorkspaceObjects, WorkspaceBarrage, FeatureSelectTool};
        command(modes[std::clamp(value, 0, 4)]);
        break;
    }
    case WorkspaceAction::Tool:
        command(WorkspacePencil + std::clamp(value, 0, 4));
        break;
    case WorkspaceAction::Database:
        command(DatabaseEditor);
        break;
    case WorkspaceAction::Settings:
        command(WorkspaceInspector);
        break;
    case WorkspaceAction::Theme:
        command(darkTheme ? WorkspaceLightTheme : WorkspaceDarkTheme);
        break;
    case WorkspaceAction::Run:
        command(editing ? Play : Edit);
        break;
    case WorkspaceAction::Layer:
        setEditLayer(value);
        break;
    case WorkspaceAction::Tile:
        command(WorkspaceTileFirst + std::clamp(value, 0, 15));
        break;
    case WorkspaceAction::Terrain:
        command(WorkspaceTerrain + std::clamp(value, 0, 3));
        break;
    case WorkspaceAction::ImportTiles:
        command(ImportTiles);
        break;
    case WorkspaceAction::Item:
        selectDockItem(value, doubleClick);
        break;
    case WorkspaceAction::ItemEdit:
        workspaceCommand(WorkspaceItemEdit, 0);
        break;
    case WorkspaceAction::MapRow:
        if (doubleClick) {
            if (value != editMap)
                switchEditMap(value);
            showMapEditor();
        } else if (value != editMap) {
            try {
                switchEditMap(value);
            } catch (...) {
                refreshWorkspace(true);
                throw;
            }
        }
        break;
    case WorkspaceAction::MapNew:
        mapCommand(MapNew);
        refreshChrome();
        break;
    case WorkspaceAction::MapSettings:
        command(MapsEditor);
        break;
    case WorkspaceAction::Property:
        applyProperty(value);
        break;
    case WorkspaceAction::CloseInspector:
        command(WorkspaceInspector);
        break;
    case WorkspaceAction::Splitter:
    case WorkspaceAction::None:
        break;
    }
}
std::vector<ui::MenuItem> App::workspaceMenu(int index) {
    std::vector<ui::MenuItem> items;
    auto add = [&](int command, const wchar_t* text, const wchar_t* shortcut = L"", bool checked = false,
                   bool disabled = false) { items.push_back({text, shortcut, command, checked, disabled, false}); };
    auto sep = [&] { items.push_back({L"", L"", 0, false, false, true}); };
    bool undo = editing && editor.undoCount() > 0, redo = editing && editor.redoCount() > 0;
    switch (index) {
    case 0:
        add(New, L"新建工程…");
        add(Open, L"打开工程…");
        add(Save, L"保存", L"Ctrl+S");
        sep();
        add(ExportPublic, L"导出作品 · 公开资源…");
        add(ExportEncrypted, L"导出作品 · 加密资源…");
        break;
    case 1:
        add(Undo, L"撤销", L"Ctrl+Z", false, !undo);
        add(Redo, L"重做", L"Ctrl+Y", false, !redo);
        sep();
        add(MapCopy, L"复制图块选区", L"Ctrl+C");
        add(MapPaste, L"粘贴图块", L"Ctrl+V");
        add(MapClear, L"擦除选区", L"Delete");
        add(MapFlipH, L"水平翻转选区");
        add(MapFlipV, L"垂直翻转选区");
        break;
    case 2:
        add(WorkspaceInspector, L"场景设置面板", L"", inspectorOpen);
        add(MapToolsEditor, L"图层显示 / 锁定…");
        add(WorkspaceGrid, L"显示网格", L"", editorGrid);
        add(WorkspaceCollisions, L"显示碰撞", L"", editorCollisions);
        sep();
        add(WorkspaceLightTheme, L"浅色主题", L"", !darkTheme);
        add(WorkspaceDarkTheme, L"深色主题", L"", darkTheme);
        add(WorkspaceReset, L"恢复默认工作区布局");
        break;
    case 3:
        add(WorkspacePencil, L"画笔");
        add(WorkspaceRectangle, L"矩形填充");
        add(WorkspaceBucket, L"油漆桶");
        add(WorkspaceSelection, L"框选");
        add(WorkspacePick, L"吸管");
        sep();
        add(5, L"玩家出生点", L"5");
        add(6, L"Boss 出生点", L"6");
        add(7, L"移动平台", L"7");
        add(FeatureSelectTool, L"平台机关选择 / 拖动");
        add(8, L"选中 / 移动地形", L"8");
        add(9, L"场景灯光", L"9");
        add(15, L"对话区域");
        add(16, L"传送区域");
        sep();
        add(10, L"圆形碰撞", L"0");
        add(11, L"三角碰撞", L"-");
        add(12, L"正方形碰撞", L"=");
        add(13, L"矩形碰撞");
        add(14, L"绘制凸轮廓");
        add(MapAutoTile, L"四邻接自动地形");
        break;
    case 4:
        add(MapsEditor, L"地图与卷轴…");
        add(MobilityEditor, L"平台动作 / 机关预设…");
        add(EventEditor, L"地图 / 全局事件…");
        add(ObjectsEditor, L"对象与模板…");
        add(BarrageEditor, L"弹幕与波次…");
        add(DatabaseEditor, L"游戏数据库：物品 / 任务 / 开关 / 变量…");
        sep();
        add(AssetsEditor, L"资源管理…");
        add(AnimationsEditor, L"精灵与动画…");
        add(LayerEditor, L"前景 / 后景图片…");
        add(DialogEditor, L"对话与头像…");
        add(AudioEditor, L"BGM / SE…");
        add(TransitionEditor, L"切图动画…");
        sep();
        add(FocusEditor, L"判定 / 低速 / 采样…");
        add(RenderPixelPreset, L"应用像素清晰预设");
        add(CollisionEditor, L"碰撞分类…");
        add(FrameRateEditor, L"帧率与同步…");
        add(InputEditor, L"键位设置…");
        add(RewindRulesEditor, L"快速回溯规则…");
        break;
    case 5:
        add(Play, L"试玩", L"F5", false, !editing);
        add(Edit, L"返回编辑", L"F2", false, editing);
        add(DebuggerEditor, L"试玩调试器…");
        add(DebugMuteMusic, L"调试时静音 BGM（默认）", L"", debugMuteMusic);
        add(ProjectHealthEditor, L"工程检查 / 恢复草稿…");
        sep();
        add(Reload, L"重载资源 / Lua", L"F6");
        add(Stress, L"弹幕压力测试");
        break;
    default:
        add(WorkspaceGuide, L"绘图与制作快捷说明");
        break;
    }
    return items;
}
void App::openWorkspaceMenu(int index, bool keyboard) {
    chromeTip.hide();
    KillTimer(window, tooltipTimer);
    while (index >= 0 && index < workspaceMenuCount) {
        const auto* title = chrome.find(act(WorkspaceAction::Menu), index);
        if (!title)
            return;
        POINT at{title->box.x, title->box.bottom() + ui::dp(3, uiScale)};
        ClientToScreen(window, &at);
        openMenu = index;
        chrome.pressed = {};
        refreshWorkspace();
        UpdateWindow(window);
        int next = -1;
        int chosen = ui::popupMenu(
            window, at, workspaceMenu(index), darkTheme, uiFonts,
            [&](POINT screen) {
                ScreenToClient(window, &screen);
                const auto* n = chrome.hit(screen.x, screen.y);
                return n && n->action == act(WorkspaceAction::Menu) ? n->value : -1;
            },
            index, &next, keyboard);
        openMenu = -1;
        chrome.hot = {};
        refreshWorkspace();
        if (chosen) {
            command(chosen);
            return;
        }
        keyboard = next < -1;
        index = next == -2   ? (index + workspaceMenuCount - 1) % workspaceMenuCount
                : next == -3 ? (index + 1) % workspaceMenuCount
                             : next;
    }
}
void App::workspaceCommand(int id, int) {
    if (id >= WorkspaceFile && id <= WorkspaceHelp) {
        openWorkspaceMenu(id - WorkspaceFile);
        return;
    }
    if (id == WorkspaceGuide) {
        MessageBoxW(window,
                    L"地形：在左侧选图块，在地图上拖动铺设；右键擦除。\n"
                    L"矩形 / 油漆桶 / 框选 / 吸管在顶部工具栏。\n"
                    L"事件：在地图上单击部署，双击打开页条件与指令链。\n"
                    L"对象：在左侧选模板后放置；弹幕：拖动现有发射器。\n"
                    L"地图列表：单击切换（自动保存），双击编辑地图设置。\n"
                    L"滚轮平移地图；Ctrl+S 保存，Ctrl+Z / Y 撤销 / 重做。\n"
                    L"F5 试玩，F2 返回编辑，F6 重载。高级功能在「作品」/「工具」菜单。",
                    L"Echo Engine · 工作区", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (id == WorkspaceReset) {
        workspaceWidth = ui::dp(272, uiScale);
        setTheme(false);
        editorGrid = editorCollisions = true;
        if (mapToolsEditor) {
            SendMessageW(controls[MapGrid], BM_SETCHECK, TRUE, 0);
            SendMessageW(controls[MapCollisions], BM_SETCHECK, TRUE, 0);
        }
        layout();
        saveWorkspaceSettings();
        refreshChrome();
        return;
    }
    if (id == WorkspaceInspector) {
        inspectorOpen = !inspectorOpen;
        layout();
        SetFocus(canvas);
        return;
    }
    if (id == WorkspaceLightTheme || id == WorkspaceDarkTheme) {
        setTheme(id == WorkspaceDarkTheme);
        saveWorkspaceSettings();
        return;
    }
    if (id == WorkspaceGrid || id == WorkspaceCollisions) {
        bool& flag = id == WorkspaceGrid ? editorGrid : editorCollisions;
        flag = !flag;
        int old = id == WorkspaceGrid ? MapGrid : MapCollisions;
        if (mapToolsEditor)
            SendMessageW(controls[old], BM_SETCHECK, flag, 0);
        saveWorkspaceSettings();
        refreshChrome();
        return;
    }
    if (id == WorkspaceItemEdit) {
        if (workspaceMode == 1)
            showEventEditor(true);
        else if (workspaceMode == 2)
            showObjectsEditor();
        else if (workspaceMode == 3)
            showBarrageEditor();
        else if (workspaceMode == 4)
            mobilityCommand(MobilityEditor);
        return;
    }
    finishBrush();
    if (id == WorkspaceObjects || id == WorkspaceBarrage) {
        editing = true;
        editor.emitterPlacementArmed = false;
        editor.tool = id == WorkspaceObjects ? 22 : 23;
        if (editor.tool == 22 && !findObjectTemplate(objectLibrary, editor.objectTemplate) &&
            !objectLibrary.templates.empty())
            editor.objectTemplate = objectLibrary.templates.front().id;
    } else if (id >= WorkspacePencil && id <= WorkspacePick) {
        editing = true;
        editor.emitterPlacementArmed = false;
        editor.tool = id == WorkspacePencil ? int(editor.terrain) : 18 + (id - WorkspaceRectangle);
    } else if (id >= WorkspaceTerrain && id <= WorkspaceErase) {
        int terrain = id - WorkspaceTerrain + 1;
        if (terrain <= 3)
            editor.terrain = Tile(terrain);
        command(terrain);
        return;
    } else if (id >= WorkspaceTileFirst && id <= WorkspaceTileLast) {
        int tile = id - WorkspaceTileFirst;
        if (tileAtlas && tile >= tileAtlasW / config.tileFrame * (tileAtlasH / config.tileFrame))
            return;
        editor.tileIndex = tile + 1;
        if (controls.contains(TileIndex))
            SendMessageW(controls[TileIndex], CB_SETCURSEL, tile, 0);
        if (editor.tool < 1 || (editor.tool > 3 && editor.tool != 18 && editor.tool != 19))
            editor.tool = int(editor.terrain);
        editing = true;
    } else
        return;
    refreshWorkspace();
    refreshChrome();
    SetFocus(canvas);
}
} // namespace echo
