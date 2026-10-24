#include "echo/app.hpp"
#include <bit>
#include <cmath>
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
namespace echo {
namespace {
using Palette = std::array<COLORREF, 8>;
// Dialogs that still use native controls share the drawn chrome's theme tokens.
Palette palette(bool dark) {
    const auto& t = ui::theme(dark);
    return {ui::colorref(t.window), ui::colorref(t.panel), ui::colorref(t.sunken), ui::colorref(t.border),
            ui::colorref(t.text),   ui::colorref(t.muted), ui::colorref(t.accent), ui::colorref(t.warn)};
}

void string(HDC dc, HFONT font, const wchar_t* value, RECT r, COLORREF color,
            UINT align = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &r, align);
    SelectObject(dc, old);
}
ui::Box box(RECT r) {
    return {r.left, r.top, r.right - r.left, r.bottom - r.top};
}
std::wstring className(HWND h) {
    wchar_t type[64]{};
    GetClassNameW(h, type, 64);
    return type;
}
bool hovered(HWND h) {
    return GetPropW(h, L"EchoHover") != nullptr;
}
// Hover tracking shared by the subclassed controls; returns true when the state changed.
bool trackHover(HWND h, UINT m) {
    if (m == WM_MOUSEMOVE && !hovered(h)) {
        SetPropW(h, L"EchoHover", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
        TrackMouseEvent(&track);
        return true;
    }
    if (m == WM_MOUSELEAVE) {
        RemovePropW(h, L"EchoHover");
        return true;
    }
    return false;
}
// Bold companion of the 15 px dialog font (strongFont is reserved for in-game dialogue names).
HFONT dialogBold() {
    static HFONT bold = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    return bold;
}
bool primaryAction(int id) {
    switch (id) {
    case Apply: case PlatformApply: case LightApply: case DialogApply: case AudioApply: case PortalApply:
    case LayerImageApply: case EventApply: case TransitionApply: case DataApply: case ObjectApply:
    case ObjectInstanceApply: case AssetApply: case AnimationApply: case AnimationFrameApply:
    case BarragePatternApply: case BarrageEmitterApply: case BarrageWaveApply: case BarrageTargetApply:
    case FocusApply: case HurtRegionApply: case FrameRateApply: case CollisionApply: case InputApply:
    case RewindRulesApply: case MobilityApply: case FeatureApply: case ChoiceApply:
        return true;
    default:
        return false;
    }
}
bool destructiveAction(int id) {
    switch (id) {
    case EventPageDelete: case EventRemove: case EventDelete: case DataDelete: case ObjectDelete:
    case ObjectInstanceDelete: case AssetDelete: case AnimationDelete: case AnimationFrameRemove:
    case BarrageDelete: case BarrageEmitterDelete: case BarrageWaveDelete: case HurtRegionDelete:
    case ChoiceDelete: case FeatureDelete: case LayerImageClear:
        return true;
    default:
        return false;
    }
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
    auto app = reinterpret_cast<App*>(data);
    if (m == WM_PAINT || m == WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};
        HDC dc = m == WM_PAINT ? BeginPaint(h, &paint) : reinterpret_cast<HDC>(w);
        RECT r;
        GetClientRect(h, &r);
        std::wstring text;
        int selected = int(SendMessageW(h, CB_GETCURSEL, 0, 0));
        if (selected >= 0) {
            text.resize(SendMessageW(h, CB_GETLBTEXTLEN, selected, 0) + 1);
            SendMessageW(h, CB_GETLBTEXT, selected, reinterpret_cast<LPARAM>(text.data()));
            text.resize(wcslen(text.c_str()));
        }
        ui::ControlState state;
        state.hovered = hovered(h);
        state.focused = GetFocus() == h || SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0);
        state.disabled = !IsWindowEnabled(h);
        ui::Painter p;
        p.resize(r.right, r.bottom);
        ui::paintComboFace(p, box(r), text, app->font, ui::theme(app->darkTheme), app->surfaceBehind(h), state);
        p.present(dc, box(r), 0, 0);
        if (m == WM_PAINT)
            EndPaint(h, &paint);
        return 0;
    }
    if (trackHover(h, m))
        InvalidateRect(h, nullptr, FALSE);
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS || m == WM_ENABLE || m == WM_CAPTURECHANGED) {
        // The classic combo draws its focus state outside WM_PAINT; repaint the drawn face after it.
        LRESULT result = DefSubclassProc(h, m, w, l);
        InvalidateRect(h, nullptr, FALSE);
        return result;
    }
    if (m == WM_NCDESTROY) {
        RemovePropW(h, L"EchoHover");
        RemoveWindowSubclass(h, comboProc, 1);
    }
    return DefSubclassProc(h, m, w, l);
}
// Edit and list boxes: soft rounded frame drawn in the non-client area, plus inner padding that
// also centres single-line text vertically.
LRESULT CALLBACK fieldProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto app = reinterpret_cast<App*>(data);
    switch (m) {
    case WM_NCCALCSIZE: {
        LRESULT result = DefSubclassProc(h, m, w, l);
        RECT& c = w ? reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0] : *reinterpret_cast<RECT*>(l);
        auto style = GetWindowLongPtrW(h, GWL_STYLE);
        auto type = className(h);
        bool edit = type == L"Edit", multiline = style & ES_MULTILINE;
        int padX = edit ? 5 : type == L"Static" ? 0 : 2, padY = edit && multiline ? 3 : type == L"Static" ? 0 : 2;
        if (edit && !multiline) {
            TEXTMETRICW metrics{};
            HDC dc = GetDC(h);
            auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
            GetTextMetricsW(dc, &metrics);
            SelectObject(dc, old);
            ReleaseDC(h, dc);
            padY = std::max(1, int(c.bottom - c.top - metrics.tmHeight) / 2);
        }
        c.left += padX;
        c.right -= (style & WS_VSCROLL) ? 0 : padX;
        c.top += padY;
        c.bottom -= (style & WS_HSCROLL) ? 0 : padY;
        c.right = std::max(c.left, c.right);
        c.bottom = std::max(c.top, c.bottom);
        return result;
    }
    case WM_NCPAINT:
        DefSubclassProc(h, m, w, l); // scroll bars
        app->drawFieldFrame(h);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE: {
        LRESULT result = DefSubclassProc(h, m, w, l);
        app->drawFieldFrame(h);
        return result;
    }
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
        if (trackHover(h, m))
            app->drawFieldFrame(h);
        break;
    case WM_NCDESTROY:
        RemovePropW(h, L"EchoHover");
        RemoveWindowSubclass(h, fieldProc, 2);
        break;
    default:
        break;
    }
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
    if (trackHover(h, m))
        InvalidateRect(h, nullptr, FALSE);
    if (m == WM_LBUTTONDOWN || m == WM_KILLFOCUS) {
        // The focus ring is only for focus that arrived from the keyboard (App::dialogKey).
        RemovePropW(h, L"EchoKeyFocus");
        InvalidateRect(h, nullptr, FALSE);
    }
    if (m == WM_NCDESTROY) {
        RemovePropW(h, L"EchoKeyFocus");
        RemovePropW(h, L"EchoChecked");
        RemovePropW(h, L"EchoHover");
        RemoveWindowSubclass(h, buttonProc, 1);
    }
    return DefSubclassProc(h, m, w, l);
}
} // namespace
// Cards painted behind the controls of the older dialogs (client coordinates).
std::vector<RECT> App::cardRects(HWND h) const {
    if (h == eventEditor)
        return {{8, 8, 272, 748}, {276, 8, 714, 708}, {720, 8, 1162, 708}};
    if (h == objectsEditor)
        return {{8, 8, 282, 744}, {288, 8, 762, 744}, {774, 8, 1208, 744}, {8, 750, 1208, 794}};
    if (h == focusEditor)
        return {{8, 8, 422, 897}, {426, 8, 862, 897}, {870, 8, 1310, 897}};
    if (h == barrageTargetEditor)
        return {{8, 8, 748, 510}};
    if (h == barrageEditor)
        return {{8, 8, 282, 878}, {290, 8, 716, 878}, {724, 8, 1300, 878}};
    if (h == animationsEditor)
        return {{8, 8, 282, 806}, {290, 8, 716, 806}, {724, 8, 1300, 806}};
    if (h == assetsEditor)
        return {{8, 8, 482, 694}, {490, 8, 1136, 694}, {8, 700, 1136, 771}};
    if (h == transitionEditor)
        return {{8, 8, 327, 520}, {338, 8, 918, 520}};
    return {};
}
// True when a control sits on a card (or the settings panel) rather than the window background.
bool App::onCard(HWND control) const {
    HWND parent = GetParent(control);
    if (parent == side)
        return true;
    RECT r{};
    GetWindowRect(control, &r);
    POINT centre{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    ScreenToClient(parent, &centre);
    for (const auto& card : cardRects(parent))
        if (PtInRect(&card, centre))
            return true;
    return false;
}
ui::Color App::surfaceBehind(HWND control) const {
    const auto& t = ui::theme(darkTheme);
    return onCard(control) ? t.panel : t.window;
}
void App::drawFieldFrame(HWND h) {
    RECT frame{};
    GetWindowRect(h, &frame);
    int width = frame.right - frame.left, height = frame.bottom - frame.top;
    if (width <= 0 || height <= 0)
        return;
    RECT client{};
    GetClientRect(h, &client);
    POINT origin{0, 0};
    ClientToScreen(h, &origin);
    OffsetRect(&client, origin.x - frame.left, origin.y - frame.top);
    auto style = GetWindowLongPtrW(h, GWL_STYLE);
    ui::ControlState state;
    state.focused = GetFocus() == h;
    state.hovered = hovered(h);
    state.disabled = !IsWindowEnabled(h);
    ui::Painter p;
    p.resize(width, height);
    // Square-ish corners where native scroll bars meet the frame.
    ui::paintFieldFrame(p, {0, 0, width, height}, ui::theme(darkTheme), surfaceBehind(h), state,
                        (style & (WS_VSCROLL | WS_HSCROLL)) ? 3.0f : 6.0f);
    HDC dc = GetWindowDC(h);
    ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
    for (LONG bar : {LONG(OBJID_VSCROLL), LONG(OBJID_HSCROLL)}) {
        SCROLLBARINFO info{};
        info.cbSize = sizeof(info);
        if (GetScrollBarInfo(h, bar, &info) && !(info.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))) {
            RECT r = info.rcScrollBar;
            OffsetRect(&r, -frame.left, -frame.top);
            ExcludeClipRect(dc, r.left, r.top, r.right, r.bottom);
        }
    }
    p.present(dc, {0, 0, width, height}, 0, 0);
    ReleaseDC(h, dc);
}
// Title bars follow the theme on Windows 10 20H1+ (attribute 20; 1809-1909 used 19).
void App::applyTitleBarTheme(HWND h) const {
    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static auto set = [] {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        return dwm ? std::bit_cast<SetAttribute>(GetProcAddress(dwm, "DwmSetWindowAttribute")) : nullptr;
    }();
    if (!set || !h || resources.published) // games keep the system title bar
        return;
    BOOL dark = darkTheme;
    if (FAILED(set(h, 20, &dark, sizeof(dark))))
        set(h, 19, &dark, sizeof(dark));
}
// Dark scroll bars and drop-down lists come from the system's dark visual styles.
void App::themeNativeControl(HWND h) {
    auto type = className(h);
    if (type == L"ComboBox")
        SetWindowTheme(h, darkTheme ? L"DarkMode_CFD" : nullptr, nullptr);
    else if (type == L"Edit" || type == L"ListBox")
        SetWindowTheme(h, darkTheme ? L"DarkMode_Explorer" : nullptr, nullptr);
    else if (type != L"Static")
        return;
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
void App::themeControl(HWND h, int, const wchar_t* type) {
    std::wstring_view kind = type;
    if (kind == L"BUTTON") {
        SetWindowLongPtrW(h, GWL_STYLE, (GetWindowLongPtrW(h, GWL_STYLE) & ~0xf) | BS_OWNERDRAW);
        SetWindowSubclass(h, buttonProc, 1, 0);
    } else if (kind == L"COMBOBOX") {
        SetWindowSubclass(h, comboProc, 1, reinterpret_cast<DWORD_PTR>(this));
        themeNativeControl(h);
    } else if (kind == L"EDIT" || kind == L"LISTBOX" ||
               (kind == L"STATIC" && (GetWindowLongPtrW(h, GWL_STYLE) & WS_BORDER))) {
        SetWindowSubclass(h, fieldProc, 2, reinterpret_cast<DWORD_PTR>(this));
        themeNativeControl(h); // also recomputes the padded client area
    }
}
void App::drawControl(const DRAWITEMSTRUCT& item) {
    const auto& t = ui::theme(darkTheme);
    int width = item.rcItem.right - item.rcItem.left, height = item.rcItem.bottom - item.rcItem.top;
    if (width <= 0 || height <= 0)
        return;
    ui::Painter p;
    p.resize(width, height);
    ui::Box area{0, 0, width, height};
    auto present = [&] { p.present(item.hDC, area, item.rcItem.left, item.rcItem.top); };
    if (item.CtlType == ODT_COMBOBOX || item.CtlType == ODT_LISTBOX) {
        if (item.itemState & ODS_COMBOBOXEDIT) {
            InvalidateRect(item.hwndItem, nullptr, FALSE); // the face is painted by comboProc
            return;
        }
        if (item.itemID == UINT(-1)) {
            p.fill(area, t.sunken);
            present();
            return;
        }
        bool combo = item.CtlType == ODT_COMBOBOX;
        std::wstring text(SendMessageW(item.hwndItem, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, item.itemID, 0) + 1, 0);
        SendMessageW(item.hwndItem, combo ? CB_GETLBTEXT : LB_GETTEXT, item.itemID,
                     reinterpret_cast<LPARAM>(text.data()));
        text.resize(wcslen(text.c_str()));
        ui::ControlState state;
        state.active = item.itemState & ODS_SELECTED;
        state.disabled = (item.itemState & ODS_DISABLED) || !IsWindowEnabled(item.hwndItem);
        // Drop-down lists are popups (panel colour); list boxes follow their own field fill.
        ui::paintListItem(p, area, text, font, t, state,
                          combo ? t.panel : ui::fieldFill(t, surfaceBehind(item.hwndItem)));
        present();
        return;
    }
    int id = item.CtlID;
    ui::Color surface = surfaceBehind(item.hwndItem);
    if (id >= 400 && id < 416) {
        // Floating tile palette cell: thumbnail at an integer scale, number underneath.
        int index = id - 400;
        bool selected = editor.tileIndex == index + 1;
        p.fill(area, surface);
        ui::Box cell = area.inset(2, 2);
        p.round(cell, 7, t.sunken, selected ? t.accent : hovered(item.hwndItem) ? t.borderStrong : t.border,
                selected ? 2.0f : 1.0f);
        ui::Box image = cell.inset(8, 8);
        image.h -= 16;
        int frame = config.tileFrame, columns = frame > 0 ? tileAtlasW / frame : 0;
        if (tileAtlas && columns > 0 && index < columns * (tileAtlasH / frame)) {
            int size = std::min(image.w, image.h);
            if (size >= frame)
                size = size / frame * frame;
            p.image(tileAtlas, index % columns * frame, index / columns * frame, frame, frame,
                    {image.x + (image.w - size) / 2, image.y + (image.h - size) / 2, size, size});
        }
        p.text({cell.x, cell.bottom() - 22, cell.w, 18}, std::to_wstring(index + 1), smallFont,
               selected ? t.accentText : t.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        present();
        return;
    }
    std::wstring text(GetWindowTextLengthW(item.hwndItem) + 1, 0);
    GetWindowTextW(item.hwndItem, text.data(), int(text.size()));
    text.resize(wcslen(text.c_str()));
    ui::ControlState state;
    state.hovered = hovered(item.hwndItem);
    state.pressed = item.itemState & ODS_SELECTED;
    state.disabled = !IsWindowEnabled(item.hwndItem);
    state.active = id >= 1 && id <= 17 && editing && editor.tool == id;
    state.primary = primaryAction(id);
    state.focused = (item.itemState & ODS_FOCUS) && GetPropW(item.hwndItem, L"EchoKeyFocus");
    if (toggle(id)) {
        state.checked = SendMessageW(item.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
        ui::paintCheckBox(p, area, text, font, t, surface, state);
    } else if (destructiveAction(id) && !state.disabled) {
        // Destructive actions: neutral face, red label, red tint on hover.
        ui::Theme danger = t;
        danger.text = t.stop;
        danger.hover = ui::mix(t.raised, t.stop, t.dark ? .16f : .08f);
        danger.pressed = ui::mix(t.raised, t.stop, t.dark ? .24f : .14f);
        ui::paintPushButton(p, area, text, font, danger, surface, state);
    } else
        ui::paintPushButton(p, area, text, state.primary ? dialogBold() : font, t, surface, state);
    present();
}
LRESULT App::colorControl(HDC dc, HWND control) {
    const auto& t = ui::theme(darkTheme);
    auto type = className(control);
    bool well = type == L"Static" && (GetWindowLongPtrW(control, GWL_STYLE) & WS_BORDER);
    bool field = type == L"Edit" || type == L"ComboBox" || type == L"ListBox" || well;
    SetTextColor(dc, ui::colorref((field || type == L"ComboLBox") && IsWindowEnabled(control) ? t.text : t.muted));
    if (type == L"ComboLBox") {
        SetBkColor(dc, ui::colorref(t.panel));
        return reinterpret_cast<LRESULT>(panelBrush);
    }
    if (field) {
        // Same rule as ui::fieldFill: inset on cards, raised on the window background.
        bool card = onCard(control);
        SetBkColor(dc, ui::colorref(card ? t.sunken : t.panel));
        return reinterpret_cast<LRESULT>(card ? fieldBrush : panelBrush);
    }
    bool card = onCard(control);
    SetBkColor(dc, ui::colorref(card ? t.panel : t.window));
    return reinterpret_cast<LRESULT>(card ? panelBrush : windowBrush);
}
void App::paintChrome(HWND h, HDC supplied) {
    PAINTSTRUCT paint{};
    HDC dc = supplied ? supplied : BeginPaint(h, &paint);
    RECT area;
    GetClientRect(h, &area);
    if (h == window && !resources.published) {
        paintWorkspace(dc, supplied ? nullptr : &paint.rcPaint);
        if (!supplied)
            EndPaint(h, &paint);
        return;
    }
    const auto& t = ui::theme(darkTheme);
    if (h == window) {
        FillRect(dc, &area, windowBrush);
        if (!supplied)
            EndPaint(h, &paint);
        return;
    }
    // Older dialogs: background and anti-aliased cards rendered offscreen, then copied once.
    ui::Painter p;
    p.resize(area.right, area.bottom);
    p.fill(box(area), h == side ? t.panel : t.window);
    for (const auto& card : cardRects(h))
        p.round(box(card), 10, t.panel, t.border);
    if (h == side) {
        int y = -sideScroll;
        for (auto [top, bottom] : std::vector<std::pair<int, int>>{
                 {54, 144}, {148, 190}, {222, 562}, {596, 700}, {738, 842}, {880, 1020}, {1048, 1196}})
            p.round({8, top + y, int(area.right) - 16, bottom - top}, 10, t.panel, t.border);
        p.text({16, 10 + y, 214, 27}, L"场景设置", strongFont, t.text);
        p.text({170, 10 + y, int(area.right) - 186, 27}, L"参数", smallFont, t.muted,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        int height = std::max(36, int(area.bottom * area.bottom / 1306.0f));
        int top = int(sideScroll * (area.bottom - height) / float(std::max(1L, 1306 - area.bottom)));
        p.round({int(area.right) - 9, top + 4, 4, std::min(int(area.bottom) - 8 - top, height)}, 2,
                ui::alpha(t.faint, .7f));
    }
    p.present(dc, box(area), 0, 0);
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
    auto backdrop = ui::theme(darkTheme).backdrop;
    renderer.editorBackdrop = {backdrop.r / 255.0f, backdrop.g / 255.0f, backdrop.b / 255.0f};
    chromeTip.hide();
    for (auto& [id, h] : controls)
        if (h && IsWindow(h))
            themeNativeControl(h);
    applyTitleBarTheme(window);
    applyTitleBarTheme(side);
    refreshChrome();
    for (HWND h : {dialogEditor,          audioEditor,      mapEditor,
                   layerEditor,           eventEditor,      tilePalette,
                   transitionEditor,      databaseEditor,   mapToolsEditor,
                   debuggerEditor,        healthEditor,     objectsEditor,
                   assetsEditor,          animationsEditor, barrageEditor,
                   barrageTargetEditor,   focusEditor,      frameRateEditor,
                   snapshotCompareEditor, collisionEditor,  inputEditor,
                   rewindRulesEditor,     choiceEditor,     mobilityEditor})
        if (h && IsWindow(h)) {
            applyTitleBarTheme(h);
            RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
}
void App::refreshChrome() {
    if (!initialized)
        return;
    if (!resources.published && !refreshingWorkspace)
        refreshWorkspace();
    InvalidateRect(window, nullptr, FALSE);
    if (side)
        InvalidateRect(side, nullptr, FALSE);
    // A null HWND would invalidate every window on the desktop.
    for (auto& [id, h] : controls)
        if (h && id != 0 && id != 999)
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
            const auto& t = ui::theme(app.darkTheme);
            HWND parent = GetParent(h);
            if ((parent != c.target && parent != app.side) ||
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
            if (std::wstring_view(type) == L"SysTreeView32") {
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
                bool edit = std::wstring_view(type) == L"Edit";
                bool scrolls = GetWindowLongPtrW(h, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL);
                if (edit || std::wstring_view(type) == L"ListBox") {
                    // Same frame as fieldProc's non-client painting.
                    ui::Painter frame;
                    frame.resize(width, height);
                    ui::ControlState state;
                    state.disabled = !IsWindowEnabled(h);
                    ui::paintFieldFrame(frame, {0, 0, width, height}, t, app.surfaceBehind(h), state,
                                        scrolls ? 3.0f : 6.0f);
                    frame.present(c.dc, {0, 0, width, height}, 0, 0);
                }
                if (std::wstring_view(type) == L"ListBox") {
                    POINT client{0, 0};
                    ClientToScreen(h, &client);
                    SetViewportOrgEx(c.dc, p.x + client.x - r.left, p.y + client.y - r.top, nullptr);
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
                RECT a{0, 0, width, height};
                if (edit)
                    InflateRect(&a, -8, -5);
                else
                    FillRect(c.dc, &a, app.onCard(h) ? app.panelBrush : app.windowBrush);
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
                bool multiline = GetWindowLongPtrW(h, GWL_STYLE) & ES_MULTILINE;
                string(c.dc, font ? font : app.font, value.c_str(), a,
                       ui::colorref(edit && IsWindowEnabled(h) ? t.text : t.muted),
                       edit ? (multiline ? DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL
                                         : DT_LEFT | DT_VCENTER | DT_SINGLELINE)
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
