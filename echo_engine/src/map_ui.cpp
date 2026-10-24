#include "echo/app.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace echo {
void App::showMapEditor() {
    command(Edit);
    if (mapEditor && IsWindow(mapEditor)) {
        ShowWindow(mapEditor, SW_SHOW);
        SetForegroundWindow(mapEditor);
        return;
    }
    mapEditor =
        CreateWindowW(L"EchoEngineWindow", L"地图与卷轴设置",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      190, 130, 650, 480, window, nullptr, instance, this);
    auto label = [&](const wchar_t* s, int y) {
        control(L"STATIC", s, 0, 20, y, 600, 23, 0, mapEditor);
    };
    auto combo = [&](int id, int y, int chosen) {
        HWND h = control(L"COMBOBOX", L"", id, 20, y, 580, 240,
                         CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, mapEditor);
        for (auto& name : config.maps)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(name).c_str()));
        SendMessageW(h, CB_SETCURSEL, chosen, 0);
        SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
    };
    label(L"编辑地图 · 切换前自动保存当前地图", 18);
    combo(MapSelect, 47, editMap);
    control(L"BUTTON", L"新建地图", MapNew, 20, 90, 180, 30, WS_TABSTOP, mapEditor);
    control(L"BUTTON", L"设为作品起始地图", MapStart, 214, 90, 190, 30, WS_TABSTOP, mapEditor);
    control(L"BUTTON", L"摄像机限制在地图边界内", MapClamp, 20, 137, 560, 28, WS_TABSTOP, mapEditor);
    SendMessageW(controls[MapClamp], BM_SETCHECK, editor.level.cameraClamp, 0);
    label(L"传送门：先用画布「传送区域」工具拖动绘制 / 选择区域", 186);
    label(L"目标地图", 216);
    int portal = editor.selectedPortal;
    int target = portal >= 0 && portal < int(editor.level.portals.count)
                     ? editor.level.portals[portal].targetMap
                     : 0;
    combo(PortalTarget, 245, target);
    label(L"指定落点 X / Y（关闭使用出生点后生效）", 284);
    control(L"EDIT", L"160", PortalX, 20, 311, 280, 28, WS_BORDER | WS_TABSTOP, mapEditor);
    control(L"EDIT", L"592", PortalY, 314, 311, 286, 28, WS_BORDER | WS_TABSTOP, mapEditor);
    control(L"BUTTON", L"使用目标地图的玩家出生点", PortalSpawn, 20, 354, 580, 28, WS_TABSTOP,
            mapEditor);
    bool spawn = true;
    if (portal >= 0 && portal < int(editor.level.portals.count)) {
        auto& p = editor.level.portals[portal];
        spawn = p.useSpawn;
        setNumber(PortalX, p.destination.x);
        setNumber(PortalY, p.destination.y);
    }
    SendMessageW(controls[PortalSpawn], BM_SETCHECK, spawn, 0);
    control(L"BUTTON", L"应用传送设置", PortalApply, 20, 399, 580, 30, WS_TABSTOP, mapEditor);
}
void App::switchEditMap(int chosen) {
    command(Edit);
    if (chosen == editMap || chosen < 0 || chosen >= int(maps.size()))
        return;
    save();
    if (eventEditor && IsWindow(eventEditor))
        SendMessageW(eventEditor, WM_CLOSE, 0, 0);
    if (transitionEditor && IsWindow(transitionEditor))
        SendMessageW(transitionEditor, WM_CLOSE, 0, 0);
    editMap = chosen;
    editor.set(maps[editMap], eventMaps[editMap], commonEvents, editMap);
    initial_->level = editor.level;
    initial_->activeMap = editMap;
    initial_->kino.x = editor.level.spawn.x;
    initial_->kino.y = editor.level.spawn.y;
    simulation->setMaps(maps);
    simulation->setEvents(eventMaps, commonEvents);
    simulation->reset(*initial_, HistoryClearReason::MapSettings);
    camera.update(*initial_, 0, true);
    renderer.environment = editor.level.background;
    SendMessageW(controls[Environment], CB_SETCURSEL, renderer.environment, 0);
    SendMessageW(controls[MapClamp], BM_SETCHECK, editor.level.cameraClamp, 0);
    refreshChrome();
    if (mapEditor && IsWindow(mapEditor))
        SendMessageW(controls[MapSelect], CB_SETCURSEL, editMap, 0);
    refreshWorkspace(true);
}
void App::mapCommand(int id) {
    if (id == MapsEditor) {
        showMapEditor();
        return;
    }
    if (id == MapSelect) {
        switchEditMap(selection(MapSelect));
        return;
    }
    if (id == MapNew) {
        if (maps.size() >= 8)
            throw std::runtime_error("当前版本最多 8 张地图");
        save();
        std::string name = "levels/map_" + std::to_string(maps.size() + 1) + ".echo";
        auto path = resources.root / name;
        if (std::filesystem::exists(path))
            throw std::runtime_error("新地图的文件已存在");
        LevelState level;
        level.background = renderer.environment;
        for (int y = 20; y < mapHeight; ++y)
            for (int x = 0; x < mapWidth; ++x) {
                level.tiles[y * mapWidth + x] = Tile::Solid;
                level.layers[1].cells[y * mapWidth + x] = 1;
            }
        writeTextAtomically(path, saveLevel(level));
        config.maps.push_back(name);
        maps.push_back(level);
        eventMaps.push_back({});
        if (mapEditor && IsWindow(mapEditor))
            for (int combo : {MapSelect, PortalTarget})
                SendMessageW(controls[combo], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(name).c_str()));
        // The dock's "+" creates maps without the settings window, so switch directly.
        switchEditMap(int(maps.size()) - 1);
        save();
        message = "已新建地图";
        return;
    }
    if (id == MapStart) {
        config.level = config.maps[editMap];
        editor.dirty = true;
        save();
        message = "已设为起始地图";
        refreshWorkspace(true);
        refreshChrome();
        return;
    }
    if (id == MapClamp) {
        editor.begin();
        editor.level.cameraClamp = SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
        editor.end();
        editor.dirty = true;
        initial_->level.cameraClamp = editor.level.cameraClamp;
        camera.update(*initial_, 0, true);
        refreshChrome();
        return;
    }
    if (id == PortalApply) {
        int chosen = editor.selectedPortal;
        if (chosen < 0 || chosen >= int(editor.level.portals.count))
            throw std::runtime_error("请先在地图上绘制或选中一个传送区域");
        editor.begin();
        auto& p = editor.level.portals[chosen];
        p.targetMap = selection(PortalTarget);
        p.destination = {number(PortalX), number(PortalY)};
        p.useSpawn = SendMessageW(controls[PortalSpawn], BM_GETCHECK, 0, 0) == BST_CHECKED;
        editor.end();
        editor.dirty = true;
        message = "传送设置已应用";
        refreshChrome();
        return;
    }
}
} // namespace echo
