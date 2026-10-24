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
namespace {
constexpr UINT fitDialogMessage = WM_APP + 41;
// The older dialogs were sized by hand; fit the client to its controls and cards (mirroring the
// left / top margins) and centre the window over the editor the first time it is shown.
void fitDialog(App& a, HWND h) {
    RECT content{1 << 20, 1 << 20, 0, 0};
    auto include = [&](RECT r) {
        content.left = std::min(content.left, r.left);
        content.top = std::min(content.top, r.top);
        content.right = std::max(content.right, r.right);
        content.bottom = std::max(content.bottom, r.bottom);
    };
    for (HWND child = GetWindow(h, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT r{};
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, h, reinterpret_cast<POINT*>(&r), 2);
        include(r);
    }
    for (const auto& card : a.cardRects(h))
        include(card);
    if (content.right <= 0 || content.bottom <= 0)
        return;
    RECT frame{0, 0, content.right + std::clamp(int(content.left), 8, 24),
               content.bottom + std::clamp(int(content.top), 8, 24)};
    AdjustWindowRectEx(&frame, DWORD(GetWindowLongPtrW(h, GWL_STYLE)), FALSE,
                       DWORD(GetWindowLongPtrW(h, GWL_EXSTYLE)));
    int width = frame.right - frame.left, height = frame.bottom - frame.top;
    RECT owner{};
    GetWindowRect(a.window, &owner);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(a.window, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;
    int x = owner.left + (owner.right - owner.left - width) / 2;
    int y = owner.top + (owner.bottom - owner.top - height) / 2;
    x = std::clamp(x, int(work.left), std::max(int(work.left), int(work.right) - width));
    y = std::clamp(y, int(work.top), std::max(int(work.top), int(work.bottom) - height));
    SetWindowPos(h, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}
} // namespace
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
        if (m == WM_SHOWWINDOW && w && h != a->window && h != a->side && !a->verifyMode && !a->smoke &&
            !(GetWindowLongPtrW(h, GWL_STYLE) & WS_CHILD) && !GetPropW(h, L"EchoFitted")) {
            // Shown from CreateWindow before its controls exist: fit once they are created.
            SetPropW(h, L"EchoFitted", reinterpret_cast<HANDLE>(1));
            a->applyTitleBarTheme(h);
            PostMessageW(h, fitDialogMessage, 0, 0);
        }
        if (m == fitDialogMessage) {
            fitDialog(*a, h);
            return 0;
        }
        if (m == WM_NCDESTROY)
            RemovePropW(h, L"EchoFitted");
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
            info->ptMinTrackSize = a->resources.published ? POINT{656, 399}
                                                         : POINT{std::max(1200, ui::dp(1100, a->uiScale)),
                                                                 std::max(720, ui::dp(600, a->uiScale))};
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
        if (h == a->window && !a->resources.published && a->initialized) {
            LRESULT handled{};
            if (a->workspaceMouse(m, w, l, handled))
                return handled;
        }
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
                // The canvas keeps keyboard focus, so wheel input over the dock arrives here.
                POINT cursor{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                if (WindowFromPoint(cursor) != h && a->workspaceWheel(cursor, GET_WHEEL_DELTA_WPARAM(w)))
                    return 0;
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
// The older settings windows are plain windows rather than dialog-manager dialogs, so keyboard
// navigation is provided here: Tab / Shift+Tab between tab stops, Esc closes the window.
bool App::dialogKey(const MSG& msg) {
    HWND focus = GetFocus();
    HWND root = focus ? GetAncestor(focus, GA_ROOT) : nullptr;
    if (msg.message != WM_KEYDOWN || !root || root == window || root == side ||
        GetAncestor(root, GA_ROOTOWNER) != window)
        return false;
    wchar_t type[32]{};
    GetClassNameW(focus, type, 32);
    std::wstring_view kind = type;
    if (msg.wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) {
        // Multi-line fields (dialogue text, Lua sources) keep Tab for indentation.
        if (kind == L"Edit" && (GetWindowLongPtrW(focus, GWL_STYLE) & ES_MULTILINE))
            return false;
        HWND current = focus;
        while (GetParent(current) && GetParent(current) != root)
            current = GetParent(current);
        HWND next = GetNextDlgTabItem(root, current, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        if (next && next != current) {
            SetFocus(next);
            GetClassNameW(next, type, 32);
            if (std::wstring_view(type) == L"Edit")
                SendMessageW(next, EM_SETSEL, 0, -1);
            SetPropW(next, L"EchoKeyFocus", reinterpret_cast<HANDLE>(1));
            InvalidateRect(next, nullptr, FALSE);
        }
        return true;
    }
    if (msg.wParam == VK_ESCAPE) {
        if (kind == L"ComboBox" && SendMessageW(focus, CB_GETDROPPEDSTATE, 0, 0))
            return false; // Esc first closes an open drop-down
        SendMessageW(root, WM_CLOSE, 0, 0);
        return true;
    }
    return false;
}
HWND App::control(const wchar_t* type, const wchar_t* text, int id, int x, int y, int w, int h,
                  DWORD style, HWND parent) {
    std::wstring_view kind = type;
    // Lists draw their own rows (tab-stop tables keep native drawing); single-line fields scroll.
    if (kind == L"LISTBOX" && !(style & LBS_USETABSTOPS))
        style |= LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
    if (kind == L"EDIT" && !(style & ES_MULTILINE))
        style |= ES_AUTOHSCROLL;
    // An EDIT with WS_BORDER paints its own square border inside the client area. A client edge
    // keeps the frame (and any scroll bar) in the non-client area, where fieldProc draws it.
    DWORD extended = 0;
    if (kind == L"EDIT" && (style & WS_BORDER)) {
        style &= ~WS_BORDER;
        extended = WS_EX_CLIENTEDGE;
    }
    HWND result = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                  parent ? parent : window, reinterpret_cast<HMENU>(std::intptr_t(id)),
                                  instance, nullptr);
    if (!result)
        throw std::runtime_error("编辑器控件创建失败");
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
        throw std::runtime_error("请输入有效的数字");
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
        chromeLayout = workspaceLayout(client.right, client.bottom, workspaceWidth, uiScale, inspectorOpen);
        const auto& view = chromeLayout.canvas;
        chromeY = chromeLayout.dock.y;
        canvasTop = view.y;
        MoveWindow(canvas, view.x, view.y, std::max(320, view.w), std::max(180, view.h), TRUE);
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
    refreshWorkspace();
    InvalidateRect(window, nullptr, FALSE);
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
    if (!resources.published) {
        uiScale = ui::systemScale();
        uiFonts.create(uiScale);
        workspaceWidth = ui::dp(272, uiScale);
    }
    RECT work{0, 0, 1440, 900};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT gameSize{0, 0, 1280, 720};
    AdjustWindowRect(&gameSize, WS_OVERLAPPEDWINDOW, FALSE);
    int initialWidth = resources.published ? gameSize.right - gameSize.left
                                           : std::min(ui::dp(1440, uiScale), int(work.right - work.left) - 48),
        initialHeight = resources.published ? gameSize.bottom - gameSize.top
                                            : std::min(ui::dp(900, uiScale), int(work.bottom - work.top) - 48);
    // The editor opens centred in the work area; published games keep their fixed corner origin.
    int initialX = resources.published ? 24 : int(work.left) + std::max(0, int(work.right - work.left) - initialWidth) / 2;
    int initialY = resources.published ? 24 : int(work.top) + std::max(0, int(work.bottom - work.top) - initialHeight) / 2;
    window = CreateWindowW(wc.lpszClassName, L"Echo Engine", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, initialX,
                           initialY, initialWidth, initialHeight, nullptr, nullptr, instance, this);
    applyTitleBarTheme(window);
    canvas = CreateWindowW(wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 20, 122, 1280, 720, window,
                           nullptr, instance, this);
    side = CreateWindowW(wc.lpszClassName, L"场景设置 · 环境 / 回溯 / 平台 / 灯光",
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, 180, 110, 304, 820,
                         window, nullptr, instance, this);
    if (resources.published) {
        status = control(L"STATIC", L"A / D 跑  ·  空格跳  ·  R 快退  ·  T 回溯", 999, 20, 950, 1280, 24);
        SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    } else {
        // The editor chrome (menus, toolbar, dock, status bar) is drawn by echo::ui.
        statusText = L"就绪   |   拖动绘制关卡，F5 即时试玩";
        auto preset = [&](int id, int, const std::vector<std::wstring>& values, int chosen) {
            HWND h = control(L"COMBOBOX", L"", id, 18, id == JumpPreset ? 73 : 113, 234, 240,
                             CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, side);
            for (auto& value : values)
                SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
            SendMessageW(h, CB_SETCURSEL, chosen, 0);
            SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
        };
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
    if (!resources.published)
        initWorkspace();
    initialized = true;
    if (!resources.published)
        loadWorkspaceSettings();
    layout();
    SetWindowTextW(
        window,
        (resources.published ? wide(config.title) : L"Echo Engine — " + wide(config.title)).c_str());
    ShowWindow(window, (verifyMode || smoke) ? SW_HIDE : SW_SHOW);
    SetFocus(canvas);
}
} // namespace echo
