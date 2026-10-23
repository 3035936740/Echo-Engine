#include "echo/app.hpp"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <sstream>
namespace echo {
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
    workspace = CreateWindowW(L"EchoEngineWindow", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 72,
                              272, 760, window, nullptr, instance, this);
    tooltips =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
    SendMessageW(tooltips, TTM_SETMAXTIPWIDTH, 0, 360);
    auto button = [&](int id, const wchar_t* text, const wchar_t* tip, HWND parent = nullptr) {
        auto h = control(L"BUTTON", text, id, 0, 0, 60, 30, WS_TABSTOP, parent);
        workspaceTooltip(h, tip);
    };
    const wchar_t* menus[] = {L"文件", L"编辑", L"视图", L"绘图", L"作品", L"工具", L"帮助"};
    for (int i = 0; i < 7; ++i)
        button(WorkspaceFile + i, menus[i], menus[i]);
    for (auto [id, text, tip] : std::vector<std::tuple<int, const wchar_t*, const wchar_t*>>{
             {New, L"＋", L"新建工程"},
             {Open, L"▱", L"打开工程"},
             {Save, L"▣", L"保存工程 · Ctrl+S"},
             {Undo, L"↶", L"撤销 · Ctrl+Z"},
             {Redo, L"↷", L"重做 · Ctrl+Y"},
             {1, L"地形", L"绘制地形与图块"},
             {17, L"事件",
              L"部署 / 拖动事件 · "
              L"双击编辑指令链"},
             {MapsEditor, L"设置", L"地图 / 传送与卷轴设置"},
             {Edit, L"■ 编辑", L"返回编辑 · F2"},
             {Play, L"▶ 试玩", L"从当前地图即时试玩 · F5"}}) {
        SetParent(controls[id], id == MapsEditor ? workspace : window);
        SetWindowTextW(controls[id], text);
        ShowWindow(controls[id], SW_SHOW);
        workspaceTooltip(controls[id], tip);
    }
    button(WorkspaceObjects, L"对象", L"选择模板并在地图部署对象");
    button(WorkspaceBarrage, L"弹幕",
           L"选择发射器并在地图拖动 / "
           L"双击编辑");
    button(WorkspacePencil, L"✎", L"画笔 · 在地图拖动绘制");
    button(WorkspaceRectangle, L"▭", L"矩形填充 · 拖动定大小");
    button(WorkspaceBucket, L"▨", L"油漆桶 · 点击填充连通地形");
    button(WorkspaceSelection, L"⌗", L"框选 · Ctrl+C / V；Delete擦除");
    button(WorkspacePick, L"⌖", L"吸管 · 点击读取图块");
    button(WorkspaceInspector, L"场景设置", L"环境 / 回溯 / 光照 / 移动平台参数");
    for (int i = 0; i < 16; ++i)
        button(WorkspaceTileFirst + i, L"", L"选图块后在地图铺设；右键擦除", workspace);
    const wchar_t* terrain[] = {L"实心", L"单向", L"尖刺", L"擦除"};
    for (int i = 0; i < 4; ++i)
        button(WorkspaceTerrain + i, terrain[i], i == 1 ? L"只能从上方落下的单向平台" : terrain[i],
               workspace);
    button(WorkspaceItemEdit, L"编辑选中项…",
           L"打开当前事件 / 对象 / "
           L"发射器的详细表单",
           workspace);
    workspaceItems = control(L"LISTBOX", L"", WorkspaceItems, 10, 76, 252, 234,
                             LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, workspace);
    SendMessageW(workspaceItems, LB_SETITEMHEIGHT, 0, 28);
    workspaceTip = control(L"STATIC", L"", 0, 10, 352, 252, 28, 0, workspace);
    SendMessageW(workspaceTip, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    SetParent(controls[Layer], workspace);
    SetParent(controls[ImportTiles], workspace);
    SetWindowTextW(controls[ImportTiles], L"导入");
    MoveWindow(controls[ImportTiles], workspaceWidth - 62, 6, 52, 26, TRUE);
    workspaceTooltip(controls[ImportTiles], L"导入图块图集 · 当前支持16种图块");
    ShowWindow(controls[TileIndex], SW_HIDE);
    for (int id = 2; id <= 8; ++id)
        ShowWindow(controls[id], SW_HIDE);
    // The old inspector remains available as a separate settings window.
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
    mapTree = control(WC_TREEVIEWW, L"", WorkspaceMaps, 6, 412, 260, 320,
                      TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | WS_TABSTOP,
                      workspace);
    TreeView_SetItemHeight(mapTree, 28);
    loadTileAtlas();
    setTheme(darkTheme);
}
void App::saveWorkspaceSettings() {
    if (resources.published || !workspace)
        return;
    auto folder = resources.root / ".echo";
    std::filesystem::create_directories(folder);
    std::ostringstream out;
    out << "ECHO_WORKSPACE_2 " << workspaceWidth << " " << int(darkTheme) << " " << int(editorGrid)
        << " " << int(editorCollisions) << " " << int(debugMuteMusic) << "\n";
    writeTextAtomically(folder / "workspace.echo", out.str());
}
void App::loadWorkspaceSettings() {
    if (resources.published || !workspace)
        return;
    int width = 272, dark = 0, grid = 1, collisions = 1, mute = 1;
    auto path = resources.root / ".echo/workspace.echo";
    if (std::filesystem::exists(path)) {
        if (std::filesystem::file_size(path) > 4096) {
            message = "Workspace preferences exceed 4 KiB; defaults used";
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
                message = "Invalid workspace preferences; defaults used";
        }
    }
    workspaceWidth = width;
    editorGrid = grid != 0;
    editorCollisions = collisions != 0;
    debugMuteMusic = mute != 0;
    updateAudioOutputPolicy();
    SendMessageW(controls[ThemeMode], CB_SETCURSEL, dark, 0);
    setTheme(dark != 0);
    if (initialized)
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
void App::refreshWorkspace(bool mapsChanged) {
    if (!workspace || resources.published)
        return;
    int mode = editor.tool == 17   ? 1
               : editor.tool == 22 ? 2
               : editor.tool == 23 ? 3
               : editor.tool == 24 ? 4
                                   : 0;
    workspaceMode = mode;
    for (int i = 0; i < 16; ++i) {
        auto h = controls[WorkspaceTileFirst + i];
        ShowWindow(h, mode == 0 ? SW_SHOW : SW_HIDE);
        EnableWindow(h,
                     !tileAtlas || i < tileAtlasW / config.tileFrame * (tileAtlasH / config.tileFrame));
        InvalidateRect(h, nullptr, FALSE);
    }
    for (int i = 0; i < 4; ++i)
        ShowWindow(controls[WorkspaceTerrain + i], mode == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(controls[Layer], mode == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(controls[ImportTiles], mode == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(workspaceItems, mode ? SW_SHOW : SW_HIDE);
    ShowWindow(controls[WorkspaceItemEdit], mode ? SW_SHOW : SW_HIDE);
    SetWindowTextW(workspaceTip, mode == 1 ? L"单击部署 / 拖动 · "
                                             L"双击编辑指令链"
                                 : mode == 2 ? L"选模板后部署 · 双击实例编辑"
                                 : mode == 3 ? L"拖动现有发射器 · 双击编辑波次"
                                 : mode == 4 ? L"选机关 / 拖动 · 双击编辑属性"
                                             : L"选图块 → 拖动铺设 · 右键擦除");
    refreshingWorkspace = true;
    if (mode) {
        SendMessageW(workspaceItems, LB_RESETCONTENT, 0, 0);
        int selected = -1;
        auto add = [&](const std::wstring& text, LPARAM value) {
            int row = int(
                SendMessageW(workspaceItems, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
            SendMessageW(workspaceItems, LB_SETITEMDATA, row, value);
            return row;
        };
        if (mode == 1) {
            for (std::uint32_t i = 0; i < editor.level.events.count; ++i) {
                const auto& placed = editor.level.events[i];
                auto* definition = findEvent(editor.events, placed.id);
                auto name = L"#" + std::to_wstring(placed.id) + L"  " +
                            (definition ? wide(definition->name.c_str()) : L"未关联事件");
                int row = add(name, i);
                if (int(i) == editor.selectedEvent)
                    selected = row;
            }
        } else if (mode == 2) {
            for (std::size_t i = 0; i < objectLibrary.templates.size(); ++i) {
                auto& t = objectLibrary.templates[i];
                int row = add(wide(t.name.c_str()), LPARAM(i));
                if (t.id == editor.objectTemplate)
                    selected = row;
            }
        } else if (mode == 4) {
            const wchar_t* names[] = {L"梯子", L"水域",     L"传送带",
                                      L"弹簧", L"落下平台", L"升降 / 路线平台"};
            for (std::uint32_t i = 0; i < editor.level.features.count; ++i) {
                const auto& f = editor.level.features[i];
                auto label = std::wstring(names[int(f.kind)]) + L" #" + std::to_wstring(f.id);
                int row = add(label, i);
                if (int(i) == editor.selectedFeature)
                    selected = row;
            }
        } else {
            for (std::size_t i = 0; i < editor.barrage.emitters.size(); ++i) {
                const auto& e = editor.barrage.emitters[i];
                if (e.map != editMap)
                    continue;
                int row = add(wide(e.name.c_str()), LPARAM(i));
                if (e.id == editor.selectedEmitter)
                    selected = row;
            }
        }
        SendMessageW(workspaceItems, LB_SETCURSEL, selected, 0);
    }
    if (mapsChanged) {
        SendMessageW(mapTree, WM_SETREDRAW, FALSE, 0);
        TreeView_DeleteAllItems(mapTree);
        auto title = wide(config.title);
        TVINSERTSTRUCTW insert{};
        insert.hParent = TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
        insert.item.pszText = title.data();
        insert.item.lParam = -1;
        insert.item.stateMask = TVIS_EXPANDED;
        insert.item.state = TVIS_EXPANDED;
        auto root = TreeView_InsertItem(mapTree, &insert);
        for (std::size_t i = 0; i < config.maps.size(); ++i) {
            auto name = pathFromUtf8(config.maps[i]).stem().wstring();
            auto label = std::to_wstring(i + 1) + L"  " + name +
                         (config.maps[i] == config.level ? L"  [起始]" : L"");
            insert.hParent = root;
            insert.item.pszText = label.data();
            insert.item.lParam = LPARAM(i);
            insert.item.mask = TVIF_TEXT | TVIF_PARAM;
            auto item = TreeView_InsertItem(mapTree, &insert);
            if (int(i) == editMap)
                TreeView_SelectItem(mapTree, item);
        }
        TreeView_Expand(mapTree, root, TVE_EXPAND);
        SendMessageW(mapTree, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(mapTree, nullptr, TRUE);
    }
    refreshingWorkspace = false;
    InvalidateRect(workspace, nullptr, FALSE);
}
LRESULT App::workspaceNotify(LPARAM value) {
    auto* header = reinterpret_cast<NMHDR*>(value);
    if (!header || header->hwndFrom != mapTree || refreshingWorkspace)
        return 0;
    if (header->code == NM_CUSTOMDRAW) {
        auto* draw = reinterpret_cast<NMTVCUSTOMDRAW*>(value);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
            return CDRF_NOTIFYITEMDRAW;
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            bool selected =
                reinterpret_cast<HTREEITEM>(draw->nmcd.dwItemSpec) == TreeView_GetSelection(mapTree);
            draw->clrText = selected ? (darkTheme ? RGB(227, 230, 239) : RGB(37, 92, 145))
                                     : (darkTheme ? RGB(227, 230, 239) : RGB(44, 52, 66));
            draw->clrTextBk = selected ? (darkTheme ? RGB(55, 77, 105) : RGB(224, 236, 250))
                                       : (darkTheme ? RGB(43, 48, 64) : RGB(255, 255, 255));
            return CDRF_DODEFAULT;
        }
        return CDRF_DODEFAULT;
    }
    if (header->code == TVN_SELCHANGEDW) {
        auto* change = reinterpret_cast<NMTREEVIEWW*>(value);
        int chosen = int(change->itemNew.lParam);
        if (chosen >= 0 && chosen < int(maps.size()) && chosen != editMap) {
            try {
                switchEditMap(chosen);
            } catch (...) {
                refreshWorkspace(true);
                throw;
            }
        }
    } else if (header->code == NM_DBLCLK)
        showMapEditor();
    return 0;
}
void App::workspaceCommand(int id, int notification) {
    if (id >= WorkspaceFile && id <= WorkspaceHelp) {
        auto menu = CreatePopupMenu();
        auto add = [&](int command, const wchar_t* text) {
            AppendMenuW(menu, MF_STRING, command, text);
        };
        auto sep = [&] { AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); };
        if (id == WorkspaceFile) {
            add(New, L"新建工程…");
            add(Open, L"打开工程…");
            add(Save, L"保存\tCtrl+S");
            sep();
            add(ExportPublic, L"导出作品 · 公开资源…");
            add(ExportEncrypted, L"导出作品 · 加密资源…");
        } else if (id == WorkspaceEdit) {
            add(Undo, L"撤销\tCtrl+Z");
            add(Redo, L"重做\tCtrl+Y");
            sep();
            add(MapCopy, L"复制图块选区\tCtrl+C");
            add(MapPaste, L"粘贴图块\tCtrl+V");
            add(MapClear, L"擦除选区\tDelete");
            add(MapFlipH, L"水平翻转选区");
            add(MapFlipV, L"垂直翻转选区");
        } else if (id == WorkspaceView) {
            add(WorkspaceInspector, L"场景设置…");
            add(MapToolsEditor, L"图层显示 / 锁定…");
            AppendMenuW(menu, MF_STRING | (editorGrid ? MF_CHECKED : 0), WorkspaceGrid, L"显示网格");
            AppendMenuW(menu, MF_STRING | (editorCollisions ? MF_CHECKED : 0), WorkspaceCollisions,
                        L"显示碰撞");
            sep();
            add(WorkspaceLightTheme, L"浅色主题");
            add(WorkspaceDarkTheme, L"深色主题");
            add(WorkspaceReset, L"恢复默认工作区布局");
        } else if (id == WorkspaceDraw) {
            add(WorkspacePencil, L"画笔");
            add(WorkspaceRectangle, L"矩形填充");
            add(WorkspaceBucket, L"油漆桶");
            add(WorkspaceSelection, L"框选");
            add(WorkspacePick, L"吸管");
            sep();
            add(5, L"玩家出生点");
            add(6, L"Boss出生点");
            add(7, L"移动平台");
            add(FeatureSelectTool, L"平台机关选择 / 拖动");
            add(8, L"选中 / 移动地形");
            add(9, L"场景灯光");
            add(15, L"对话区域");
            add(16, L"传送区域");
            sep();
            add(10, L"圆形碰撞");
            add(11, L"三角碰撞");
            add(12, L"正方形碰撞");
            add(13, L"矩形碰撞");
            add(14, L"绘制凸轮廓");
            add(MapAutoTile, L"四邻接自动地形");
        } else if (id == WorkspaceGame) {
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
        } else if (id == WorkspaceTools) {
            add(Play, L"试玩\tF5");
            add(Edit, L"返回编辑\tF2");
            add(DebuggerEditor, L"试玩调试器…");
            AppendMenuW(menu, MF_STRING | (debugMuteMusic ? MF_CHECKED : 0), DebugMuteMusic,
                        L"调试时静音 BGM（默认）");
            add(ProjectHealthEditor, L"工程检查 / 恢复草稿…");
            sep();
            add(Reload, L"重载资源 / Lua\tF6");
            add(Stress, L"弹幕压力测试");
        } else
            add(WorkspaceGuide, L"绘图与制作快捷说明");
        RECT anchor{};
        GetWindowRect(controls[id], &anchor);
        int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, anchor.left, anchor.bottom, 0,
                                    window, nullptr);
        DestroyMenu(menu);
        if (chosen)
            command(chosen);
        return;
    }
    if (id == WorkspaceGuide) {
        MessageBoxW(window,
                    L"地形：选左侧图块，在地图拖"
                    L"动；右键擦除。\n"
                    L"矩形 / 油漆桶 / 框选 / "
                    L"吸管在顶部绘图工具栏。\n"
                    L"事件：点击部署，双击打开页"
                    L"条件与指令链。\n"
                    L"对象：从左侧选模板后放置；"
                    L"弹幕：拖动现有发射器。\n"
                    L"地图树：单击切换（自动保存"
                    L"），双击编辑地图设置。\n"
                    L"滚轮平移地图；Ctrl+S保存，Ctrl+Z/"
                    L"Y撤销/重做。\n"
                    L"F5试玩，F2返回编辑，F6重载。高"
                    L"级功能在作品 / 工具菜单。",
                    L"Echo Engine · 工作区", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (id == WorkspaceReset) {
        workspaceWidth = 272;
        SendMessageW(controls[ThemeMode], CB_SETCURSEL, 0, 0);
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
        showSceneInspector();
        return;
    }
    if (id == WorkspaceLightTheme || id == WorkspaceDarkTheme) {
        SendMessageW(controls[ThemeMode], CB_SETCURSEL, id == WorkspaceDarkTheme, 0);
        command(ThemeMode, CBN_SELCHANGE);
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
    if (id == WorkspaceItems) {
        if (refreshingWorkspace || (notification != LBN_SELCHANGE && notification != LBN_DBLCLK))
            return;
        int row = int(SendMessageW(workspaceItems, LB_GETCURSEL, 0, 0));
        if (row < 0)
            return;
        int index = int(SendMessageW(workspaceItems, LB_GETITEMDATA, row, 0));
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
        }
        if (workspaceMode == 4 && index >= 0 && index < int(editor.level.features.count)) {
            finishBrush();
            editor.selectedFeature = index;
            auto r = editor.level.features[index].base;
            camera.logicalX = std::clamp(r.x - 640.f, 0.f, mapWidth * tileSize - 1280.f);
            camera.renderX = std::floor(camera.logicalX * pixelsPerUnit) / pixelsPerUnit;
        }
        if (notification == LBN_DBLCLK)
            workspaceCommand(WorkspaceItemEdit, 0);
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
