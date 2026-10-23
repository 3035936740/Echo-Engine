#include "echo/app.hpp"
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <windowsx.h>
namespace echo {
std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring result(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), result.data(), n);
    return result;
}
LRESULT CALLBACK App::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto a = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        a = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
    }
    if (!a)
        return DefWindowProcW(h, m, w, l);
    try {
        if (m == WM_ERASEBKGND)
            return 1;
        if ((m == WM_PAINT || m == WM_PRINTCLIENT) && h == a->dialogWindow) {
            a->paintDialogue(h, m == WM_PRINTCLIENT ? reinterpret_cast<HDC>(w) : nullptr);
            return 0;
        }
        if (m == WM_PRINTCLIENT && h != a->canvas) {
            a->paintChrome(h, reinterpret_cast<HDC>(w));
            return 0;
        }
        if (m == WM_PAINT && h != a->canvas) {
            a->paintChrome(h);
            return 0;
        }
        if (m == WM_DRAWITEM) {
            a->drawControl(*reinterpret_cast<DRAWITEMSTRUCT*>(l));
            return TRUE;
        }
        if (m == WM_MEASUREITEM) {
            auto item = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
            item->itemHeight = 25;
            return TRUE;
        }
        if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX ||
            m == WM_CTLCOLORBTN)
            return a->colorControl(reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l));
        if (m == WM_GETMINMAXINFO && h == a->window) {
            auto info = reinterpret_cast<MINMAXINFO*>(l);
            info->ptMinTrackSize = a->resources.published ? POINT{656, 399} : POINT{1200, 720};
            return 0;
        }
        if (m == WM_CLOSE && h == a->side) {
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        if (m == WM_CLOSE) {
            if (h == a->window && a->eventEditor && IsWindow(a->eventEditor))
                a->eventCommand(EventApply);
            if (h == a->window && !a->resources.published && a->editor.dirty) {
                int result = MessageBoxW(h, L"保存当前项目的修改？", L"Echo Engine",
                                         MB_YESNOCANCEL | MB_ICONQUESTION);
                if (result == IDCANCEL)
                    return 0;
                if (result == IDYES)
                    a->save();
            }
            if (h == a->snapshotCompareEditor) {
                a->snapshotCompareEditor = nullptr;
                a->snapshotA.reset();
                a->snapshotB.reset();
                a->snapshotReport.reset();
                a->snapshotFilterText.clear();
            }
            if (h == a->collisionEditor)
                a->collisionEditor = nullptr;
            if (h == a->rewindRulesEditor)
                a->rewindRulesEditor = nullptr;
            if (h == a->inputEditor)
                a->inputEditor = nullptr;
            if (h == a->frameRateEditor)
                a->frameRateEditor = nullptr;
            if (h == a->focusEditor)
                a->focusEditor = nullptr;
            if (h == a->databaseEditor)
                a->databaseEditor = nullptr;
            if (h == a->mapToolsEditor)
                a->mapToolsEditor = nullptr;
            if (h == a->debuggerEditor) {
                if (a->snapshotCompareEditor)
                    SendMessageW(a->snapshotCompareEditor, WM_CLOSE, 0, 0);
                a->debuggerEditor = nullptr;
                a->debugPaused = false;
                a->debugStepRequested = false;
                a->renderer.debugCollisions = false;
            }
            if (h == a->healthEditor)
                a->healthEditor = nullptr;
            if (h == a->objectsEditor)
                a->objectsEditor = nullptr;
            if (h == a->barrageTargetEditor)
                a->barrageTargetEditor = nullptr;
            if (h == a->barrageEditor) {
                if (a->barrageTargetEditor)
                    SendMessageW(a->barrageTargetEditor, WM_CLOSE, 0, 0);
                a->barrageEditor = nullptr;
                a->barragePreviewPlaying = false;
                a->barragePreviewWorld.reset();
            }
            if (h == a->animationsEditor) {
                a->animationsEditor = nullptr;
                if (a->animationBitmap) {
                    DeleteObject(a->animationBitmap);
                    a->animationBitmap = nullptr;
                }
            }
            if (h == a->assetsEditor) {
                a->assetsEditor = nullptr;
                if (a->assetThumbnail) {
                    DeleteObject(a->assetThumbnail);
                    a->assetThumbnail = nullptr;
                }
            }
            if (h == a->dialogEditor)
                a->dialogEditor = nullptr;
            if (h == a->transitionEditor) {
                a->transitionEditor = nullptr;
                a->transitionPreview = false;
            }
            if (h == a->mapEditor)
                a->mapEditor = nullptr;
            if (h == a->layerEditor)
                a->layerEditor = nullptr;
            if (h == a->mobilityEditor)
                a->mobilityEditor = nullptr;
            if (h == a->choiceEditor) {
                if (IsWindowVisible(h))
                    a->choiceRead();
                a->choiceEditor = nullptr;
            }
            if (h == a->eventEditor) {
                if (a->choiceEditor)
                    SendMessageW(a->choiceEditor, WM_CLOSE, 0, 0);
                a->eventCommand(EventApply);
                a->eventEditor = nullptr;
            }
            if (h == a->tilePalette)
                a->tilePalette = nullptr;
            if (h == a->audioEditor) {
                a->audioEditor = nullptr;
                a->previewAudio = false;
                a->audio.pause(true);
            }
            DestroyWindow(h);
            return 0;
        }
        if (m == WM_DESTROY && h == a->window) {
            PostQuitMessage(0);
            return 0;
        }
        if (m == WM_SIZE && a->initialized && (h == a->window || h == a->side)) {
            a->layout();
            return 0;
        }
        if (m == WM_ACTIVATE &&
            (h == a->window || h == a->dialogEditor || h == a->audioEditor || h == a->mapEditor ||
             h == a->layerEditor || h == a->eventEditor || h == a->tilePalette ||
             h == a->transitionEditor || h == a->databaseEditor || h == a->mapToolsEditor ||
             h == a->debuggerEditor || h == a->healthEditor || h == a->objectsEditor ||
             h == a->assetsEditor || h == a->animationsEditor ||
             GetAncestor(h, GA_ROOTOWNER) == a->window)) {
            HWND next = reinterpret_cast<HWND>(l);
            a->focused =
                LOWORD(w) != WA_INACTIVE ||
                (next &&
                 (next == a->window || next == a->dialogEditor || next == a->audioEditor ||
                  next == a->mapEditor || next == a->layerEditor || next == a->eventEditor ||
                  next == a->tilePalette || next == a->transitionEditor || next == a->databaseEditor ||
                  next == a->mapToolsEditor || next == a->debuggerEditor || next == a->healthEditor ||
                  next == a->objectsEditor || next == a->assetsEditor || next == a->animationsEditor ||
                  GetAncestor(next, GA_ROOTOWNER) == a->window));
            a->audio.pause(!a->focused);
            return 0;
        }
        if (h == a->window && !a->resources.published) {
            int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
            if (m == WM_LBUTTONDOWN && y >= 72 && x >= a->workspaceWidth && x < a->workspaceWidth + 8) {
                a->finishBrush();
                a->splittingWorkspace = true;
                SetCapture(h);
                return 0;
            }
            if (m == WM_MOUSEMOVE && a->splittingWorkspace) {
                a->workspaceWidth = std::clamp(x, 256, 380);
                a->layout();
                return 0;
            }
            if (m == WM_LBUTTONUP && a->splittingWorkspace) {
                a->splittingWorkspace = false;
                ReleaseCapture();
                a->saveWorkspaceSettings();
                return 0;
            }
            if ((m == WM_CAPTURECHANGED || m == WM_CANCELMODE) && a->splittingWorkspace) {
                a->splittingWorkspace = false;
                a->saveWorkspaceSettings();
            }
            if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT) {
                POINT p{};
                GetCursorPos(&p);
                ScreenToClient(h, &p);
                if (p.y >= 72 && p.x >= a->workspaceWidth && p.x < a->workspaceWidth + 8) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
        }
        if (m == WM_NOTIFY)
            return a->workspaceNotify(l);
        if (m == WM_COMMAND) {
            a->command(LOWORD(w), HIWORD(w));
            return 0;
        }
        if (h == a->side) {
            if (m == WM_LBUTTONDOWN) {
                RECT r;
                GetClientRect(h, &r);
                if (GET_X_LPARAM(l) >= r.right - 12) {
                    int target =
                        int(GET_Y_LPARAM(l) * 1306.0f / std::max(1L, r.bottom)) - int(r.bottom) / 2;
                    a->scrollSide(target - a->sideScroll);
                    return 0;
                }
            }

            if (m == WM_MOUSEWHEEL) {
                a->scrollSide(-GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 64);
                return 0;
            }
            if (m == WM_VSCROLL) {
                int action = LOWORD(w), amount = 0;
                if (action == SB_LINEUP)
                    amount = -28;
                if (action == SB_LINEDOWN)
                    amount = 28;
                if (action == SB_PAGEUP)
                    amount = -240;
                if (action == SB_PAGEDOWN)
                    amount = 240;
                if (action == SB_THUMBTRACK || action == SB_THUMBPOSITION)
                    amount = HIWORD(w) - a->sideScroll;
                a->scrollSide(amount);
                return 0;
            }
        }
        if (h == a->canvas && a->editing) {
            if (m == WM_CAPTURECHANGED || m == WM_CANCELMODE) {
                a->finishBrush();
                return 0;
            }
            if (m == WM_LBUTTONDBLCLK && a->editor.tool == 24) {
                a->finishBrush();
                a->mobilityCommand(MobilityEditor);
                return 0;
            }
            if (m == WM_LBUTTONDBLCLK && a->editor.tool == 23) {
                a->finishBrush();
                a->showBarrageEditor();
                return 0;
            }
            if (m == WM_LBUTTONDBLCLK && a->editor.tool == 22) {
                a->dragging = false;
                ReleaseCapture();
                a->editor.end();
                a->objectInstanceIndex = a->editor.selectedObject;
                a->showObjectsEditor();
                return 0;
            }
            if (m == WM_LBUTTONDBLCLK && a->editor.tool == 17) {
                a->dragging = false;
                ReleaseCapture();
                a->editor.end();
                a->showEventEditor(true);
                return 0;
            }
            if (m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN) {
                SetFocus(h);
                SetCapture(h);
                a->dragging = true;
                a->erasing = m == WM_RBUTTONDOWN;
                a->brush(GET_X_LPARAM(l), GET_Y_LPARAM(l), true);
                a->refreshChrome();
                return 0;
            }
            if (m == WM_MOUSEMOVE && a->dragging) {
                a->brush(GET_X_LPARAM(l), GET_Y_LPARAM(l), false);
                return 0;
            }
            if (m == WM_LBUTTONUP || m == WM_RBUTTONUP) {
                a->finishBrush();
                a->refreshChrome();
                return 0;
            }
            if (m == WM_MOUSEWHEEL) {
                a->camera.logicalX = std::clamp(a->camera.logicalX - GET_WHEEL_DELTA_WPARAM(w) * 2.0f,
                                                0.0f, mapWidth * tileSize - 1280);
                a->camera.pixelGridX = int(std::round(a->camera.logicalX * pixelsPerUnit));
                a->camera.renderX = a->camera.pixelGridX / pixelsPerUnit;
                return 0;
            }
        }
    } catch (const std::exception& e) {
        a->message = e.what();
        MessageBoxW(a->window, wide(e.what()).c_str(), L"Echo Engine", MB_ICONERROR);
    }
    return DefWindowProcW(h, m, w, l);
}
HWND App::control(const wchar_t* type, const wchar_t* text, int id, int x, int y, int w, int h,
                  DWORD style, HWND parent) {
    HWND result =
        CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent ? parent : window,
                      reinterpret_cast<HMENU>(std::intptr_t(id)), instance, nullptr);
    if (!result)
        throw std::runtime_error("Editor control creation failed");
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    themeControl(result, id, type);
    controls[id] = result;
    return result;
}
void App::label(const wchar_t* text, int x, int y) {
    control(L"STATIC", text, 0, x, y, 248, 22, 0, side);
}
void App::combo(int id, const std::vector<std::wstring>& values, int selected, int x, int y, int width) {
    auto c =
        control(L"COMBOBOX", L"", id, x, y, width, 240,
                CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, side);
    for (auto& v : values)
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v.c_str()));
    SendMessageW(c, CB_SETCURSEL, selected, 0);
    SendMessageW(c, CB_SETITEMHEIGHT, -1, 25);
}
int App::selection(int id) {
    return int(SendMessageW(controls.at(id), CB_GETCURSEL, 0, 0));
}
float App::number(int id) {
    wchar_t text[64]{};
    GetWindowTextW(controls.at(id), text, 64);
    float n = std::stof(text);
    if (!std::isfinite(n))
        throw std::runtime_error("Finite numbers required");
    return n;
}
void App::setNumber(int id, float n) {
    std::wostringstream s;
    s << n;
    SetWindowTextW(controls[id], s.str().c_str());
}
std::filesystem::path App::choose(bool save, const wchar_t* filter, const wchar_t* title,
                                  const wchar_t* extension) {
    wchar_t file[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = extension;
    dialog.Flags =
        OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
        return file;
    return {};
}
void App::layout() {
    RECT client{};
    GetClientRect(window, &client);
    if (resources.published) {
        canvasTop = 0;
        MoveWindow(canvas, 0, 0, std::max(320L, client.right), std::max(180L, client.bottom), TRUE);
        MoveWindow(status, 12, client.bottom - 24, client.right - 24, 20, TRUE);
    } else {
        canvasTop = 102;
        chromeY = 72;
        int x = 6;
        for (int id = WorkspaceFile; id <= WorkspaceHelp; ++id) {
            MoveWindow(controls[id], x, 2, 62, 26, TRUE);
            x += 64;
        }
        x = 8;
        for (auto [id, width] : std::vector<std::pair<int, int>>{{New, 36},
                                                                 {Open, 36},
                                                                 {Save, 36},
                                                                 {Undo, 34},
                                                                 {Redo, 34},
                                                                 {1, 60},
                                                                 {17, 60},
                                                                 {WorkspaceObjects, 60},
                                                                 {WorkspaceBarrage, 60},
                                                                 {WorkspacePencil, 44},
                                                                 {WorkspaceRectangle, 44},
                                                                 {WorkspaceBucket, 44},
                                                                 {WorkspaceSelection, 44},
                                                                 {WorkspacePick, 44},
                                                                 {WorkspaceInspector, 86},
                                                                 {Edit, 62},
                                                                 {Play, 92}}) {
            MoveWindow(controls[id], x, 34, width, 30, TRUE);
            x += width +
                 ((id == Save || id == Redo || id == WorkspaceBarrage || id == WorkspacePick) ? 12 : 4);
        }
        MoveWindow(controls[ThemeMode], client.right - 140, 34, 132, 28, TRUE);
        MoveWindow(workspace, 0, 72, workspaceWidth, std::max(300L, client.bottom - 98), TRUE);
        MoveWindow(controls[ImportTiles], workspaceWidth - 62, 6, 52, 26, TRUE);
        int cell = 60, left = (workspaceWidth - 4 * cell) / 2;
        MoveWindow(controls[Layer], 10, 36, workspaceWidth - 20, 240, TRUE);
        for (int i = 0; i < 16; ++i)
            MoveWindow(controls[WorkspaceTileFirst + i], left + i % 4 * cell, 76 + i / 4 * cell, cell,
                       cell, TRUE);
        for (int i = 0; i < 4; ++i)
            MoveWindow(controls[WorkspaceTerrain + i], left + i * cell, 320, cell, 28, TRUE);
        MoveWindow(workspaceItems, 10, 76, workspaceWidth - 20, 234, TRUE);
        MoveWindow(controls[WorkspaceItemEdit], 10, 320, workspaceWidth - 20, 28, TRUE);
        MoveWindow(workspaceTip, 10, 352, workspaceWidth - 20, 28, TRUE);
        MoveWindow(mapTree, 6, 412, workspaceWidth - 12, std::max(80L, client.bottom - 72 - 26 - 420),
                   TRUE);
        MoveWindow(controls[MapsEditor], workspaceWidth - 62, 384, 52, 25, TRUE);
        MoveWindow(canvas, workspaceWidth + 8, canvasTop,
                   std::max(320, int(client.right) - workspaceWidth - 16),
                   std::max(180, int(client.bottom) - canvasTop - 30), TRUE);
        MoveWindow(status, 10, client.bottom - 24, client.right - 20, 22, TRUE);
        RECT inspector{};
        GetClientRect(side, &inspector);
        SCROLLINFO info{sizeof(info),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        0,
                        1306,
                        UINT(inspector.bottom),
                        sideScroll,
                        0};
        SetScrollInfo(side, SB_VERT, &info, TRUE);
        scrollSide(0);
    }
    RECT c{};
    GetClientRect(canvas, &c);
    renderer.resize(c.right, c.bottom);
    InvalidateRect(window, nullptr, FALSE);
    if (workspace)
        InvalidateRect(workspace, nullptr, FALSE);
}
void App::ui() {
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&common);
    font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    strongFont = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    smallFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    setTheme(resources.published);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.hInstance = instance;
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"EchoEngineWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hbrBackground = windowBrush;
    RegisterClassW(&wc);
    RECT gameSize{0, 0, 1280, 720};
    AdjustWindowRect(&gameSize, WS_OVERLAPPEDWINDOW, FALSE);
    int initialWidth = resources.published ? gameSize.right - gameSize.left : 1440,
        initialHeight = resources.published ? gameSize.bottom - gameSize.top : 900;
    window = CreateWindowW(wc.lpszClassName, L"Echo Engine", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 24,
                           24, initialWidth, initialHeight, nullptr, nullptr, instance, this);
    canvas = CreateWindowW(wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 20, 122, 1280, 720, window,
                           nullptr, instance, this);
    side = CreateWindowW(wc.lpszClassName, L"场景设置 · 环境 / 回溯 / 平台 / 灯光",
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, 180, 110, 304, 820,
                         window, nullptr, instance, this);
    status = control(L"STATIC",
                     resources.published ? L"A / D 跑  ·  空格跳  ·  R 快退  ·  T 回溯"
                                         : L"就绪  ·  拖动绘制关卡，F5 即时试玩",
                     999, 20, 950, 1280, 24);
    SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    if (!resources.published) {
        int x = 236;
        for (auto [id, text, width] : std::vector<std::tuple<int, const wchar_t*, int>>{
                 {Open, L"打开", 64},
                 {New, L"新建", 64},
                 {Save, L"保存", 64},
                 {Play, L"▶ 试玩 F5", 106},
                 {Edit, L"编辑 F2", 88},
                 {Undo, L"↶", 38},
                 {Redo, L"↷", 38},
                 {ExportPublic, L"公开导出", 92},
                 {ExportEncrypted, L"加密导出", 92},
                 {Reload, L"重载 F6", 88},
                 {Stress, L"弹幕压测", 92},
                 {DialogEditor, L"对话与头像", 108},
                 {AudioEditor, L"BGM / SE", 100},
                 {MapsEditor, L"地图与卷轴", 108},
                 {LayerEditor, L"图层图片", 96},
                 {EventEditor, L"地图 / 全局事件", 140},
                 {TilePaletteEditor, L"图块调色板", 112},
                 {TransitionEditor, L"切图动画", 100},
                 {DatabaseEditor, L"游戏数据库", 108},
                 {MapToolsEditor, L"地图工具", 100},
                 {DebuggerEditor, L"试玩调试", 100},
                 {ProjectHealthEditor, L"工程检查与恢复", 140},
                 {ObjectsEditor, L"对象与模板", 108},
                 {AssetsEditor, L"资源管理", 100},
                 {AnimationsEditor, L"精灵与动画", 108},
                 {BarrageEditor, L"弹幕与波次", 108},
                 {FocusEditor, L"判定 / 低速 / 采样", 152},
                 {FrameRateEditor, L"帧率与同步", 108},
                 {CollisionEditor, L"碰撞分类", 100}}) {
            control(L"BUTTON", text, id, x, 18, width, 30, WS_TABSTOP);
            ShowWindow(controls[id], SW_HIDE);
            x += width + 8;
        }
        auto preset = [&](int id, int, const std::vector<std::wstring>& values, int chosen) {
            HWND h = control(L"COMBOBOX", L"", id, 18, id == JumpPreset ? 73 : 113, 234, 240,
                             CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, side);
            for (auto& value : values)
                SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
            SendMessageW(h, CB_SETCURSEL, chosen, 0);
            SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
        };
        HWND theme = control(L"COMBOBOX", L"", ThemeMode, 1300, 18, 170, 240,
                             CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP);
        SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"浅色 · 默认"));
        SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"深色 · 石板灰"));
        SendMessageW(theme, CB_SETCURSEL, 0, 0);
        SendMessageW(theme, CB_SETITEMHEIGHT, -1, 25);
        preset(JumpPreset, 720,
               {L"手感：项目 / 自定义", L"手感：基础平台跳跃", L"手感：弹性跑跳", L"手感：紧凑动作",
                L"手感：宽容探索"},
               initial_->jumpPreset + 1);
        preset(BulletPreset, 956,
               {L"弹幕：项目 / 自定义", L"弹幕：关闭", L"弹幕：新手慢速", L"弹幕：扇形", L"弹幕：环形",
                L"弹幕：螺旋", L"弹幕：波浪", L"弹幕：阵列"},
               initial_->bulletPreset + 1);
        label(L"关卡工具", 16, 34);
        const wchar_t* names[] = {L"1  实心方块", L"2  单向平台",  L"3  尖刺",     L"4  橡皮擦",
                                  L"5  玩家出生", L"6  Boss 出生", L"7  移动平台", L"8  选中拖动"};
        for (int i = 0; i < 8; ++i)
            control(L"BUTTON", names[i], i + 1, 18 + (i % 2) * 120, 68 + (i / 2) * 34, 114, 28,
                    WS_TABSTOP, side);
        combo(Layer, {L"图层：后景", L"图层：世界", L"图层：前景"}, 1, 18, 228, 234);
        std::vector<std::wstring> tileNames;
        for (int i = 1; i <= 16; ++i)
            tileNames.push_back(L"Tile " + std::to_wstring(i));
        combo(TileIndex, tileNames, 0, 18, 266, 114);
        control(L"BUTTON", L"导入图块", ImportTiles, 138, 266, 114, 28, WS_TABSTOP, side);
        control(L"BUTTON", L"世界地砖自动生成方形碰撞", AutoCollision, 18, 300, 234, 27, WS_TABSTOP,
                side);
        SendMessageW(controls[AutoCollision], BM_SETCHECK, 1, 0);
        label(L"环境与光照", 16, 346);
        combo(Environment,
              {L"黎明 · Dawn", L"白昼 · Day", L"黄昏 · Dusk", L"夜晚 · Night", L"流星星空", L"洞窟",
               L"极光"},
              config.environment, 18, 380, 234);
        combo(Weather, {L"晴 / 无降水", L"雨", L"雪"}, config.weather, 18, 418, 114);
        combo(Strength, {L"无", L"小", L"中", L"大", L"暴", L"雷暴 / 暴风雪"},
              int(std::round(config.weatherStrength * 5)), 138, 418, 114);
        label(L"雾浓度 0..1          风速 -3..3", 18, 452);
        control(L"EDIT", L"", Fog, 18, 477, 114, 26, WS_BORDER | WS_TABSTOP, side);
        setNumber(Fog, config.fog);
        control(L"EDIT", L"", Wind, 138, 477, 114, 26, WS_BORDER | WS_TABSTOP, side);
        setNumber(Wind, config.wind);
        control(L"BUTTON", L"太阳 / 月光与场景灯光", Sunlight, 18, 513, 234, 27, WS_TABSTOP, side);
        control(L"BUTTON", L"黄昏 / 夜晚萤火虫", Fireflies, 18, 543, 234, 27, WS_TABSTOP, side);
        control(L"BUTTON", L"可选流体背景层", Fluid, 18, 573, 234, 27, WS_TABSTOP, side);
        control(L"BUTTON", L"SDF Raymarch 背景", Raymarch, 18, 603, 234, 27, WS_TABSTOP, side);
        for (auto [id, value] : std::vector<std::pair<int, bool>>{{Fluid, config.fluid},
                                                                  {Raymarch, config.raymarch},
                                                                  {Sunlight, config.sunlight},
                                                                  {Fireflies, config.fireflies}})
            SendMessageW(controls[id], BM_SETCHECK, value, 0);
        control(L"BUTTON", L"导入自定义背景着色器", CustomShader, 18, 639, 234, 28, WS_TABSTOP, side);
        combo(Snap, {L"Crisp · 像素吸附滞后", L"Smooth · 亚像素混合"},
              camera.snapMode == SnapMode::Crisp ? 0 : 1, 18, 676, 234);
        label(L"回溯记录", 16, 720);
        label(L"窗口秒数               记录频率", 18, 747);
        control(L"EDIT", L"", WindowSeconds, 18, 772, 114, 26, WS_BORDER | WS_TABSTOP, side);
        setNumber(WindowSeconds, initial_->rewind.history_seconds);
        combo(RecordHz, {L"30 Hz", L"60 Hz", L"120 Hz"},
              initial_->rewind.record_hz == 30   ? 0
              : initial_->rewind.record_hz == 60 ? 1
                                                 : 2,
              138, 772, 114);
        control(L"BUTTON", L"启用回溯", Enabled, 18, 810, 114, 27, WS_TABSTOP, side);
        SendMessageW(controls[Enabled], BM_SETCHECK, initial_->rewind.enabled, 0);
        control(L"BUTTON", L"应用参数", Apply, 138, 810, 114, 27, WS_TABSTOP, side);
        label(L"移动平台", 16, 862);
        label(L"振幅 (px)                速度", 18, 888);
        control(L"EDIT", L"96", PlatformAmplitude, 18, 913, 114, 26, WS_BORDER | WS_TABSTOP, side);
        control(L"EDIT", L"1", PlatformSpeed, 138, 913, 114, 26, WS_BORDER | WS_TABSTOP, side);
        control(L"BUTTON", L"应用到选中平台", PlatformApply, 18, 952, 234, 28, WS_TABSTOP, side);
        label(L"场景灯光", 16, 1004);
        control(L"BUTTON", L"9  放置 / 选择灯光", 9, 18, 1037, 234, 28, WS_TABSTOP, side);
        label(L"半径 (px)                亮度", 18, 1070);
        control(L"EDIT", L"160", LightRadius, 18, 1095, 114, 26, WS_BORDER | WS_TABSTOP, side);
        control(L"EDIT", L"1.2", LightIntensity, 138, 1095, 114, 26, WS_BORDER | WS_TABSTOP, side);
        control(L"BUTTON", L"应用到选中灯光", LightApply, 18, 1129, 234, 28, WS_TABSTOP, side);
        label(L"碰撞绘制 · 拖动定大小", 16, 1170);
        const wchar_t* shapes[] = {L"0  圆形", L"−  三角形", L"=  正方形", L"矩形", L"自绘凸轮廓"};
        for (int i = 0; i < 5; ++i)
            control(L"BUTTON", shapes[i], 10 + i, 18 + (i % 2) * 120, 1205 + (i / 2) * 34, 114, 28,
                    WS_TABSTOP, side);
        control(L"BUTTON", L"对话区域", 15, 138, 1273, 114, 28, WS_TABSTOP, side);
        control(L"BUTTON", L"传送区域", 16, 18, 1307, 234, 28, WS_TABSTOP, side);
        control(L"BUTTON", L"部署 / 选择地图事件", 17, 18, 1341, 234, 28, WS_TABSTOP, side);
        label(L"事件工具：双击事件编辑指令", 18, 1375);
        label(L"R 快退 · T 回溯 · E 互动", 18, 1402);
    }
    dialogWindow =
        CreateWindowW(wc.lpszClassName, L"", WS_CHILD, 0, 0, 600, 150, canvas, nullptr, instance, this);
    renderer.initialize(canvas, &resources);
    renderer.environment = editor.level.background;
    if (!resources.published)
        SendMessageW(controls[Environment], CB_SETCURSEL, editor.level.background, 0);
    if (!resources.published) {
        initWorkspace();
        loadWorkspaceSettings();
    }
    initialized = true;
    layout();
    if (!resources.published)
        refreshWorkspace(true);
    SetWindowTextW(
        window,
        (resources.published ? wide(config.title) : L"Echo Engine — " + wide(config.title)).c_str());
    ShowWindow(window, (verifyMode || smoke) ? SW_HIDE : SW_SHOW);
    SetFocus(canvas);
}
} // namespace echo
