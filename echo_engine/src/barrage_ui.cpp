#include "echo/app.hpp"
#include "echo/collision.hpp"
#include "echo/objects.hpp"
#include <algorithm>
#include <commctrl.h>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <windowsx.h>
namespace echo {
namespace {
std::string field(HWND h) {
    std::wstring text(GetWindowTextLengthW(h) + 1, 0);
    GetWindowTextW(h, text.data(), int(text.size()));
    text.resize(wcslen(text.c_str()));
    int count =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), count, nullptr, nullptr);
    return out;
}
float real(HWND h) {
    auto text = field(h);
    std::size_t used{};
    float v = std::stof(text, &used);
    if (!std::isfinite(v) || text.find_first_not_of(" \t\r\n", used) != std::string::npos)
        throw std::runtime_error("请输入有效的数字");
    return v;
}
int integer(HWND h, int low, int high) {
    auto v = real(h);
    if (v < low || v > high || std::floor(v) != v)
        throw std::runtime_error("整数超出显示的范围");
    return int(v);
}
std::uint32_t seedValue(HWND h) {
    auto text = field(h);
    std::size_t used{};
    if (text.empty() || text.front() == '-')
        throw std::runtime_error("随机种子需在 1–4294967295 之间");
    auto value = std::stoull(text, &used);
    if (!value || value > UINT32_MAX || text.find_first_not_of(" \t\r\n", used) != std::string::npos)
        throw std::runtime_error("随机种子需在 1–4294967295 之间");
    return std::uint32_t(value);
}
struct Parameter {
    int id;
    const wchar_t* title;
    float BarragePattern::*member;
};
constexpr Parameter parameters[] = {
    {BarrageSpeed, L"速度 px/s：0–4000", &BarragePattern::speed},
    {BarrageAcceleration, L"加速 px/s²：±4000", &BarragePattern::acceleration},
    {BarrageMaxSpeed, L"速度上限 px/s", &BarragePattern::maxSpeed},
    {BarrageLife, L"寿命 s：1/60–60", &BarragePattern::lifetime},
    {BarrageDirection, L"初始方向 °：右为 0", &BarragePattern::direction},
    {BarrageSpread, L"展开角 °：0–360", &BarragePattern::spread},
    {BarrageTurn, L"转向 °/s：±3600", &BarragePattern::turnRate},
    {BarrageTurnSeconds, L"转向持续 s：0–60", &BarragePattern::turnSeconds},
    {BarrageVisualRadius, L"显示半径 px", &BarragePattern::visualRadius},
    {BarrageHitRadius, L"命中半径 px", &BarragePattern::hitRadius},
    {BarrageWobble, L"波浪幅度 px", &BarragePattern::wobble},
    {BarrageFrequency, L"波浪频率 rad/s", &BarragePattern::frequency},
    {BarrageGrowth, L"阵型缩放 /s：±10", &BarragePattern::growth},
    {BarrageRotation, L"阵型旋转 °/s", &BarragePattern::rotation},
    {BarrageRelease, L"释放阵型 s：0保持", &BarragePattern::releaseSeconds},
    {BarrageJitter, L"随机角 ±°：0–180", &BarragePattern::jitter}};
LRESULT CALLBACK previewProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (app && (msg == WM_PAINT || msg == WM_PRINTCLIENT)) {
        PAINTSTRUCT paint{};
        auto dc = msg == WM_PAINT ? BeginPaint(h, &paint) : reinterpret_cast<HDC>(w);
        RECT r;
        GetClientRect(h, &r);
        app->paintBarrage(dc, r);
        if (msg == WM_PAINT)
            EndPaint(h, &paint);
        return 0;
    }
    if (app && msg == WM_TIMER && app->barragePreviewPlaying) {
        app->tickBarragePreview();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if (app && msg == WM_LBUTTONDOWN && app->barragePreviewWorld) {
        RECT r;
        GetClientRect(h, &r);
        auto& k = app->barragePreviewWorld->kino;
        float zoom = std::min((r.right - 16) / 1280.f, (r.bottom - 16) / 720.f);
        k.x = app->barragePreviewX + (GET_X_LPARAM(l) - r.right * .5f) / zoom - k.width * .5f;
        k.y = app->barragePreviewY + (GET_Y_LPARAM(l) - r.bottom * .5f) / zoom - k.height * .5f;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(h, msg, w, l);
}
} // namespace
void App::showBarrageEditor() {
    command(Edit);
    if (barrageEditor && IsWindow(barrageEditor)) {
        if (!verifyMode && !smoke)
            ShowWindow(barrageEditor, SW_SHOW);
        barrageForm();
        SetForegroundWindow(barrageEditor);
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = previewProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"EchoBarragePreview";
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    RegisterClassW(&wc);
    barrageEditor =
        CreateWindowW(L"EchoEngineWindow", L"弹幕 · 模式、波次与发射器",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      35, 25, 1324, 925, window, nullptr, instance, this);
    auto label = [&](const wchar_t* title, int x, int y, int width = 250) {
        control(L"STATIC", title, 0, x, y, width, 23, 0, barrageEditor);
    };
    auto edit = [&](int id, int x, int y, int width = 122) {
        control(L"EDIT", L"", id, x, y, width, 28, WS_BORDER | WS_TABSTOP, barrageEditor);
    };
    auto button = [&](int id, const wchar_t* title, int x, int y, int width = 122, bool check = false) {
        control(L"BUTTON", title, id, x, y, width, 30, WS_TABSTOP | (check ? BS_AUTOCHECKBOX : 0),
                barrageEditor);
    };
    auto combo = [&](int id, const std::vector<std::wstring>& values, int x, int y, int width = 250) {
        auto h =
            control(L"COMBOBOX", L"", id, x, y, width, 250,
                    CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, barrageEditor);
        for (const auto& value : values)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
        SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
        SendMessageW(h, CB_SETCURSEL, 0, 0);
    };
    label(L"模式库 · 可复制复用", 20, 16);
    control(L"LISTBOX", L"", BarragePatterns, 20, 48, 250, 128,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, barrageEditor);
    combo(BarragePreset, {L"环形散射", L"瞄准扇形", L"旋转弧线", L"波浪弹", L"扩散阵型"}, 20, 188);
    button(BarrageNew, L"新建", 20, 229, 75);
    button(BarrageCopy, L"复制", 106, 229, 75);
    button(BarrageDelete, L"删除", 192, 229, 78);
    label(L"发射器 · 部署到地图", 20, 280);
    control(L"LISTBOX", L"", BarrageEmitters, 20, 310, 250, 128,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, barrageEditor);
    button(BarrageEmitterNew, L"新建", 20, 450, 75);
    button(BarrageEmitterCopy, L"复制", 106, 450, 75);
    button(BarrageEmitterDelete, L"删除", 192, 450, 78);
    button(BarrageMapPlace, L"在地图上定位 / 拖动…", 20, 489, 250);
    label(L"轨迹预览 · 点击移动目标", 20, 528);
    auto preview = CreateWindowW(L"EchoBarragePreview", L"", WS_CHILD | WS_VISIBLE, 20, 554, 250, 176,
                                 barrageEditor, reinterpret_cast<HMENU>(BarragePreview), instance, this);
    controls[BarragePreview] = preview;
    SetTimer(preview, 1, 33, nullptr);
    button(BarragePreviewPlay, L"播放 / 暂停", 20, 742, 119);
    button(BarragePreviewReset, L"重置同种子", 151, 742, 119);
    control(L"STATIC", L"", BarragePreviewInfo, 20, 790, 250, 80, 0, barrageEditor);
    label(L"模式属性 · 逻辑像素，角度制", 302, 16, 402);
    edit(BarrageName, 302, 48, 402);
    label(L"形状", 302, 89, 190);
    label(L"瞄准方式", 514, 89, 190);
    combo(BarrageShapeSelect, {L"环形", L"扇形", L"直线束", L"阵型"}, 302, 115, 190);
    combo(BarrageAimSelect, {L"固定方向", L"临发瞄准", L"预警时锁定"}, 514, 115, 190);
    auto tips =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                        0, 0, 0, 0, barrageEditor, nullptr, instance, nullptr);
    SendMessageW(tips, TTM_SETMAXTIPWIDTH, 0, 380);
    int index = 0;
    auto numeric = [&](int id, const wchar_t* title) {
        int x = 302 + (index % 3) * 142, y = 164 + (index / 3) * 62;
        auto caption = control(L"STATIC", title, 0, x, y, 118, 23, 0, barrageEditor);
        SendMessageW(caption, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
        edit(id, x, y + 25, 118);
        for (HWND h : {caption, controls[id]}) {
            TOOLINFOW tool{};
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = barrageEditor;
            tool.uId = reinterpret_cast<UINT_PTR>(h);
            tool.lpszText = const_cast<wchar_t*>(title);
            SendMessageW(tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        ++index;
    };
    numeric(BarrageCount, L"弹数：1–512");
    numeric(BarrageDamage, L"伤害：0–100万");
    numeric(BarrageGroup, L"分组：0–65535");
    numeric(BarragePalette, L"颜色：0–7");
    for (const auto& p : parameters)
        numeric(p.id, p.title);
    label(L"地形碰撞（扫掠；橙线为发射预警）", 302, 618, 402);
    combo(BarrageTerrain, {L"穿过地形", L"仅固体", L"固体 + 单向平台"}, 302, 645, 402);
    label(L"满池策略 · 对所有来源生效", 302, 690, 402);
    combo(BarrageOverflow, {L"拒绝新弹", L"回收最旧弹（年龄 / ID）", L"仅回收同组最旧弹"}, 302, 717,
          402);
    button(BarragePatternApply, L"应用模式与满池策略", 302, 770, 402);
    button(BarrageUndo, L"撤销 Ctrl+Z", 302, 809, 194);
    button(BarrageRedo, L"重做 Ctrl+Y", 510, 809, 194);
    label(L"Ctrl+S 保存；F5 以最新定义重启。", 302, 849, 402);
    label(L"发射器 · 对象原点 + 偏移", 736, 16, 552);
    edit(BarrageEmitterName, 736, 48, 552);
    label(L"所属地图", 736, 89, 266);
    label(L"位置参考", 1022, 89, 266);
    std::vector<std::wstring> names;
    for (const auto& name : config.maps)
        names.push_back(wide(name));
    combo(BarrageMap, names, 736, 115, 266);
    combo(BarrageOriginSelect, {L"世界坐标", L"玩家位置 + 偏移", L"Boss位置 + 偏移", L"地图对象 + 偏移"},
          1022, 115, 266);
    label(L"X / Y：逻辑 px", 736, 156, 266);
    label(L"跟随对象（选中的地图）", 1022, 156, 266);
    edit(BarrageX, 736, 182, 122);
    edit(BarrageY, 880, 182, 122);
    combo(BarrageObject, {L"无"}, 1022, 182, 266);
    label(L"开关条件（关闭时暂停游标）", 736, 223, 266);
    label(L"随机种子：1–4294967295", 1022, 223, 266);
    std::vector<std::wstring> gates{L"始终执行"};
    for (int i = 0; i < 128; ++i)
        gates.push_back(wide(dataLabel(database, false, i)));
    combo(BarrageGate, gates, 736, 249, 266);
    edit(BarrageSeed, 1022, 249, 266);
    label(L"启动延迟 s：0–3600", 736, 290, 266);
    label(L"循环间隔 s：≥1/60", 1022, 290, 266);
    edit(BarrageDelay, 736, 316, 266);
    edit(BarrageLoopDelay, 1022, 316, 266);
    button(BarrageEnabled, L"启用", 736, 359, 168, true);
    button(BarrageLoop, L"循环", 928, 359, 168, true);
    button(BarragePersistent, L"重进保留进度", 1120, 359, 168, true);
    button(BarrageEmitterApply, L"应用发射器部署", 736, 404, 266);
    button(BarrageTargetEditor, L"瞄准目标与阵营…", 1022, 404, 266);
    label(L"波次顺序 · 延迟 → 预警 → 重复发射", 736, 448, 552);
    control(L"LISTBOX", L"", BarrageWaves, 736, 477, 552, 105,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, barrageEditor);
    button(BarrageWaveNew, L"加一波", 736, 594, 126);
    button(BarrageWaveDelete, L"删除波", 878, 594, 126);
    button(BarrageWaveUp, L"上移", 1020, 594, 126);
    button(BarrageWaveDown, L"下移", 1162, 594, 126);
    combo(BarrageWavePattern, {}, 736, 639, 552);
    for (auto [id, title, x, y] : {std::tuple{BarrageWaveRepeat, L"重复：1–256", 736, 681},
                                   std::tuple{BarrageWaveDelay, L"首发延迟 s", 928, 681},
                                   std::tuple{BarrageWarning, L"预警 s：伤害弹≥.85", 1120, 681},
                                   std::tuple{BarrageInterval, L"发射间隔 s", 736, 748},
                                   std::tuple{BarrageAngleStep, L"每次递增角 °", 928, 748}}) {
        label(title, x, y, 168);
        edit(id, x, y + 25, 168);
    }
    button(BarrageWaveApply, L"应用当前波次", 1120, 773, 168);
    label(L"模式最多128个，发射器64个，波次32个；弹池2048。", 736, 826, 552);
    label(L"对象被销毁时暂停发射，不使用失效位置。无需 Lua。", 736, 852, 552);
    barrageForm();
    resetBarragePreview();
    refreshChrome();
}
void App::showBarrageTargetEditor() {
    if (barrageEmitterIndex < 0)
        return;
    if (barrageTargetEditor && IsWindow(barrageTargetEditor)) {
        SetForegroundWindow(barrageTargetEditor);
        return;
    }
    barrageTargetEditor =
        CreateWindowW(L"EchoEngineWindow", L"弹幕瞄准 · 目标与对象阵营",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      150, 110, 774, 555, barrageEditor, nullptr, instance, this);
    auto label = [&](const wchar_t* text, int x, int y, int width = 340) {
        control(L"STATIC", text, 0, x, y, width, 23, 0, barrageTargetEditor);
    };
    auto edit = [&](int id, int x, int y, int width = 340) {
        control(L"EDIT", L"", id, x, y, width, 29, WS_BORDER | WS_TABSTOP, barrageTargetEditor);
    };
    auto combo = [&](int id, std::initializer_list<const wchar_t*> values, int x, int y) {
        auto h = control(L"COMBOBOX", L"", id, x, y, 340, 250,
                         CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                         barrageTargetEditor);
        for (auto value : values)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
    };
    label(L"目标类型 · 固定方向模式不查询目标", 24, 24);
    combo(BarrageTargetSelect,
          {L"玩家判定中心", L"Boss 中心", L"指定地图对象", L"最近的地图对象", L"固定世界坐标"}, 24, 52);
    label(L"指定对象 · 使用已应用的所属地图", 390, 24);
    combo(BarrageTargetObject, {}, 390, 52);
    label(L"对象阵营过滤 · 指定 / 最近对象有效", 24, 105);
    combo(BarrageTargetFilter, {L"任意阵营", L"只选择该阵营", L"排除该阵营"}, 24, 133);
    label(L"阵营编号：0–65535（精确匹配）", 390, 105);
    edit(BarrageTargetFaction, 390, 133);
    label(L"最近对象半径 px：0不限，最大16384", 24, 186);
    edit(BarrageTargetRange, 24, 214);
    control(L"BUTTON", L"排除发射器的跟随对象", BarrageTargetExclude, 390, 214, 340, 29,
            WS_TABSTOP | BS_AUTOCHECKBOX, barrageTargetEditor);
    label(L"固定坐标 X / Y：逻辑 px，±8192", 24, 267, 706);
    edit(BarrageTargetX, 24, 295);
    edit(BarrageTargetY, 390, 295);
    control(L"STATIC", L"", BarrageTargetHint, 24, 350, 706, 84, 0, barrageTargetEditor);
    control(L"BUTTON", L"应用目标配置并重置预览", BarrageTargetApply, 24, 458, 706, 32, WS_TABSTOP,
            barrageTargetEditor);
    barrageTargetForm();
    refreshChrome();
}
void App::barrageTargetForm(bool load) {
    if (!barrageTargetEditor || barrageEmitterIndex < 0)
        return;
    const auto& e = editor.barrage.emitters.at(barrageEmitterIndex);
    if (load) {
        SendMessageW(controls[BarrageTargetSelect], CB_SETCURSEL, int(e.target), 0);
        SendMessageW(controls[BarrageTargetFilter], CB_SETCURSEL, int(e.factionFilter), 0);
        setNumber(BarrageTargetFaction, e.targetFaction);
        setNumber(BarrageTargetRange, e.targetRange);
        setNumber(BarrageTargetX, e.targetPoint.x);
        setNumber(BarrageTargetY, e.targetPoint.y);
        SendMessageW(controls[BarrageTargetExclude], BM_SETCHECK, e.excludeParent, 0);
        auto h = controls[BarrageTargetObject];
        SendMessageW(h, CB_RESETCONTENT, 0, 0);
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"未选择"));
        SendMessageW(h, CB_SETITEMDATA, 0, 0);
        int selected = 0;
        const auto& level = e.map == editMap ? editor.level : maps.at(e.map);
        for (const auto& o : level.objects) {
            auto t = findObjectTemplate(objectLibrary, o.templateId);
            auto title = L"#" + std::to_wstring(o.id) + L" " + (t ? wide(t->name.c_str()) : L"缺失模板");
            auto index = SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
            SendMessageW(h, CB_SETITEMDATA, index, LPARAM(o.id));
            if (o.id == e.targetObject)
                selected = int(index);
        }
        if (e.targetObject && !selected) {
            auto title = L"缺失对象 #" + std::to_wstring(e.targetObject);
            selected = int(SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str())));
            SendMessageW(h, CB_SETITEMDATA, selected, LPARAM(e.targetObject));
        }
        SendMessageW(h, CB_SETCURSEL, selected, 0);
    }
    auto type = BarrageTarget(selection(BarrageTargetSelect));
    bool objects = type == BarrageTarget::Object || type == BarrageTarget::NearestObject;
    EnableWindow(controls[BarrageTargetObject], type == BarrageTarget::Object);
    EnableWindow(controls[BarrageTargetFilter], objects);
    EnableWindow(controls[BarrageTargetFaction], objects && selection(BarrageTargetFilter) != 0);
    EnableWindow(controls[BarrageTargetExclude], objects);
    EnableWindow(controls[BarrageTargetRange], type == BarrageTarget::NearestObject);
    EnableWindow(controls[BarrageTargetX], type == BarrageTarget::Point);
    EnableWindow(controls[BarrageTargetY], type == BarrageTarget::Point);
    SetWindowTextW(controls[BarrageTargetHint],
                   L"预警锁定：保存当时坐标，目标离开后仍按原方向发射。\n"
                   L"临发瞄准：目标丢失时暂停；恢复后重新完整预警。\n"
                   L"阵营过滤用于选择瞄准对象；命中伤害路由仍按现有战斗规则。");
}
void App::barrageForm() {
    if (!barrageEditor)
        return;
    loadingBarrage = true;
    auto list = [&](int id, const std::vector<std::wstring>& names, int selected, bool drop = false) {
        auto h = controls[id];
        SendMessageW(h, drop ? CB_RESETCONTENT : LB_RESETCONTENT, 0, 0);
        for (const auto& name : names)
            SendMessageW(h, drop ? CB_ADDSTRING : LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(name.c_str()));
        SendMessageW(h, drop ? CB_SETCURSEL : LB_SETCURSEL, selected, 0);
    };
    auto& l = editor.barrage;
    barragePatternIndex =
        l.patterns.empty() ? -1 : std::clamp(barragePatternIndex, 0, int(l.patterns.size()) - 1);
    barrageEmitterIndex =
        l.emitters.empty() ? -1 : std::clamp(barrageEmitterIndex, 0, int(l.emitters.size()) - 1);
    std::vector<std::wstring> patterns, emitters;
    for (const auto& p : l.patterns)
        patterns.push_back(wide(p.name.c_str()));
    for (const auto& e : l.emitters)
        emitters.push_back(L"图" + std::to_wstring(e.map + 1) + L" · " + wide(e.name.c_str()));
    list(BarragePatterns, patterns, barragePatternIndex);
    list(BarrageEmitters, emitters, barrageEmitterIndex);
    list(BarrageWavePattern, patterns, 0, true);
    for (int id = BarrageName; id <= BarragePatternApply; ++id)
        EnableWindow(controls[id], barragePatternIndex >= 0);
    EnableWindow(controls[BarrageEmitterNew], barragePatternIndex >= 0);
    EnableWindow(controls[BarrageEmitterCopy], barrageEmitterIndex >= 0);
    EnableWindow(controls[BarrageMapPlace], barrageEmitterIndex >= 0);
    EnableWindow(controls[BarrageUndo], editor.undoCount() > 0);
    EnableWindow(controls[BarrageRedo], editor.redoCount() > 0);
    EnableWindow(controls[BarrageTargetEditor], barrageEmitterIndex >= 0);
    EnableWindow(controls[BarrageEmitterDelete], barrageEmitterIndex >= 0);
    for (int id = BarrageEmitterName; id <= BarrageWaveApply; ++id)
        EnableWindow(controls[id], barrageEmitterIndex >= 0);
    if (barragePatternIndex >= 0) {
        const auto& p = l.patterns[barragePatternIndex];
        SetWindowTextW(controls[BarrageName], wide(p.name.c_str()).c_str());
        SendMessageW(controls[BarrageShapeSelect], CB_SETCURSEL, int(p.shape), 0);
        SendMessageW(controls[BarrageAimSelect], CB_SETCURSEL, int(p.aim), 0);
        SendMessageW(controls[BarrageTerrain], CB_SETCURSEL, p.terrain, 0);
        setNumber(BarrageCount, p.count);
        setNumber(BarrageDamage, p.damage);
        setNumber(BarrageGroup, p.group);
        setNumber(BarragePalette, p.palette);
        for (const auto& param : parameters)
            setNumber(param.id, p.*param.member);
    }
    SendMessageW(controls[BarrageOverflow], CB_SETCURSEL, int(l.overflow), 0);
    if (barrageEmitterIndex >= 0) {
        const auto& e = l.emitters[barrageEmitterIndex];
        SetWindowTextW(controls[BarrageEmitterName], wide(e.name.c_str()).c_str());
        SendMessageW(controls[BarrageMap], CB_SETCURSEL, e.map, 0);
        SendMessageW(controls[BarrageOriginSelect], CB_SETCURSEL, int(e.origin), 0);
        setNumber(BarrageX, e.position.x);
        setNumber(BarrageY, e.position.y);
        SendMessageW(controls[BarrageGate], CB_SETCURSEL, e.gateSwitch + 1, 0);
        SetWindowTextW(controls[BarrageSeed], std::to_wstring(e.seed).c_str());
        setNumber(BarrageDelay, e.startDelay);
        setNumber(BarrageLoopDelay, e.loopDelay);
        SendMessageW(controls[BarrageEnabled], BM_SETCHECK, e.enabled, 0);
        SendMessageW(controls[BarrageLoop], BM_SETCHECK, e.loop, 0);
        SendMessageW(controls[BarragePersistent], BM_SETCHECK, e.persistent, 0);
        auto h = controls[BarrageObject];
        SendMessageW(h, CB_RESETCONTENT, 0, 0);
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"无"));
        SendMessageW(h, CB_SETITEMDATA, 0, 0);
        int selected = 0;
        const auto& level = e.map == editMap ? editor.level : maps[e.map];
        for (const auto& o : level.objects) {
            const auto* t = findObjectTemplate(objectLibrary, o.templateId);
            auto label = L"#" + std::to_wstring(o.id) + L" " + (t ? wide(t->name.c_str()) : L"缺失模板");
            auto index = SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(h, CB_SETITEMDATA, index, LPARAM(o.id));
            if (o.id == e.object)
                selected = int(index);
        }
        SendMessageW(h, CB_SETCURSEL, selected, 0);
        EnableWindow(h, e.origin == BarrageOrigin::Object);
        std::vector<std::wstring> waves;
        for (std::size_t i = 0; i < e.waves.size(); ++i) {
            const auto& wave = e.waves[i];
            auto p = findBarragePattern(l, wave.pattern);
            waves.push_back(std::to_wstring(i + 1) + L" · " + (p ? wide(p->name.c_str()) : L"缺失模式") +
                            L" ×" + std::to_wstring(wave.repeat) + L" · 预警 " +
                            std::to_wstring(wave.warning) + L"s");
        }
        barrageWaveIndex = std::clamp(barrageWaveIndex, 0, int(e.waves.size()) - 1);
        list(BarrageWaves, waves, barrageWaveIndex);
        const auto& wave = e.waves[barrageWaveIndex];
        int selectedPattern = 0;
        for (std::size_t i = 0; i < l.patterns.size(); ++i)
            if (l.patterns[i].id == wave.pattern)
                selectedPattern = int(i);
        SendMessageW(controls[BarrageWavePattern], CB_SETCURSEL, selectedPattern, 0);
        setNumber(BarrageWaveRepeat, wave.repeat);
        setNumber(BarrageWaveDelay, wave.delay);
        setNumber(BarrageWarning, wave.warning);
        setNumber(BarrageInterval, wave.interval);
        setNumber(BarrageAngleStep, wave.angleStep);
    } else
        list(BarrageWaves, {}, -1);
    loadingBarrage = false;
}
void App::resetBarragePreview() {
    barragePreviewPlaying = false;
    barragePreviewLibrary = editor.barrage;
    for (auto& trace : barrageTraces) {
        trace.id = trace.count = trace.head = 0;
    }
    barragePreviewWorld = std::make_unique<WorldState>(*initial_);
    auto& w = *barragePreviewWorld;
    w.bullets.clear();
    w.clock = 0;
    w.bulletsDropped = w.bulletsRecycled = 0;
    BarrageEmitter e;
    if (barrageEmitterIndex >= 0 && barrageEmitterIndex < int(editor.barrage.emitters.size()))
        e = editor.barrage.emitters[barrageEmitterIndex];
    else if (barragePatternIndex >= 0 && barragePatternIndex < int(editor.barrage.patterns.size())) {
        e.id = allocateBarrageId(barragePreviewLibrary);
        e.name.set("Preview");
        e.position = {640, 256};
        e.map = editMap;
        e.waves.push_back({editor.barrage.patterns[barragePatternIndex].id});
    }
    barragePreviewLibrary.emitters.clear();
    if (!e.waves.empty())
        barragePreviewLibrary.emitters.push_back(e);
    w.activeMap = e.map;
    w.level = e.map == editMap ? editor.level : maps.at(e.map);
    w.kino.x = w.level.spawn.x;
    w.kino.y = w.level.spawn.y;
    w.aster.x = w.level.bossSpawn.x;
    w.aster.y = w.level.bossSpawn.y;
    enterObjectMap(w, objectLibrary);
    w.kino.invulnTimer = 100000;
    resetBarrage(w, barragePreviewLibrary);
    barragePreviewX = e.position.x;
    barragePreviewY = e.position.y;
    if (e.origin == BarrageOrigin::Player) {
        barragePreviewX += w.kino.x;
        barragePreviewY += w.kino.y;
    }
    if (e.origin == BarrageOrigin::Boss) {
        barragePreviewX += w.aster.x;
        barragePreviewY += w.aster.y;
    }
    if (e.origin == BarrageOrigin::Object)
        for (const auto& o : w.objects.slots)
            if (o.active && o.placementId == e.object && o.map == w.activeMap) {
                barragePreviewX += o.x;
                barragePreviewY += o.y;
                break;
            }
    barragePreviewX = std::clamp(barragePreviewX, 640.f, mapWidth * tileSize - 640.f);
    barragePreviewY = std::clamp(barragePreviewY, 360.f, mapHeight * tileSize - 360.f);
    barragePreviewStats = {};
    tickBarragePreview();
    InvalidateRect(controls[BarragePreview], nullptr, FALSE);
}
void App::tickBarragePreview() {
    if (!barragePreviewWorld)
        return;
    auto& w = *barragePreviewWorld;
    if (barragePreviewPlaying)
        for (int i = 0; i < 2; ++i) {
            w.clock += fixedDt;
            stepBarrage(w, barragePreviewLibrary, fixedDt);
            stepBullets(w, fixedDt, {w.kino.x, w.kino.y}, barragePreviewStats, nullptr);
            for (std::size_t j = 0; j < std::min(w.bullets.count, 512u); ++j) {
                const auto& bullet = w.bullets[j];
                auto& trace = barrageTraces[j];
                if (trace.id != bullet.id) {
                    trace.id = bullet.id;
                    trace.count = trace.head = 0;
                }
                trace.points[trace.head] = {bullet.x, bullet.y};
                trace.head = (trace.head + 1) % trace.points.size();
                trace.count = std::min(trace.count + 1, std::uint32_t(trace.points.size()));
            }
        }
    std::wostringstream s;
    s.precision(2);
    s << std::fixed << L"时间 " << w.clock << L"s · 弹数 " << w.bullets.count << L"/2048\n"
      << L"拒绝 " << w.bulletsDropped << L" · 回收 " << w.bulletsRecycled << L"\n"
      << L"缺失父对象 " << w.barrage.missingParents << L" · 缺失目标 " << w.barrage.missingTargets
      << L"\n目标查询 " << w.barrage.targetQueries << L" · 候选 " << w.barrage.targetCandidates;
    SetWindowTextW(controls[BarragePreviewInfo], s.str().c_str());
}
void App::paintBarrage(HDC dc, RECT r) {
    FillRect(dc, &r, darkTheme ? windowBrush : fieldBrush);
    if (!barragePreviewWorld)
        return;
    const auto& w = *barragePreviewWorld;
    float zoom = std::min((r.right - 16) / 1280.f, (r.bottom - 16) / 720.f);
    auto x = [&](float v) { return int(std::floor((v - barragePreviewX) * zoom + r.right * .5f)); };
    auto y = [&](float v) { return int(std::floor((v - barragePreviewY) * zoom + r.bottom * .5f)); };
    auto brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    auto pen = CreatePen(PS_SOLID, 1, darkTheme ? RGB(84, 102, 128) : RGB(194, 204, 218));
    auto old = SelectObject(dc, pen);
    for (int row = 0; row < mapHeight; ++row)
        for (int col = 0; col < mapWidth; ++col)
            if (w.level.at(col, row) != Tile::Empty)
                Rectangle(dc, x(col * tileSize), y(row * tileSize), x((col + 1) * tileSize),
                          y((row + 1) * tileSize));
    for (const auto& c : w.level.colliders) {
        if (c.kind == ColliderKind::Circle)
            Ellipse(dc, x(c.bounds.x), y(c.bounds.y), x(c.bounds.x + c.bounds.w),
                    y(c.bounds.y + c.bounds.h));
        else {
            std::array<Vec2, 16> vertices{};
            int n = colliderVertices(c, vertices);
            std::array<POINT, 16> pts{};
            for (int i = 0; i < n; ++i)
                pts[i] = {x(vertices[i].x), y(vertices[i].y)};
            Polygon(dc, pts.data(), n);
        }
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    pen = CreatePen(PS_SOLID, 1, RGB(105, 166, 219));
    old = SelectObject(dc, pen);
    Rectangle(dc, x(w.kino.x), y(w.kino.y), x(w.kino.x + w.kino.width), y(w.kino.y + w.kino.height));
    for (const auto& o : w.objects.slots)
        if (o.active && o.alive && o.map == w.activeMap) {
            auto body = objectBody(w, o);
            Rectangle(dc, x(body.x), y(body.y), x(body.x + body.w), y(body.y + body.h));
        }
    SelectObject(dc, old);
    DeleteObject(pen);
    pen = CreatePen(PS_DOT, 1, RGB(201, 146, 70));
    old = SelectObject(dc, pen);
    for (std::size_t i = 0; i < barragePreviewLibrary.emitters.size(); ++i) {
        const auto& s = w.barrage.emitters[i];
        if (!s.warning)
            continue;
        if (s.targetValid) {
            MoveToEx(dc, x(s.target.x - 18), y(s.target.y), nullptr);
            LineTo(dc, x(s.target.x + 18), y(s.target.y));
            MoveToEx(dc, x(s.target.x), y(s.target.y - 18), nullptr);
            LineTo(dc, x(s.target.x), y(s.target.y + 18));
        }
        int count = std::min(int(s.warningCount), 64);
        for (int j = 0; j < count; ++j) {
            float fraction = s.warningShape == 2 ? 0.f
                             : s.warningShape == 0 || s.warningShape == 3
                                 ? float(j) / count
                                 : (count > 1 ? float(j) / (count - 1) - .5f : 0);
            float angle = s.warningAngle + fraction * s.warningSpread * .01745329252f;
            MoveToEx(dc, x(s.origin.x), y(s.origin.y), nullptr);
            LineTo(dc, x(s.origin.x + std::cos(angle) * 180), y(s.origin.y + std::sin(angle) * 180));
        }
        Ellipse(dc, x(s.origin.x - 20), y(s.origin.y - 20), x(s.origin.x + 20), y(s.origin.y + 20));
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    pen = CreatePen(PS_SOLID, 1, RGB(81, 160, 192));
    old = SelectObject(dc, pen);
    // The preview draw cap only limits visual markers; all 2048 bullets still simulate.
    for (std::size_t i = 0; i < std::min(w.bullets.count, 512u); ++i) {
        const auto& trace = barrageTraces[i];
        if (trace.id != w.bullets[i].id || trace.count < 2)
            continue;
        std::array<POINT, 128> points{};
        auto start = (trace.head + trace.points.size() - trace.count) % trace.points.size();
        for (std::size_t j = 0; j < trace.count; ++j) {
            auto point = trace.points[(start + j) % trace.points.size()];
            points[j] = {x(point.x), y(point.y)};
        }
        Polyline(dc, points.data(), int(trace.count));
    }
    for (std::size_t i = 0; i < std::min(w.bullets.count, 512u); ++i) {
        const auto& b = w.bullets[i];
        int radius = std::max(1, int(b.visualR * zoom));
        Ellipse(dc, x(b.x) - radius, y(b.y) - radius, x(b.x) + radius + 1, y(b.y) + radius + 1);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    pen = CreatePen(PS_SOLID, 1, RGB(214, 112, 119));
    old = SelectObject(dc, pen);
    for (std::size_t i = 0; i < std::min(w.bullets.count, 512u); ++i) {
        const auto& b = w.bullets[i];
        Rectangle(dc, x(b.x - b.hitR), y(b.y - b.hitR), x(b.x + b.hitR) + 1, y(b.y + b.hitR) + 1);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    SelectObject(dc, brush);
}
void App::barrageCommand(int id, int notification) {
    if (id == BarrageEditor) {
        showBarrageEditor();
        return;
    }
    if (!barrageEditor || loadingBarrage)
        return;
    if (id == BarrageUndo || id == BarrageRedo) {
        command(id == BarrageUndo ? Undo : Redo);
        return;
    }
    if (id == BarrageMapPlace) {
        command(Edit);
        finishBrush();
        if (barrageEmitterIndex < 0)
            return;
        const auto& e = editor.barrage.emitters.at(barrageEmitterIndex);
        if (e.map != editMap)
            throw std::runtime_error("请先在地图设置中切换到发射器所属地图");
        if (!editor.armEmitter(e.id))
            throw std::runtime_error("发射器的跟随对象缺失；请先修复原点引用");
        if (barrageTargetEditor)
            SendMessageW(barrageTargetEditor, WM_CLOSE, 0, 0);
        ShowWindow(barrageEditor, SW_HIDE);
        SetForegroundWindow(window);
        SetFocus(canvas);
        message = "左键定位发射器；拖动标记；双击编辑；右键取消定位。";
        refreshChrome();
        return;
    }
    if (id == BarrageTargetEditor) {
        showBarrageTargetEditor();
        return;
    }
    if ((id == BarrageTargetSelect || id == BarrageTargetFilter) && notification == CBN_SELCHANGE) {
        barrageTargetForm(false);
        return;
    }
    if (id == BarragePatterns || id == BarrageEmitters || id == BarrageWaves) {
        if (notification != LBN_SELCHANGE)
            return;
        if (barrageTargetEditor)
            SendMessageW(barrageTargetEditor, WM_CLOSE, 0, 0);
        int index = int(SendMessageW(controls[id], LB_GETCURSEL, 0, 0));
        if (id == BarragePatterns)
            barragePatternIndex = index;
        else if (id == BarrageEmitters) {
            barrageEmitterIndex = index;
            barrageWaveIndex = 0;
        } else
            barrageWaveIndex = index;
        barrageForm();
        resetBarragePreview();
        return;
    }
    if (id == BarrageOriginSelect && notification == CBN_SELCHANGE) {
        EnableWindow(controls[BarrageObject], selection(id) == int(BarrageOrigin::Object));
        return;
    }
    if (id == BarrageMap && notification == CBN_SELCHANGE) {
        int map = selection(id);
        auto h = controls[BarrageObject];
        SendMessageW(h, CB_RESETCONTENT, 0, 0);
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"无"));
        SendMessageW(h, CB_SETITEMDATA, 0, 0);
        const auto& level = map == editMap ? editor.level : maps.at(map);
        for (const auto& o : level.objects) {
            auto title = L"#" + std::to_wstring(o.id);
            auto i = SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
            SendMessageW(h, CB_SETITEMDATA, i, LPARAM(o.id));
        }
        SendMessageW(h, CB_SETCURSEL, 0, 0);
        return;
    }
    if (id == BarragePreviewPlay) {
        barragePreviewPlaying = !barragePreviewPlaying;
        return;
    }
    if (id == BarragePreviewReset) {
        resetBarragePreview();
        return;
    }
    if (notification != BN_CLICKED)
        return;
    bool mutation = id == BarrageNew || id == BarrageCopy || id == BarrageDelete ||
                    id == BarragePatternApply || id == BarrageEmitterNew || id == BarrageEmitterCopy ||
                    id == BarrageEmitterDelete || id == BarrageEmitterApply || id == BarrageWaveNew ||
                    id == BarrageWaveDelete || id == BarrageWaveUp || id == BarrageWaveDown ||
                    id == BarrageWaveApply || id == BarrageTargetApply;
    if (!mutation)
        return;
    command(Edit);
    auto next = editor.barrage;
    int pi = barragePatternIndex, ei = barrageEmitterIndex, wi = barrageWaveIndex;
    if (id == BarrageNew || id == BarrageCopy) {
        auto p = id == BarrageCopy && pi >= 0 ? next.patterns[pi]
                                              : barragePreset(0, selection(BarragePreset));
        p.id = allocateBarrageId(next);
        next.patterns.push_back(p);
        pi = int(next.patterns.size()) - 1;
    } else if (id == BarrageDelete && pi >= 0) {
        auto p = next.patterns[pi].id;
        std::string uses;
        for (const auto& e : next.emitters)
            for (const auto& wave : e.waves)
                if (wave.pattern == p)
                    uses += std::string(e.name.c_str()) + "\n";
        if (!uses.empty())
            throw std::runtime_error("以下发射器正在使用这个弹幕模式：\n" + uses);
        next.patterns.erase(next.patterns.begin() + pi);
    } else if (id == BarragePatternApply && pi >= 0) {
        auto& p = next.patterns[pi];
        auto name = field(controls[BarrageName]);
        if (name.empty() || name.size() >= 64)
            throw std::runtime_error("模式名称需为 1–63 字节（UTF-8）");
        p.name.set(name);
        p.shape = BarrageShape(selection(BarrageShapeSelect));
        p.aim = BarrageAim(selection(BarrageAimSelect));
        p.count = integer(controls[BarrageCount], 1, 512);
        p.damage = integer(controls[BarrageDamage], 0, 1000000);
        p.group = std::uint16_t(integer(controls[BarrageGroup], 0, 65535));
        p.palette = std::uint8_t(integer(controls[BarragePalette], 0, 7));
        p.terrain = std::uint8_t(selection(BarrageTerrain));
        for (const auto& param : parameters)
            p.*param.member = real(controls[param.id]);
        next.overflow = BulletOverflowPolicy(selection(BarrageOverflow));
    } else if (id == BarrageEmitterNew || id == BarrageEmitterCopy) {
        if (pi < 0)
            throw std::runtime_error("请先新建一个弹幕模式");
        BarrageEmitter e;
        if (id == BarrageEmitterCopy && ei >= 0)
            e = next.emitters[ei];
        else {
            e.name.set("新发射器");
            e.map = editMap;
            e.position = {camera.logicalX + 640, camera.logicalY + 360};
            e.waves.push_back({next.patterns[pi].id});
        }
        e.id = allocateBarrageId(next);
        next.emitters.push_back(e);
        ei = int(next.emitters.size()) - 1;
        wi = 0;
    } else if (id == BarrageEmitterDelete && ei >= 0) {
        auto emitterId = next.emitters[ei].id;
        if (eventUsesEmitter(editor.common, emitterId))
            throw std::runtime_error("公共事件正在引用此发射器；请先修改事件指令");
        for (std::size_t map = 0; map < eventMaps.size(); ++map)
            if (eventUsesEmitter(int(map) == editMap ? editor.events : eventMaps[map], emitterId))
                throw std::runtime_error("地图事件正在引用此发射器：" + config.maps[map]);
        next.emitters.erase(next.emitters.begin() + ei);
    } else if (ei >= 0) {
        auto& e = next.emitters[ei];
        wi = std::clamp(wi, 0, int(e.waves.size()) - 1);
        if (id == BarrageTargetApply) {
            e.target = BarrageTarget(selection(BarrageTargetSelect));
            e.factionFilter = BarrageFactionFilter(selection(BarrageTargetFilter));
            auto object = SendMessageW(controls[BarrageTargetObject], CB_GETITEMDATA,
                                       selection(BarrageTargetObject), 0);
            if (object == CB_ERR)
                throw std::runtime_error("请选择目标对象");
            e.targetObject = std::uint64_t(object);
            e.targetFaction = std::uint16_t(integer(controls[BarrageTargetFaction], 0, 65535));
            e.targetRange = real(controls[BarrageTargetRange]);
            e.targetPoint = {real(controls[BarrageTargetX]), real(controls[BarrageTargetY])};
            e.excludeParent =
                SendMessageW(controls[BarrageTargetExclude], BM_GETCHECK, 0, 0) == BST_CHECKED;
        } else if (id == BarrageEmitterApply) {
            auto name = field(controls[BarrageEmitterName]);
            if (name.empty() || name.size() >= 64)
                throw std::runtime_error("发射器名称需为 1–63 字节（UTF-8）");
            e.name.set(name);
            e.map = selection(BarrageMap);
            e.origin = BarrageOrigin(selection(BarrageOriginSelect));
            e.object = std::uint64_t(
                SendMessageW(controls[BarrageObject], CB_GETITEMDATA, selection(BarrageObject), 0));
            e.position = {real(controls[BarrageX]), real(controls[BarrageY])};
            e.gateSwitch = selection(BarrageGate) - 1;
            e.seed = seedValue(controls[BarrageSeed]);
            e.startDelay = real(controls[BarrageDelay]);
            e.loopDelay = real(controls[BarrageLoopDelay]);
            e.enabled = SendMessageW(controls[BarrageEnabled], BM_GETCHECK, 0, 0) == BST_CHECKED;
            e.loop = SendMessageW(controls[BarrageLoop], BM_GETCHECK, 0, 0) == BST_CHECKED;
            e.persistent = SendMessageW(controls[BarragePersistent], BM_GETCHECK, 0, 0) == BST_CHECKED;
        } else if (id == BarrageWaveNew) {
            if (pi < 0)
                return;
            e.waves.insert(e.waves.begin() + wi + 1, BarrageWave{next.patterns[pi].id});
            ++wi;
        } else if (id == BarrageWaveDelete) {
            if (e.waves.size() == 1)
                throw std::runtime_error("至少保留一个波次");
            e.waves.erase(e.waves.begin() + wi);
        } else if (id == BarrageWaveUp && wi > 0) {
            std::swap(e.waves[wi], e.waves[wi - 1]);
            --wi;
        } else if (id == BarrageWaveDown && wi + 1 < int(e.waves.size())) {
            std::swap(e.waves[wi], e.waves[wi + 1]);
            ++wi;
        } else if (id == BarrageWaveApply) {
            auto index = selection(BarrageWavePattern);
            if (index < 0 || index >= int(next.patterns.size()))
                throw std::runtime_error("请为波次选择弹幕模式");
            auto& wave = e.waves[wi];
            wave.pattern = next.patterns[index].id;
            wave.repeat = integer(controls[BarrageWaveRepeat], 1, 256);
            wave.delay = real(controls[BarrageWaveDelay]);
            wave.warning = real(controls[BarrageWarning]);
            wave.interval = real(controls[BarrageInterval]);
            wave.angleStep = real(controls[BarrageAngleStep]);
        }
    }
    validateBarrage(next, int(config.maps.size()));
    if (saveBarrage(next) != saveBarrage(editor.barrage)) {
        editor.end();
        editor.begin();
        editor.barrage = std::move(next);
        editor.dirty = true;
        editor.end();
    }
    barragePatternIndex = pi;
    barrageEmitterIndex = ei;
    barrageWaveIndex = wi;
    if (barrageTargetEditor)
        SendMessageW(barrageTargetEditor, WM_CLOSE, 0, 0);
    barrageForm();
    resetBarragePreview();
    message = "弹幕已应用，F5 试玩将使用新时间线";
    refreshChrome();
}
} // namespace echo
