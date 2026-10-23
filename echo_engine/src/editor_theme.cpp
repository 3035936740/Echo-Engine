#include "echo/app.hpp"
#include <cmath>
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
namespace echo {
namespace {
using Palette = std::array<COLORREF, 8>;
Palette palette(bool dark) {
    if (dark)
        return {RGB(37, 41, 54),    RGB(43, 48, 64),    RGB(52, 59, 77),    RGB(69, 81, 106),
                RGB(227, 230, 239), RGB(154, 166, 185), RGB(120, 172, 230), RGB(201, 169, 116)};
    return {RGB(238, 241, 245), RGB(255, 255, 255), RGB(247, 249, 252), RGB(206, 214, 224),
            RGB(44, 52, 66),    RGB(104, 116, 134), RGB(71, 137, 201),  RGB(145, 111, 66)};
}

void rounded(HDC dc, RECT r, COLORREF fill, COLORREF stroke, int radius = 8) {
    auto brush = CreateSolidBrush(fill);
    auto pen = CreatePen(PS_SOLID, 1, stroke);
    auto oldB = SelectObject(dc, brush), oldP = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(brush);
    DeleteObject(pen);
}
void string(HDC dc, HFONT font, const wchar_t* value, RECT r, COLORREF color,
            UINT align = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &r, align);
    SelectObject(dc, old);
}
bool toggle(int id) {
    if (id >= EventControlFirst && id <= EventControlLast)
        return true;
    if (id >= CollisionCell && id <= CollisionLast)
        return true;
    if (id == FrameVsync)
        return true;
    if (id == FocusEnabled || id == FocusIndicator || id == FocusGround || id == FocusAir ||
        id == FocusInstant || id == HurtTouching || id == HurtMirror || id == HurtProjectileBox ||
        id == HurtRegionEnabled || id == HurtRegionFocusOnly || id == HurtMaskBullet ||
        id == HurtMaskContact || id == HurtMaskMelee)
        return true;
    return id == DataAuto || id == Fluid || id == Raymarch || id == Enabled || id == Sunlight ||
           id == Fireflies || id == AutoCollision || id == MusicLoopEnabled || id == MapClamp ||
           id == PortalSpawn || id == EventSwitchConditionValue || id == EventSelfConditionValue ||
           id == EventSolid || id == MobilityDash || id == MobilityWall || id == MobilityCrush ||
           id == FeatureEnabled || id == FeatureRepeat || id == ChoiceHide || id == RewindNearEnabled ||
           id == InputArrows || id == EventPageScriptEnabled || id == TransitionCenter ||
           id == TransitionOverride || id == MapGrid || id == MapCollisions || id == MapLock ||
           id == MapHide || id == DebugPause || id == ObjectGravity || id == ObjectSolid ||
           id == ObjectRide || id == ObjectCrush || id == ObjectPersistent ||
           id == ObjectOverrideSpeed || id == ObjectOverrideHP || id == ObjectOverrideDamage ||
           id == AnimationLoop || id == AnimationFlip || id == AnimationCollision ||
           id == AnimationHurt || id == AnimationAttack || id == BarrageEnabled || id == BarrageLoop ||
           id == BarragePersistent || id == BarrageTargetExclude;
}
LRESULT CALLBACK comboProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    if (m == WM_PAINT || m == WM_PRINTCLIENT) {
        auto app = reinterpret_cast<App*>(data);
        auto [bg, panel, field, border, text, muted, accent, gold] = palette(app->darkTheme);
        PAINTSTRUCT paint{};
        HDC dc = m == WM_PAINT ? BeginPaint(h, &paint) : reinterpret_cast<HDC>(w);
        RECT r;
        GetClientRect(h, &r);
        rounded(dc, r, field, GetFocus() == h ? accent : border, 6);
        DRAWITEMSTRUCT item{};
        item.CtlType = ODT_COMBOBOX;
        item.CtlID = GetDlgCtrlID(h);
        item.itemID = UINT(SendMessageW(h, CB_GETCURSEL, 0, 0));
        item.hwndItem = h;
        item.hDC = dc;
        item.rcItem = {r.left + 2, r.top + 2, r.right - 24, r.bottom - 2};
        app->drawControl(item);
        auto pen = CreatePen(PS_SOLID, 1, muted);
        auto old = SelectObject(dc, pen);
        int x = r.right - 15, y = (r.bottom - r.top) / 2;
        MoveToEx(dc, x - 3, y - 2, nullptr);
        LineTo(dc, x, y + 1);
        LineTo(dc, x + 3, y - 2);
        SelectObject(dc, old);
        DeleteObject(pen);
        if (m == WM_PAINT)
            EndPaint(h, &paint);
        return 0;
    }
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, comboProc, 1);
    return DefSubclassProc(h, m, w, l);
}
LRESULT CALLBACK buttonProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    int id = GetDlgCtrlID(h);
    if (m == BM_SETCHECK && toggle(id)) {
        SetPropW(h, L"EchoChecked", reinterpret_cast<HANDLE>(std::intptr_t(w)));
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if (m == BM_GETCHECK && toggle(id))
        return reinterpret_cast<std::intptr_t>(GetPropW(h, L"EchoChecked"));
    if ((m == WM_LBUTTONUP || (m == WM_KEYUP && w == VK_SPACE)) && toggle(id)) {
        auto current = reinterpret_cast<std::intptr_t>(GetPropW(h, L"EchoChecked"));
        SetPropW(h, L"EchoChecked", reinterpret_cast<HANDLE>(current ? 0 : 1));
    }
    if (m == WM_MOUSEMOVE) {
        if (!GetPropW(h, L"EchoHover")) {
            SetPropW(h, L"EchoHover", reinterpret_cast<HANDLE>(1));
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
            TrackMouseEvent(&track);
        }
    }
    if (m == WM_MOUSELEAVE) {
        RemovePropW(h, L"EchoHover");
        InvalidateRect(h, nullptr, FALSE);
    }
    if (m == WM_NCDESTROY) {
        RemovePropW(h, L"EchoChecked");
        RemovePropW(h, L"EchoHover");
        RemoveWindowSubclass(h, buttonProc, 1);
    }
    return DefSubclassProc(h, m, w, l);
}
} // namespace
void App::themeControl(HWND h, int, const wchar_t* type) {
    if (std::wstring_view(type) == L"BUTTON") {
        SetWindowLongPtrW(h, GWL_STYLE, (GetWindowLongPtrW(h, GWL_STYLE) & ~0xf) | BS_OWNERDRAW);
        SetWindowSubclass(h, buttonProc, 1, 0);
    } else if (std::wstring_view(type) == L"COMBOBOX") {
        SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        SetWindowSubclass(h, comboProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
}
void App::drawControl(const DRAWITEMSTRUCT& item) {
    auto [bg, panel, field, border, text, muted, accent, gold] = palette(darkTheme);
    if (item.CtlType == ODT_COMBOBOX) {
        RECT r = item.rcItem;
        SetBkColor(item.hDC, field);
        SetTextColor(item.hDC, text);
        FillRect(item.hDC, &r, fieldBrush);
        int selected = item.itemID == UINT(-1) ? int(SendMessageW(item.hwndItem, CB_GETCURSEL, 0, 0))
                                               : int(item.itemID);
        if (selected >= 0) {
            wchar_t value[256]{};
            SendMessageW(item.hwndItem, CB_GETLBTEXT, selected, reinterpret_cast<LPARAM>(value));
            if (item.itemState & ODS_SELECTED) {
                auto selected = CreateSolidBrush(darkTheme ? RGB(55, 77, 105) : RGB(224, 236, 250));
                FillRect(item.hDC, &r, selected);
                DeleteObject(selected);
            }
            r.left += 9;
            string(item.hDC, font, value, r, IsWindowEnabled(item.hwndItem) ? text : muted);
        }
        return;
    }
    int id = item.CtlID;
    bool dockTile = id >= WorkspaceTileFirst && id <= WorkspaceTileLast;
    if ((id >= 400 && id < 416) || dockTile) {
        RECT r = item.rcItem;
        int index = dockTile ? id - WorkspaceTileFirst : id - 400;
        bool selected = editor.tileIndex == index + 1;
        FillRect(item.hDC, &r, panelBrush);
        int size = dockTile ? 48 : 56;
        int left = r.left + (r.right - r.left - size) / 2, top = r.top + (dockTile ? 5 : 8);
        if (tileAtlas) {
            int frame = config.tileFrame, columns = tileAtlasW / frame;
            if (columns > 0 && index < columns * (tileAtlasH / frame)) {
                auto source = CreateCompatibleDC(item.hDC);
                auto old = SelectObject(source, tileAtlas);
                SetStretchBltMode(item.hDC, COLORONCOLOR);
                StretchBlt(item.hDC, left, top, size, size, source, index % columns * frame,
                           index / columns * frame, frame, frame, SRCCOPY);
                SelectObject(source, old);
                DeleteDC(source);
            }
        } else {
            rounded(item.hDC, {left, top, left + size, top + size}, field, border, 0);
            auto number = std::to_wstring(index + 1);
            string(item.hDC, font, number.c_str(), {left, top, left + size, top + size}, muted,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        RECT edge{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        auto pen = CreatePen(PS_SOLID, selected ? 2 : 1, selected ? accent : border);
        auto oldP = SelectObject(item.hDC, pen),
             oldB = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
        Rectangle(item.hDC, edge.left, edge.top, edge.right, edge.bottom);
        SelectObject(item.hDC, oldP);
        SelectObject(item.hDC, oldB);
        DeleteObject(pen);
        if (!dockTile) {
            auto number = std::to_wstring(index + 1);
            string(item.hDC, smallFont, number.c_str(), {r.left, r.bottom - 20, r.right, r.bottom - 2},
                   selected ? accent : muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return;
    }
    RECT r = item.rcItem;
    bool hovered = GetPropW(item.hwndItem, L"EchoHover"), pressed = item.itemState & ODS_SELECTED;
    bool active = (id >= 1 && id <= 17 && editing && editor.tool == id) || (id == Edit && editing);
    active = active || (id == WorkspaceObjects && editing && editor.tool == 22) ||
             (id == WorkspaceBarrage && editing && editor.tool == 23) ||
             (id == 1 && editing && workspaceMode == 0) ||
             (id == WorkspacePencil && editor.tool == int(editor.terrain)) ||
             (id >= WorkspaceRectangle && id <= WorkspacePick &&
              editor.tool == 18 + int(id - WorkspaceRectangle)) ||
             (id >= WorkspaceTerrain && id <= WorkspaceErase &&
              editor.tool == int(id - WorkspaceTerrain + 1));
    bool primary = id == Play;
    COLORREF fill = active    ? (darkTheme ? RGB(55, 77, 105) : RGB(224, 236, 250))
                    : hovered ? (darkTheme ? RGB(61, 69, 87) : RGB(230, 236, 245))
                              : field;
    COLORREF stroke = active ? accent : border;
    COLORREF foreground = active ? accent : text;
    if (primary) {
        fill = pressed ? RGB(53, 114, 177) : hovered ? RGB(96, 159, 220) : accent;
        stroke = fill;
        foreground = darkTheme ? bg : RGB(255, 255, 255);
    }
    if (id == Save && editor.dirty)
        stroke = gold;
    if (pressed && !primary)
        fill = darkTheme ? RGB(69, 87, 114) : RGB(212, 226, 246);
    if (toggle(id)) {
        FillRect(item.hDC, &r, panelBrush);
        RECT box{r.left + 2, r.top + 5, r.left + 18, r.top + 21};
        bool checked = SendMessageW(item.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
        rounded(item.hDC, box, checked ? accent : field, checked ? accent : border, 4);
        if (checked) {
            auto pen = CreatePen(PS_SOLID, 2, bg);
            auto old = SelectObject(item.hDC, pen);
            MoveToEx(item.hDC, box.left + 3, box.top + 8, nullptr);
            LineTo(item.hDC, box.left + 7, box.top + 12);
            LineTo(item.hDC, box.left + 13, box.top + 4);
            SelectObject(item.hDC, old);
            DeleteObject(pen);
        }
        wchar_t value[128]{};
        GetWindowTextW(item.hwndItem, value, 128);
        RECT label = r;
        label.left += 28;
        string(item.hDC, font, value, label,
               !IsWindowEnabled(item.hwndItem) ? muted
               : hovered                       ? accent
                                               : text);
        return;
    }
    bool menu = id >= WorkspaceFile && id <= WorkspaceHelp;
    rounded(item.hDC, r, menu && !hovered ? bg : fill, menu && !hovered ? bg : stroke, 4);
    wchar_t value[128]{};
    GetWindowTextW(item.hwndItem, value, 128);
    string(item.hDC, primary ? strongFont : font, value, r,
           IsWindowEnabled(item.hwndItem) ? foreground : muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item.itemState & ODS_FOCUS) {
        RECT focus = r;
        InflateRect(&focus, -3, -3);
        auto pen = CreatePen(PS_DOT, 1, accent);
        auto oldP = SelectObject(item.hDC, pen);
        auto oldB = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
        RoundRect(item.hDC, focus.left, focus.top, focus.right, focus.bottom, 5, 5);
        SelectObject(item.hDC, oldB);
        SelectObject(item.hDC, oldP);
        DeleteObject(pen);
    }
}
LRESULT App::colorControl(HDC dc, HWND control) {
    auto [bg, panel, field, border, text, muted, accent, gold] = palette(darkTheme);
    wchar_t type[64]{};
    GetClassNameW(control, type, 64);
    bool edit = std::wstring_view(type) == L"Edit" || std::wstring_view(type) == L"ComboBox" ||
                std::wstring_view(type) == L"ListBox";
    SetTextColor(dc, edit && IsWindowEnabled(control) ? text : muted);
    bool root = GetParent(control) != side;
    if ((GetParent(control) == eventEditor || GetParent(control) == transitionEditor) && !edit) {
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    SetBkColor(dc, edit ? field : root ? bg : panel);
    return reinterpret_cast<LRESULT>(edit ? fieldBrush : root ? windowBrush : panelBrush);
}
void App::paintChrome(HWND h, HDC supplied) {
    auto [bg, panel, field, border, text, muted, accent, gold] = palette(darkTheme);
    PAINTSTRUCT paint{};
    HDC dc = supplied ? supplied : BeginPaint(h, &paint);
    RECT area;
    GetClientRect(h, &area);
    FillRect(dc, &area, (h == side || h == workspace) ? panelBrush : windowBrush);
    if (h == window && resources.published) {
        if (!supplied)
            EndPaint(h, &paint);
        return;
    }
    if (h == window) {
        string(dc, smallFont, L"ECHO ENGINE", {480, 2, area.right - 12, 28}, muted,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        auto title = wide(pathUtf8(pathFromUtf8(config.maps[editMap]).stem()));
        if (editor.dirty)
            title += L"  \u2022 \u672a\u4fdd\u5b58";
        title += editing ? L"    /    \u5173\u5361\u7f16\u8f91" : L"    /    \u8bd5\u73a9\u4e2d";
        string(dc, font, title.c_str(), {workspaceWidth + 12, 72, area.right - 220, 100}, text);
        string(
            dc, smallFont, L"\u6eda\u8f6e\u5e73\u79fb  \u00b7  F5 \u8bd5\u73a9  \u00b7  F2 \u7f16\u8f91",
            {area.right - 260, 72, area.right - 12, 100}, muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        auto pen = CreatePen(PS_SOLID, 1, border);
        auto old = SelectObject(dc, pen);
        MoveToEx(dc, 0, 29, nullptr);
        LineTo(dc, area.right, 29);
        MoveToEx(dc, 0, 70, nullptr);
        LineTo(dc, area.right, 70);
        MoveToEx(dc, 0, area.bottom - 26, nullptr);
        LineTo(dc, area.right, area.bottom - 26);
        SelectObject(dc, old);
        DeleteObject(pen);
    } else if (h == workspace) {
        const wchar_t* titles[] = {L"\u56fe\u5757", L"\u5730\u56fe\u4e8b\u4ef6",
                                   L"\u5bf9\u8c61\u6a21\u677f", L"\u5730\u56fe\u53d1\u5c04\u5668",
                                   L"平台机关"};
        string(dc, font, titles[workspaceMode], {12, 6, area.right - 68, 32}, text);
        if (workspaceMode)
            string(dc, smallFont,
                   workspaceMode == 4   ? L"地图区域 · 选择或双击编辑"
                   : workspaceMode == 1 ? L"\u90e8\u7f72\u5728\u5f53\u524d\u5730\u56fe\u7684\u4e8b\u4ef6"
                   : workspaceMode == 2
                       ? L"\u53ef\u590d\u7528\u6a21\u677f \u00b7 \u5355\u51fb\u9009\u62e9"
                       : L"\u5f53\u524d\u5730\u56fe \u00b7 \u73b0\u6709\u53d1\u5c04\u5668",
                   {12, 36, area.right - 10, 64}, muted);
        string(dc, font, L"\u5de5\u7a0b\u5730\u56fe", {12, 384, area.right - 68, 409}, text);
        auto pen = CreatePen(PS_SOLID, 1, border);
        auto old = SelectObject(dc, pen);
        MoveToEx(dc, area.right - 1, 0, nullptr);
        LineTo(dc, area.right - 1, area.bottom);
        MoveToEx(dc, 6, 380, nullptr);
        LineTo(dc, area.right - 6, 380);
        SelectObject(dc, old);
        DeleteObject(pen);
    } else if (h == eventEditor) {
        rounded(dc, {8, 8, 272, 748}, panel, border, 10);
        rounded(dc, {276, 8, 714, 708}, panel, border, 10);
        rounded(dc, {720, 8, 1162, 708}, panel, border, 10);
    } else if (h == objectsEditor) {
        rounded(dc, {8, 8, 282, 744}, panel, border, 10);
        rounded(dc, {288, 8, 762, 744}, panel, border, 10);
        rounded(dc, {774, 8, 1208, 744}, panel, border, 10);
        rounded(dc, {8, 750, 1208, 794}, panel, border, 10);
    } else if (h == focusEditor) {
        rounded(dc, {8, 8, 422, 897}, panel, border, 10);
        rounded(dc, {426, 8, 862, 897}, panel, border, 10);
        rounded(dc, {870, 8, 1310, 897}, panel, border, 10);
    } else if (h == barrageTargetEditor) {
        rounded(dc, {8, 8, 748, 510}, panel, border, 10);
    } else if (h == barrageEditor) {
        rounded(dc, {8, 8, 282, 878}, panel, border, 10);
        rounded(dc, {290, 8, 716, 878}, panel, border, 10);
        rounded(dc, {724, 8, 1300, 878}, panel, border, 10);
    } else if (h == animationsEditor) {
        rounded(dc, {8, 8, 282, 806}, panel, border, 10);
        rounded(dc, {290, 8, 716, 806}, panel, border, 10);
        rounded(dc, {724, 8, 1300, 806}, panel, border, 10);
    } else if (h == assetsEditor) {
        rounded(dc, {8, 8, 482, 694}, panel, border, 10);
        rounded(dc, {490, 8, 1136, 694}, panel, border, 10);
        rounded(dc, {8, 700, 1136, 771}, panel, border, 10);
    } else if (h == transitionEditor) {
        rounded(dc, {8, 8, 327, 520}, panel, border, 10);
        rounded(dc, {338, 8, 918, 520}, panel, border, 10);
    } else if (h == side) {
        int y = -sideScroll;
        auto card = [&](int top, int bottom) {
            rounded(dc, {8, top + y, area.right - 8, bottom + y}, panel, border, 10);
        };
        card(54, 144);
        card(148, 190);
        card(222, 562);
        card(596, 700);
        card(738, 842);
        card(880, 1020);
        card(1048, 1196);
        string(dc, strongFont, L"场景设置", {16, 10 + y, 230, 37 + y}, text);
        RECT track{area.right - 10, 4, area.right - 4, area.bottom - 4};
        rounded(dc, track, field, field, 5);
        int height = std::max(36, int(area.bottom * area.bottom / 1306.0f));
        int top = int(sideScroll * (area.bottom - height) / float(std::max(1L, 1306 - area.bottom)));
        rounded(dc, {track.left, top + 4, track.right, std::min(int(area.bottom) - 4, top + height)},
                darkTheme ? RGB(81, 95, 119) : RGB(173, 186, 204),
                darkTheme ? RGB(81, 95, 119) : RGB(173, 186, 204), 5);
        string(dc, smallFont, L"参数", {170, 10 + y, area.right - 16, 37 + y}, muted,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!supplied)
        EndPaint(h, &paint);
}
void App::setTheme(bool dark) {
    darkTheme = dark;
    auto colors = palette(darkTheme);
    if (windowBrush)
        DeleteObject(windowBrush);
    if (panelBrush)
        DeleteObject(panelBrush);
    if (fieldBrush)
        DeleteObject(fieldBrush);
    windowBrush = CreateSolidBrush(colors[0]);
    panelBrush = CreateSolidBrush(colors[1]);
    fieldBrush = CreateSolidBrush(colors[2]);
    if (window)
        SetClassLongPtrW(window, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(windowBrush));
    if (mapTree && IsWindow(mapTree)) {
        TreeView_SetBkColor(mapTree, colors[1]);
        TreeView_SetTextColor(mapTree, colors[4]);
        TreeView_SetLineColor(mapTree, colors[3]);
        // Native themed inactive selection ignores custom colors on older Windows.
        SetWindowTheme(mapTree, L"", L"");
    }
    refreshChrome();
    for (HWND h : {dialogEditor,          audioEditor,      mapEditor,
                   layerEditor,           eventEditor,      tilePalette,
                   transitionEditor,      databaseEditor,   mapToolsEditor,
                   debuggerEditor,        healthEditor,     objectsEditor,
                   assetsEditor,          animationsEditor, barrageEditor,
                   barrageTargetEditor,   focusEditor,      frameRateEditor,
                   snapshotCompareEditor, collisionEditor,  inputEditor,
                   rewindRulesEditor,     choiceEditor,     mobilityEditor})
        if (h && IsWindow(h))
            RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
void App::refreshChrome() {
    if (!initialized)
        return;
    if (workspace && !refreshingWorkspace)
        refreshWorkspace();
    InvalidateRect(window, nullptr, FALSE);
    InvalidateRect(side, nullptr, FALSE);
    if (workspace)
        InvalidateRect(workspace, nullptr, FALSE);
    for (auto& [id, h] : controls)
        if (id != 0 && id != 999)
            InvalidateRect(h, nullptr, FALSE);
}
void App::scrollSide(int amount) {
    RECT r;
    GetClientRect(side, &r);
    int next = std::clamp(sideScroll + amount, 0, std::max(0, 1306 - int(r.bottom)));
    if (next == sideScroll)
        return;
    int delta = sideScroll - next;
    sideScroll = next;
    ScrollWindowEx(side, 0, delta, nullptr, nullptr, nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    SetScrollPos(side, SB_VERT, sideScroll, TRUE);
}
void App::printUi(HDC dc, HWND target) {
    if (!target)
        target = window;
    paintChrome(target, dc);
    struct Context {
        App* app;
        HDC dc;
        HWND target;
    };
    Context context{this, dc, target};
    if (!resources.published && target == window && IsWindowVisible(side)) {
        RECT r;
        GetWindowRect(side, &r);
        POINT p{r.left, r.top};
        ScreenToClient(window, &p);
        int old = SaveDC(dc);
        SetViewportOrgEx(dc, p.x, p.y, nullptr);
        RECT c;
        GetClientRect(side, &c);
        IntersectClipRect(dc, 0, 0, c.right, c.bottom);
        paintChrome(side, dc);
        RestoreDC(dc, old);
    }
    EnumChildWindows(
        target,
        [](HWND h, LPARAM param) -> BOOL {
            auto& c = *reinterpret_cast<Context*>(param);
            auto& app = *c.app;
            auto [bg, panel, field, border, text, muted, accent, gold] = palette(app.darkTheme);
            HWND parent = GetParent(h);
            if ((parent != c.target && parent != app.workspace && parent != app.side) ||
                h == app.canvas || h == app.side || !(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE))
                return TRUE;
            RECT r;
            GetWindowRect(h, &r);
            POINT p{r.left, r.top};
            ScreenToClient(c.target, &p);
            int width = r.right - r.left, height = r.bottom - r.top;
            int saved = SaveDC(c.dc);
            if (parent == app.side && c.target == app.window) {
                RECT s;
                GetWindowRect(app.side, &s);
                POINT q{s.left, s.top};
                ScreenToClient(app.window, &q);
                IntersectClipRect(c.dc, q.x, q.y, q.x + s.right - s.left, q.y + s.bottom - s.top);
            }
            SetViewportOrgEx(c.dc, p.x, p.y, nullptr);
            IntersectClipRect(c.dc, 0, 0, width, height);
            wchar_t type[64]{};
            GetClassNameW(h, type, 64);
            if (h == app.workspace) {
                app.paintChrome(h, c.dc);
            } else if (std::wstring_view(type) == L"SysTreeView32") {
                SendMessageW(h, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(c.dc), PRF_CLIENT);
            } else if (std::wstring_view(type) == L"EchoHurtPreview") {
                app.paintFocus(c.dc, {0, 0, width, height});
            } else if (std::wstring_view(type) == L"Button") {
                DRAWITEMSTRUCT item{};
                item.CtlType = ODT_BUTTON;
                item.CtlID = GetDlgCtrlID(h);
                item.hwndItem = h;
                item.hDC = c.dc;
                item.rcItem = {0, 0, width, height};
                app.drawControl(item);
            } else if (std::wstring_view(type) == L"ComboBox")
                SendMessageW(h, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(c.dc), PRF_CLIENT);
            else {
                if (std::wstring_view(type) == L"ListBox") {
                    SendMessageW(h, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(c.dc), PRF_CLIENT);
                    RestoreDC(c.dc, saved);
                    return TRUE;
                }
                if (GetDlgCtrlID(h) == BarragePreview) {
                    app.paintBarrage(c.dc, {0, 0, width, height});
                    RestoreDC(c.dc, saved);
                    return TRUE;
                }
                if (GetDlgCtrlID(h) == AnimationPreview) {
                    app.paintAnimation(c.dc, {0, 0, width, height});
                    RestoreDC(c.dc, saved);
                    return TRUE;
                }
                bool edit = std::wstring_view(type) == L"Edit";
                bool root = parent == app.window;
                RECT a{0, 0, width, height};
                FillRect(c.dc, &a, edit ? app.fieldBrush : root ? app.windowBrush : app.panelBrush);
                if (edit) {
                    auto b = CreateSolidBrush(border);
                    FrameRect(c.dc, &a, b);
                    DeleteObject(b);
                    a.left += 7;
                }
                if (GetDlgCtrlID(h) == AssetThumbnail && app.assetThumbnail) {
                    BITMAP bitmap{};
                    GetObjectW(app.assetThumbnail, sizeof(bitmap), &bitmap);
                    auto source = CreateCompatibleDC(c.dc);
                    auto old = SelectObject(source, app.assetThumbnail);
                    BitBlt(c.dc, (width - bitmap.bmWidth) / 2, (height - bitmap.bmHeight) / 2,
                           bitmap.bmWidth, bitmap.bmHeight, source, 0, 0, SRCCOPY);
                    SelectObject(source, old);
                    DeleteDC(source);
                    RestoreDC(c.dc, saved);
                    return TRUE;
                }
                std::wstring value(GetWindowTextLengthW(h) + 1, 0);
                GetWindowTextW(h, value.data(), int(value.size()));
                auto font = reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0));
                string(c.dc, font ? font : app.font, value.c_str(), a,
                       edit && IsWindowEnabled(h) ? text : muted,
                       edit ? DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL
                            : ((GetDlgCtrlID(h) == AssetInfo || GetDlgCtrlID(h) == BarragePreviewInfo ||
                                GetDlgCtrlID(h) == BarrageTargetHint)
                                   ? DT_LEFT | DT_WORDBREAK
                                   : DT_LEFT | DT_VCENTER | DT_SINGLELINE));
            }
            RestoreDC(c.dc, saved);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
}

} // namespace echo
