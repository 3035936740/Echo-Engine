#include "echo/app.hpp"
#include "echo/focus.hpp"
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <windowsx.h>
namespace echo {
namespace {
std::wstring fieldText(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n > 4096)
        throw std::runtime_error("Text exceeds 4096 characters");
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
bool checked(App& a, int id) { return SendMessageW(a.controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED; }
void check(App& a, int id, bool v) { SendMessageW(a.controls[id], BM_SETCHECK, v, 0); }
int integer(App& a, int id, int lo, int hi) {
    float v = a.number(id);
    if (v < lo || v > hi || std::floor(v) != v)
        throw std::runtime_error("Integer outside field range");
    return int(v);
}
LRESULT CALLBACK previewProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto* a = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        a = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
    }
    if (a && (msg == WM_PAINT || msg == WM_PRINTCLIENT)) {
        PAINTSTRUCT p{};
        auto dc = msg == WM_PAINT ? BeginPaint(h, &p) : reinterpret_cast<HDC>(w);
        RECT r;
        GetClientRect(h, &r);
        a->paintFocus(dc, r);
        if (msg == WM_PAINT)
            EndPaint(h, &p);
        return 0;
    }
    if (a && msg == WM_LBUTTONDOWN) {
        RECT r;
        GetClientRect(h, &r);
        float zoom = std::min((r.right - 40) / 120.f, (r.bottom - 30) / 80.f);
        auto& region = a->hurtDraft.regions[a->hurtRegionIndex];
        region.anchorX = region.anchorY = 0;
        region.offsetX = (GET_X_LPARAM(l) - r.right * .5f) / zoom + a->initial_->kino.width * .5f;
        region.offsetY = (GET_Y_LPARAM(l) - r.bottom * .5f) / zoom + a->initial_->kino.height * .5f;
        a->focusForm(false);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(h, msg, w, l);
}
} // namespace
void App::showFocusEditor() {
    command(Edit);
    if (focusEditor && IsWindow(focusEditor)) {
        SetForegroundWindow(focusEditor);
        return;
    }
    hurtDraft = initial_->hurt;
    focusDraft = initial_->focus;
    hurtRegionIndex = 0;
    WNDCLASSW wc{};
    wc.lpfnWndProc = previewProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"EchoHurtPreview";
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    RegisterClassW(&wc);
    focusEditor =
        CreateWindowW(L"EchoEngineWindow", L"受击判定 / 低速 / 画面采样",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      110, 60, 1330, 944, window, nullptr, instance, this);
    auto label = [&](const wchar_t* text, int x, int y, int width = 390) {
        control(L"STATIC", text, 0, x, y, width, 22, 0, focusEditor);
    };
    auto combo = [&](int id, std::initializer_list<const wchar_t*> items, int x, int y,
                     int width = 390) {
        auto h =
            control(L"COMBOBOX", L"", id, x, y, width, 260,
                    CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, focusEditor);
        for (auto item : items)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
    };
    auto edit = [&](int id, int x, int y, int width = 112) {
        control(L"EDIT", L"", id, x, y, width, 29, WS_BORDER | WS_TABSTOP, focusEditor);
    };
    auto toggle = [&](int id, const wchar_t* text, int x, int y, int width = 185) {
        control(L"BUTTON", text, id, x, y, width, 27, WS_TABSTOP, focusEditor);
    };
    auto button = [&](int id, const wchar_t* text, int x, int y, int width = 185) {
        control(L"BUTTON", text, id, x, y, width, 30, WS_TABSTOP, focusEditor);
    };
    label(L"低速移动 · 独立于下穿平台", 20, 18);
    toggle(FocusEnabled, L"开启低速", 20, 49);
    toggle(FocusIndicator, L"显示低速标识", 218, 49);
    label(L"启用方式", 20, 88);
    combo(FocusActivation, {L"按住", L"按一下切换", L"条件满足自动开启"}, 20, 114);
    label(L"使用条件（另叠加地图 / 开关 / 地面限制）", 20, 156);
    combo(FocusCondition, {L"任何正常行动时", L"场上存在弹幕时", L"Boss 存活时", L"指定开关为真时"}, 20,
          182);
    label(L"限定地图", 20, 224, 185);
    label(L"限定开关", 218, 224, 185);
    auto mapsBox =
        control(L"COMBOBOX", L"", FocusMap, 20, 250, 185, 240,
                CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, focusEditor);
    SendMessageW(mapsBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"所有地图"));
    for (auto& name : config.maps) {
        auto text = wide(name);
        SendMessageW(mapsBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    auto switches =
        control(L"COMBOBOX", L"", FocusSwitch, 218, 250, 185, 260,
                CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, focusEditor);
    SendMessageW(switches, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"不限制"));
    for (int i = 0; i < 64; ++i) {
        auto text = std::to_wstring(i + 1) + L" · " + wide(database.switches[i].name.c_str());
        SendMessageW(switches, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    label(L"低速倍率 0.01–1", 20, 292, 185);
    label(L"按键 VK 编号 1–254", 218, 292, 185);
    edit(FocusRatio, 20, 318, 185);
    edit(FocusKey, 218, 318, 185);
    toggle(FocusGround, L"地面允许", 20, 362);
    toggle(FocusAir, L"空中允许", 218, 362);
    toggle(FocusInstant, L"立即限制水平速度", 20, 397, 383);
    label(L"判定范围来源", 20, 443);
    combo(HurtSourceControl,
          {L"优先动画逐帧；无框使用区域 1", L"整个角色矩形", L"自定义多个区域（任意一个命中）"}, 20,
          469);
    label(L"命中检测", 20, 511);
    combo(HurtMethodControl, {L"连续扫掠 · 防止高速穿过", L"当前帧重合 · 允许高速穿越"}, 20, 537);
    toggle(HurtTouching, L"相切也算命中", 20, 580);
    toggle(HurtMirror, L"跟随角色朝向", 218, 580);
    toggle(HurtProjectileBox, L"弹幕按方形判定", 20, 615, 383);
    label(L"预设只替换判定与低速；不会修改地形碰撞。", 20, 657);
    combo(HurtPreset,
          {L"经典弹幕 · 小圆点 + Shift", L"完整角色矩形", L"双圆判定区域", L"胶囊判定", L"自绘凸三角形"},
          20, 686);
    button(HurtPresetApply, L"应用预设到草稿", 20, 733, 383);
    label(L"先应用区域，再应用全部。Ctrl+S 应用并保存。", 20, 787);
    label(L"圆 / 胶囊扫掠精确；椭圆以 32 边近似。", 20, 817);
    button(FocusApply, L"应用全部配置", 20, 856, 383);

    label(L"受击区域 · 固定容量 8（可按伤害类别过滤）", 438, 18);
    combo(HurtRegionList, {L"1"}, 438, 49, 410);
    button(HurtRegionNew, L"新增", 438, 90, 128);
    button(HurtRegionCopy, L"复制", 578, 90, 128);
    button(HurtRegionDelete, L"删除", 718, 90, 130);
    label(L"形状", 438, 139);
    combo(HurtShapeControl, {L"圆形", L"矩形", L"椭圆 · 32 边近似", L"胶囊", L"自绘凸多边形"}, 438, 165,
          410);
    toggle(HurtRegionEnabled, L"区域启用", 438, 208);
    toggle(HurtRegionFocusOnly, L"仅低速时启用", 650, 208, 198);
    toggle(HurtMaskBullet, L"弹幕", 438, 246, 125);
    toggle(HurtMaskContact, L"接触伤害", 578, 246, 125);
    toggle(HurtMaskMelee, L"近战攻击", 718, 246, 130);
    const wchar_t* names[] = {L"锚点 X（宽比例）",  L"锚点 Y（高比例）",  L"偏移 X（逻辑 px）",
                              L"偏移 Y（逻辑 px）", L"圆半径（逻辑 px）", L"宽（逻辑 px）",
                              L"高（逻辑 px）",     L"旋转角度（度）"};
    int ids[] = {HurtAnchorX, HurtAnchorY, HurtOffsetX, HurtOffsetY,
                 HurtRadius,  HurtWidth,   HurtHeight,  HurtAngle};
    for (int i = 0; i < 8; ++i) {
        int x = 438 + (i % 2) * 212, y = 291 + (i / 2) * 69;
        label(names[i], x, y, 198);
        edit(ids[i], x, y + 25, 198);
    }
    label(L"自绘顶点 · 中心相对坐标，逐行 x y；3–16 点", 438, 575, 410);
    control(L"EDIT", L"", HurtVertices, 438, 604, 410, 151,
            ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, focusEditor);
    label(L"凸点按边界顺序排列；凹形请拆成多个区域。", 438, 768, 410);
    label(L"预览点击可定位中心；区域显示放大不改变碰撞。", 438, 800, 410);
    button(HurtRegionApply, L"应用当前区域到草稿", 438, 856, 410);

    label(L"贴图过滤", 882, 18);
    combo(RenderTexture, {L"最近邻 · 像素无处理（默认）", L"双线性", L"三点过滤 · 三角插值"}, 882, 44,
          414);
    label(L"虚拟画布放大", 882, 84);
    combo(RenderUpscale,
          {L"整数最近邻（默认）", L"双线性", L"锐利双线性", L"Catmull–Rom 双三次", L"Lanczos 2",
           L"Lanczos 3"},
          882, 110, 414);
    label(L"抗锯齿 · 与贴图过滤独立", 882, 150);
    combo(RenderAA,
          {L"关闭（默认）", L"FXAA · 快速", L"FXAA · 质量", L"SSAA 2×2 · 4 个样本",
           L"SSAA 4×4 · 16 个样本"},
          882, 176, 414);
    label(L"锐利双线性强度 0–1", 882, 216, 245);
    edit(RenderSharpness, 1156, 212, 140);
    label(L"SSAA 增加场景绘制成本；GPU 缓冲有固定上限。", 882, 255, 414);
    label(L"判定显示时机", 882, 291);
    combo(HurtShowControl,
          {L"关闭显示", L"一直显示", L"低速时显示", L"按住指定键显示", L"有弹幕或受击时显示"}, 882, 317,
          414);
    label(L"显示样式", 882, 357);
    combo(HurtStyleControl, {L"轮廓", L"十字中心", L"填充", L"轮廓 + 十字"}, 882, 383, 414);
    const wchar_t* visualNames[] = {L"显示放大 0.25–8", L"透明度 0–1", L"线宽 0.25–8", L"显示键 VK",
                                    L"红 0–1",          L"绿 0–1",     L"蓝 0–1"};
    int visualIds[] = {HurtDisplayScale, HurtOpacity, HurtOutline, HurtShowKey,
                       HurtRed,          HurtGreen,   HurtBlue};
    for (int i = 0; i < 7; ++i) {
        int x = 882 + (i % 2) * 212, y = 424 + (i / 2) * 61;
        label(visualNames[i], x, y, 198);
        edit(visualIds[i], x, y + 23, 198);
    }
    auto preview = CreateWindowW(L"EchoHurtPreview", L"", WS_CHILD | WS_VISIBLE, 882, 680, 414, 165,
                                 focusEditor, reinterpret_cast<HMENU>(HurtPreview), instance, this);
    controls[HurtPreview] = preview;
    label(L"淡蓝：地形体积；黄：真实受击范围。单位为逻辑 px。", 882, 856, 414);
    focusForm();
}

void App::focusForm(bool all) {
    loadingFocus = true;
    auto choose = [&](int id, int v) { SendMessageW(controls[id], CB_SETCURSEL, v, 0); };
    if (all) {
        auto& f = focusDraft;
        auto& h = hurtDraft;
        check(*this, FocusEnabled, f.enabled);
        check(*this, FocusIndicator, f.showIndicator);
        check(*this, FocusGround, f.ground);
        check(*this, FocusAir, f.air);
        check(*this, FocusInstant, f.instant);
        choose(FocusActivation, int(f.mode));
        choose(FocusCondition, int(f.when));
        choose(FocusMap, f.map + 1);
        choose(FocusSwitch, f.gateSwitch + 1);
        setNumber(FocusRatio, f.speedRatio);
        setNumber(FocusKey, float(f.key));
        choose(HurtSourceControl, int(h.source));
        choose(HurtMethodControl, int(h.method));
        choose(HurtShowControl, int(h.show));
        choose(HurtStyleControl, h.style);
        check(*this, HurtTouching, h.touching);
        check(*this, HurtMirror, h.mirror);
        check(*this, HurtProjectileBox, h.projectileBox);
        setNumber(HurtDisplayScale, h.displayScale);
        setNumber(HurtOpacity, h.opacity);
        setNumber(HurtOutline, h.outline);
        setNumber(HurtShowKey, float(h.showKey));
        setNumber(HurtRed, h.color[0]);
        setNumber(HurtGreen, h.color[1]);
        setNumber(HurtBlue, h.color[2]);
        choose(RenderTexture, renderer.textureFilter);
        choose(RenderUpscale, renderer.upscaleFilter);
        choose(RenderAA, renderer.antiAliasing);
        setNumber(RenderSharpness, renderer.renderSharpness);
    }
    SendMessageW(controls[HurtRegionList], CB_RESETCONTENT, 0, 0);
    for (std::uint32_t i = 0; i < hurtDraft.count; ++i) {
        auto text = std::to_wstring(i + 1) + L" · " + (hurtDraft.regions[i].enabled ? L"启用" : L"关闭");
        SendMessageW(controls[HurtRegionList], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    choose(HurtRegionList, hurtRegionIndex);
    auto& r = hurtDraft.regions[hurtRegionIndex];
    choose(HurtShapeControl, int(r.shape));
    check(*this, HurtRegionEnabled, r.enabled);
    check(*this, HurtRegionFocusOnly, r.focusOnly);
    check(*this, HurtMaskBullet, r.mask & hurtBullets);
    check(*this, HurtMaskContact, r.mask & hurtContact);
    check(*this, HurtMaskMelee, r.mask & hurtMelee);
    setNumber(HurtAnchorX, r.anchorX);
    setNumber(HurtAnchorY, r.anchorY);
    setNumber(HurtOffsetX, r.offsetX);
    setNumber(HurtOffsetY, r.offsetY);
    setNumber(HurtRadius, r.radius);
    setNumber(HurtWidth, r.width);
    setNumber(HurtHeight, r.height);
    setNumber(HurtAngle, r.angle * 180 / 3.14159265359f);
    std::wostringstream points;
    for (auto v : r.vertices)
        points << v.x << L" " << v.y << L"\r\n";
    SetWindowTextW(controls[HurtVertices], points.str().c_str());
    loadingFocus = false;
    InvalidateRect(controls[HurtPreview], nullptr, FALSE);
}
HurtRegion App::readHurtRegion() {
    auto r = hurtDraft.regions[hurtRegionIndex];
    r.shape = HurtShape(selection(HurtShapeControl));
    r.enabled = checked(*this, HurtRegionEnabled);
    r.focusOnly = checked(*this, HurtRegionFocusOnly);
    r.mask = std::uint8_t((checked(*this, HurtMaskBullet) ? hurtBullets : 0) |
                          (checked(*this, HurtMaskContact) ? hurtContact : 0) |
                          (checked(*this, HurtMaskMelee) ? hurtMelee : 0));
    r.anchorX = number(HurtAnchorX);
    r.anchorY = number(HurtAnchorY);
    r.offsetX = number(HurtOffsetX);
    r.offsetY = number(HurtOffsetY);
    r.radius = number(HurtRadius);
    r.width = number(HurtWidth);
    r.height = number(HurtHeight);
    r.angle = number(HurtAngle) * 3.14159265359f / 180;
    r.vertices.clear();
    std::wistringstream points(fieldText(controls[HurtVertices]));
    points >> std::ws;
    while (!points.eof()) {
        Vec2 v;
        if (!(points >> v.x >> v.y) || !r.vertices.push(v))
            throw std::runtime_error("Vertex list: x y pairs; maximum 16");
        points >> std::ws;
    }
    HurtConfig test;
    test.source = HurtSource::Custom;
    test.regions[0] = r;
    validateHurt(test);
    return r;
}
void App::focusCommand(int id, int notification) {
    if (id == FocusEditor) {
        showFocusEditor();
        return;
    }
    if (loadingFocus)
        return;
    if (id == HurtRegionList && notification == CBN_SELCHANGE) {
        hurtRegionIndex = selection(id);
        focusForm(false);
        return;
    }
    if (id == HurtRegionNew || id == HurtRegionCopy) {
        if (hurtDraft.count == 8)
            throw std::runtime_error("Maximum 8 hurt regions");
        auto r = id == HurtRegionCopy ? readHurtRegion() : HurtRegion{};
        hurtRegionIndex = int(hurtDraft.count);
        hurtDraft.regions[hurtDraft.count++] = r;
        focusForm(false);
        return;
    }
    if (id == HurtRegionDelete) {
        if (hurtDraft.count == 1)
            throw std::runtime_error("Keep one region; disable it to receive no matching damage");
        for (std::uint32_t i = std::uint32_t(hurtRegionIndex); i + 1 < hurtDraft.count; ++i)
            hurtDraft.regions[i] = hurtDraft.regions[i + 1];
        hurtDraft.regions[--hurtDraft.count] = {};
        hurtRegionIndex = std::min(hurtRegionIndex, int(hurtDraft.count) - 1);
        focusForm(false);
        return;
    }
    if (id == HurtRegionApply) {
        hurtDraft.regions[hurtRegionIndex] = readHurtRegion();
        focusForm(false);
        message = "Hurt region draft updated; apply all to use";
        return;
    }
    if (id == HurtPresetApply) {
        int preset = selection(HurtPreset);
        if (preset < 0)
            throw std::runtime_error("Select a hurt preset");
        hurtDraft = {};
        focusDraft = {};
        focusDraft.enabled = preset != 1;
        hurtDraft.source = HurtSource::Custom;
        auto& r = hurtDraft.regions[0];
        if (preset == 1) {
            hurtDraft.source = HurtSource::Whole;
            hurtDraft.show = HurtShow::Always;
        }
        if (preset == 2) {
            hurtDraft.count = 2;
            r.offsetY = -12;
            hurtDraft.regions[1] = r;
            hurtDraft.regions[1].offsetY = 4;
        }
        if (preset == 3) {
            r.shape = HurtShape::Capsule;
            r.width = 10;
            r.height = 26;
        }
        if (preset == 4) {
            r.shape = HurtShape::Polygon;
            r.vertices.push({0, -8});
            r.vertices.push({7, 6});
            r.vertices.push({-7, 6});
        }
        hurtRegionIndex = 0;
        focusForm();
        return;
    }
    if (id == FocusApply) {
        auto h = hurtDraft;
        auto f = focusDraft;
        h.regions[hurtRegionIndex] = readHurtRegion();
        f.enabled = checked(*this, FocusEnabled);
        f.showIndicator = checked(*this, FocusIndicator);
        f.ground = checked(*this, FocusGround);
        f.air = checked(*this, FocusAir);
        f.instant = checked(*this, FocusInstant);
        f.mode = FocusMode(selection(FocusActivation));
        f.when = FocusWhen(selection(FocusCondition));
        f.map = selection(FocusMap) - 1;
        f.gateSwitch = selection(FocusSwitch) - 1;
        f.speedRatio = number(FocusRatio);
        f.key = integer(*this, FocusKey, 1, 254);
        h.source = HurtSource(selection(HurtSourceControl));
        h.method = HurtMethod(selection(HurtMethodControl));
        h.show = HurtShow(selection(HurtShowControl));
        h.style = selection(HurtStyleControl);
        h.touching = checked(*this, HurtTouching);
        h.mirror = checked(*this, HurtMirror);
        h.projectileBox = checked(*this, HurtProjectileBox);
        h.displayScale = number(HurtDisplayScale);
        h.opacity = number(HurtOpacity);
        h.outline = number(HurtOutline);
        h.showKey = integer(*this, HurtShowKey, 1, 254);
        h.color = {number(HurtRed), number(HurtGreen), number(HurtBlue)};
        validateFocus(f);
        validateHurt(h);
        if (f.when == FocusWhen::Switch && f.gateSwitch < 0)
            throw std::runtime_error("Select a switch for switch-only focus");
        renderer.setSampling(selection(RenderTexture), selection(RenderUpscale), selection(RenderAA),
                             number(RenderSharpness));
        hurtDraft = initial_->hurt = config.hurt = h;
        focusDraft = initial_->focus = config.focus = f;
        config.textureFilter = renderer.textureFilter;
        config.upscaleFilter = renderer.upscaleFilter;
        config.antiAliasing = renderer.antiAliasing;
        config.renderSharpness = renderer.renderSharpness;
        simulation->reset(*initial_, HistoryClearReason::FocusSettings);
        editor.dirty = true;
        focusForm();
        message = "Hurt / focus / rendering applied; old play history restarted";
        refreshChrome();
        return;
    }
}
void App::paintFocus(HDC dc, RECT area) {
    auto brush = CreateSolidBrush(darkTheme ? RGB(31, 36, 48) : RGB(244, 247, 251));
    FillRect(dc, &area, brush);
    DeleteObject(brush);
    float zoom = std::min((area.right - 40) / 120.f, (area.bottom - 30) / 80.f);
    KinoState k = initial_->kino;
    k.x = k.y = 0;
    k.facing = 1;
    auto x = [&](float v) { return int(std::round(area.right * .5f + (v - k.width * .5f) * zoom)); };
    auto y = [&](float v) { return int(std::round(area.bottom * .5f + (v - k.height * .5f) * zoom)); };
    auto pen = CreatePen(PS_SOLID, 1, RGB(127, 170, 222));
    auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x(0), y(0), x(k.width), y(k.height));
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    auto count = hurtDraft.source == HurtSource::Custom ? hurtDraft.count : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto r = effectiveHurt(k, hurtDraft, i);
        if (!r.enabled)
            continue;
        pen = CreatePen(PS_SOLID, i == std::uint32_t(hurtRegionIndex) ? 2 : 1,
                        darkTheme ? RGB(245, 208, 128) : RGB(168, 111, 34));
        SelectObject(dc, pen);
        auto points = hurtPolygon(k, hurtDraft, r);
        std::array<POINT, 33> poly{};
        for (std::uint32_t j = 0; j < points.count; ++j)
            poly[j] = {x(points[j].x), y(points[j].y)};
        if (points.count) {
            poly[points.count] = poly[0];
            Polyline(dc, poly.data(), int(points.count) + 1);
        }
        auto center = hurtCenter(k, hurtDraft, r);
        MoveToEx(dc, x(center.x) - 4, y(center.y), nullptr);
        LineTo(dc, x(center.x) + 5, y(center.y));
        MoveToEx(dc, x(center.x), y(center.y) - 4, nullptr);
        LineTo(dc, x(center.x), y(center.y) + 5);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    SelectObject(dc, oldBrush);
}
} // namespace echo

namespace echo {
void App::frameRateCommand(int id) {
    constexpr int rates[] = {30, 60, 90, 120, 144, 0, -1};
    if (id == FrameRateEditor) {
        if (frameRateEditor && IsWindow(frameRateEditor)) {
            SetForegroundWindow(frameRateEditor);
            return;
        }
        frameRateEditor = CreateWindowW(L"EchoEngineWindow", L"绘制帧率与垂直同步",
                                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                            ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                                        240, 180, 620, 450, window, nullptr, instance, this);
        auto label = [&](const wchar_t* text, int y) {
            control(L"STATIC", text, 0, 24, y, 558, 25, 0, frameRateEditor);
        };
        label(L"仅限制绘制；物理固定 60 Hz，历史独立采样。", 22);
        auto combo = control(L"COMBOBOX", L"", FrameRatePreset, 24, 64, 558, 260,
                             CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                             frameRateEditor);
        for (auto text : {L"30 FPS", L"60 FPS", L"90 FPS", L"120 FPS", L"144 FPS", L"无限制", L"自定义"})
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        int selected = 6;
        for (int i = 0; i < 6; ++i)
            if (config.frameLimit == rates[i])
                selected = i;
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
        label(L"自定义上限 1–1000 FPS（整数）", 111);
        control(L"EDIT", L"", FrameRateCustom, 24, 147, 558, 30, WS_BORDER | WS_TABSTOP,
                frameRateEditor);
        setNumber(FrameRateCustom, config.frameLimit ? float(config.frameLimit) : 60);
        control(L"BUTTON", L"垂直同步", FrameVsync, 24, 201, 558, 31, WS_TABSTOP, frameRateEditor);
        SendMessageW(controls[FrameVsync], BM_SETCHECK, config.vsync, 0);
        label(L"开启同步时，还受显示器刷新率与驱动设置限制。", 248);
        label(L"无限制移除软件等待；不保证设备达到指定帧率。", 282);
        control(L"BUTTON", L"应用帧率配置", FrameRateApply, 24, 343, 558, 36, WS_TABSTOP,
                frameRateEditor);
        EnableWindow(controls[FrameRateCustom], selected == 6);
        return;
    }
    if (id == FrameRatePreset) {
        EnableWindow(controls[FrameRateCustom], selection(id) == 6);
        return;
    }
    if (id == FrameRateApply) {
        int selected = selection(FrameRatePreset);
        if (selected < 0 || selected > 6)
            throw std::runtime_error("Select frame rate mode");
        int fps = selected == 6 ? integer(*this, FrameRateCustom, 1, 1000) : rates[selected];
        bool vsync = checked(*this, FrameVsync);
        bool available = renderer.setVsync(vsync);
        config.frameLimit = fps;
        config.vsync = vsync;
        editor.dirty = true;
        message = available ? "Render frame limit applied"
                            : "Software frame limit applied; driver VSync control unavailable";
        refreshChrome();
        return;
    }
}
} // namespace echo
