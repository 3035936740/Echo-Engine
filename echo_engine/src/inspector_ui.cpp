#include "echo/app.hpp"
#include <algorithm>
#include <cmath>
// Drawn scene-settings panel. Values live in the (hidden) native controls of the legacy settings
// window, so every edit goes through the same command handlers as before.
namespace echo {
namespace {
std::vector<std::wstring> comboOptions(HWND combo) {
    std::vector<std::wstring> values;
    int count = combo ? int(SendMessageW(combo, CB_GETCOUNT, 0, 0)) : 0;
    for (int i = 0; i < count; ++i) {
        std::wstring text(SendMessageW(combo, CB_GETLBTEXTLEN, i, 0) + 1, 0);
        SendMessageW(combo, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(text.data()));
        text.resize(wcslen(text.c_str()));
        values.push_back(text);
    }
    return values;
}
Property row(Property::Kind kind, int id, const wchar_t* label, const wchar_t* tip = L"") {
    Property p;
    p.kind = kind;
    p.id = id;
    p.label = label;
    p.tip = tip;
    return p;
}
} // namespace
std::vector<Property> App::sceneProperties() {
    std::vector<Property> list;
    auto section = [&](const wchar_t* text) { list.push_back(row(Property::Section, 0, text)); };
    auto choice = [&](int id, const wchar_t* text, const wchar_t* tip = L"") {
        Property p = row(Property::Choice, id, text, tip);
        p.options = comboOptions(controls.contains(id) ? controls[id] : nullptr);
        // "手感：…" / "弹幕：…" repeat the row label; keep only the value.
        for (auto& o : p.options)
            if (auto cut = o.find(L'：'); cut != std::wstring::npos && cut <= 3)
                o = o.substr(cut + 1);
        p.choice = p.options.empty() ? -1 : selection(id);
        list.push_back(p);
    };
    auto toggle = [&](int id, const wchar_t* text, const wchar_t* tip = L"") {
        Property p = row(Property::Toggle, id, text, tip);
        p.on = controls.contains(id) && SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
        list.push_back(p);
    };
    auto slider = [&](int id, const wchar_t* text, float lo, float hi, float step, const wchar_t* unit = L"") {
        Property p = row(Property::Slider, id, text);
        p.minimum = lo;
        p.maximum = hi;
        p.step = step;
        p.unit = unit;
        try {
            p.value = std::clamp(number(id), lo, hi);
        } catch (...) {
            p.value = lo;
        }
        if (sliderDrag.value == id && sliderDrag.action)
            p.value = std::clamp(std::round(sliderDragValue / step) * step, lo, hi);
        list.push_back(p);
    };
    auto button = [&](int id, const wchar_t* text, const wchar_t* tip = L"") {
        auto p = row(Property::Button, id, text, tip);
        p.on = id >= 1 && id <= 17 && editor.tool == id; // shows which drawing tool is armed
        list.push_back(p);
    };
    auto hint = [&](const wchar_t* text) { list.push_back(row(Property::Hint, 0, text)); };
    section(L"手感与弹幕");
    choice(JumpPreset, L"跳跃手感", L"玩家跑跳参数预设");
    choice(BulletPreset, L"弹幕预设", L"Boss 默认弹幕样式");
    toggle(AutoCollision, L"世界层自动生成碰撞", L"铺设世界层图块时同时写入方形碰撞");
    section(L"环境与光照");
    choice(Environment, L"环境");
    choice(Weather, L"天气");
    choice(Strength, L"天气强度");
    slider(Fog, L"雾浓度", 0, 1, .01f);
    slider(Wind, L"风速", -3, 3, .1f);
    toggle(Sunlight, L"太阳 / 月光与场景灯光");
    toggle(Fireflies, L"黄昏 / 夜晚萤火虫");
    toggle(Fluid, L"流体背景层");
    toggle(Raymarch, L"SDF Raymarch 背景");
    choice(Snap, L"镜头", L"Crisp 吸附像素，Smooth 亚像素平滑");
    button(CustomShader, L"导入自定义背景着色器…");
    section(L"时间回溯");
    toggle(Enabled, L"启用回溯");
    slider(WindowSeconds, L"可回溯时长", 1, 30, .5f, L" 秒");
    choice(RecordHz, L"记录频率");
    section(L"移动平台");
    if (editor.selectedPlatform >= 0 && editor.selectedPlatform < int(editor.level.platforms.count)) {
        slider(PlatformAmplitude, L"振幅", 0, 1024, 8, L" px");
        slider(PlatformSpeed, L"速度", 0, 10, .1f);
    } else
        hint(L"在地图上用「移动平台」工具放置或选中平台后，可在这里调整振幅与速度。");
    button(7, L"放置 / 选择移动平台");
    section(L"场景灯光");
    if (editor.selectedLight >= 0 && editor.selectedLight < int(editor.level.lights.count)) {
        slider(LightRadius, L"半径", 4, 2048, 4, L" px");
        slider(LightIntensity, L"亮度", 0, 8, .1f);
    } else
        hint(L"放置或选中一盏灯光后，可在这里调整半径与亮度。");
    button(9, L"放置 / 选择灯光");
    section(L"区域与碰撞绘制");
    button(15, L"对话区域", L"拖动绘制对话触发区域");
    button(16, L"传送区域", L"拖动绘制传送门，在地图设置中选择目标");
    button(13, L"矩形碰撞", L"拖动定大小");
    button(10, L"圆形碰撞");
    button(11, L"三角碰撞");
    button(14, L"绘制凸轮廓");
    return list;
}
void App::applyProperty(int id, float value) {
    auto fresh = [&] {
        for (const auto& p : sceneProperties())
            if (p.id == id && p.kind != Property::Section && p.kind != Property::Hint)
                return p;
        return row(Property::Hint, 0, L"");
    };
    Property p = fresh();
    switch (p.kind) {
    case Property::Choice: {
        if (p.options.empty())
            return;
        std::vector<ui::MenuItem> items;
        for (int i = 0; i < int(p.options.size()); ++i)
            items.push_back({p.options[i], L"", i + 1, i == p.choice});
        const auto* node = chrome.find(int(WorkspaceAction::Property), id);
        if (!node)
            return;
        auto field = ui::dropdownField(chrome.screenBox(*node), uiScale);
        POINT at{field.x, field.bottom() + ui::dp(3, uiScale)};
        ClientToScreen(window, &at);
        chrome.pressed = {int(WorkspaceAction::Property), id};
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        int chosen = ui::popupMenu(window, at, items, darkTheme, uiFonts);
        chrome.pressed = {};
        InvalidateRect(window, nullptr, FALSE);
        if (!chosen || chosen - 1 == p.choice)
            return;
        SendMessageW(controls[id], CB_SETCURSEL, chosen - 1, 0);
        command(id == RecordHz ? int(Apply) : id, CBN_SELCHANGE);
        break;
    }
    case Property::Toggle:
        SendMessageW(controls[id], BM_SETCHECK, !p.on, 0);
        command(id == Enabled ? int(Apply) : id);
        break;
    case Property::Slider:
        value = std::round(std::clamp(value, p.minimum, p.maximum) / p.step) * p.step;
        setNumber(id, value);
        if (id == Fog || id == Wind)
            command(id, EN_KILLFOCUS);
        else if (id == WindowSeconds)
            command(Apply);
        else if (id == PlatformAmplitude || id == PlatformSpeed)
            command(PlatformApply);
        else if (id == LightRadius || id == LightIntensity)
            command(LightApply);
        break;
    case Property::Button:
        command(id);
        break;
    default:
        break;
    }
    refreshChrome();
}
} // namespace echo
