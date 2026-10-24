#include "echo/app.hpp"
#include "echo/transition.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace echo {
void App::showTransitionEditor() {
    command(Edit);
    if (transitionEditor && IsWindow(transitionEditor)) {
        ShowWindow(transitionEditor, SW_SHOW);
        SetForegroundWindow(transitionEditor);
        return;
    }
    transitionScope = 0;
    transitionPortal = editor.selectedPortal;
    if (transitionPortal >= 0 && transitionPortal < int(editor.level.portals.count))
        transitionPortalRegion = editor.level.portals[transitionPortal].region;
    transitionEditor =
        CreateWindowW(L"EchoEngineWindow", L"切图 / 重开动画",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      170, 100, 970, 660, window, nullptr, instance, this);
    auto label = [&](const wchar_t* s, int x, int y, int width = 280) {
        control(L"STATIC", s, 0, x, y, width, 23, 0, transitionEditor);
    };
    auto box = [&](int id, const std::vector<std::wstring>& items, int x, int y, int width) {
        auto h = control(L"COMBOBOX", L"", id, x, y, width, 260,
                         CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                         transitionEditor);
        for (const auto& item : items)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
    };
    auto edit = [&](int id, int x, int y, int width = 100) {
        control(L"EDIT", L"", id, x, y, width, 30, WS_BORDER | WS_TABSTOP, transitionEditor);
    };
    label(L"配置范围", 20, 20);
    box(TransitionScope, {L"作品默认 · 切换地图", L"作品默认 · 死亡 / 手动重开", L"当前选中的传送区域"},
        20, 49, 290);
    label(L"预设 · 选择「无」就是立即切换", 20, 101, 300);
    box(TransitionPreset,
        {L"无动画", L"淡入淡出", L"圆形收拢 / 展开", L"菱形收拢 / 展开", L"双侧幕帘", L"斜向擦除",
         L"像素块消隐", L"自定义 GLSL 遮罩"},
        20, 130, 290);
    label(L"遮住 / 停留 / 显露（秒）", 20, 181, 290);
    edit(TransitionClose, 20, 213, 86);
    edit(TransitionHold, 118, 213, 86);
    edit(TransitionOpen, 216, 213, 94);
    label(L"缓动", 20, 264);
    box(TransitionEasing, {L"线性", L"平滑", L"缓入", L"缓出"}, 20, 293, 290);
    label(L"方向 · 圆形 / 菱形忽略此项", 20, 342, 300);
    box(TransitionDirection,
        {L"向右 / 横向幕帘", L"向左 / 横向幕帘", L"向下 / 纵向幕帘", L"向上 / 纵向幕帘"}, 20, 371, 290);
    control(L"BUTTON", L"圆形 / 菱形以玩家为中心", TransitionCenter, 20, 423, 290, 29, WS_TABSTOP,
            transitionEditor);
    control(L"BUTTON", L"覆盖作品默认切图动画", TransitionOverride, 20, 473, 290, 29, WS_TABSTOP,
            transitionEditor);
    label(L"遮罩颜色 R / G / B（0..1）", 350, 20, 550);
    edit(TransitionRed, 350, 49, 160);
    edit(TransitionGreen, 524, 49, 160);
    edit(TransitionBlue, 698, 49, 160);
    label(L"像素块尺寸 / 自定义参数（1..64）", 350, 101, 550);
    edit(TransitionDetail, 350, 130, 170);
    label(L"自定义遮罩文件 · 可导入自己的 .glsl", 350, 181, 550);
    control(L"EDIT", L"", TransitionShaderPath, 350, 213, 550, 30, ES_READONLY | WS_BORDER | WS_TABSTOP,
            transitionEditor);
    control(L"BUTTON", L"导入 / 替换自定义 GLSL", TransitionImport, 350, 259, 550, 32, WS_TABSTOP,
            transitionEditor);
    label(L"普通使用只需要选择预设；高级自定义才编写着色器。", 350, 321, 550);
    label(L"先遮住旧地图，完全遮住时切换，再显露目标地图。", 350, 355, 550);
    label(L"过渡时暂停动作与事件；BGM继续，T可撤回地图过渡。", 350, 389, 550);
    label(L"预览只显示动画，不执行传送或重开。", 350, 423, 550);
    label(L"表单修改后点击「应用」，Ctrl+S保存到作品。", 350, 457, 550);
    control(L"BUTTON", L"在地图画布预览", TransitionPreview, 20, 539, 290, 36, WS_TABSTOP,
            transitionEditor);
    control(L"BUTTON", L"应用配置", TransitionApply, 350, 539, 550, 36, WS_TABSTOP, transitionEditor);
    transitionForm();
}
void App::transitionForm() {
    TransitionConfig c = transitionScope == 1 ? initial_->deathTransition : initial_->mapTransition;
    bool portal = transitionScope == 2, override = false;
    if (portal && transitionPortal >= 0 && transitionPortal < int(editor.level.portals.count)) {
        c = editor.level.portals[transitionPortal].transition;
        override = editor.level.portals[transitionPortal].transitionOverride;
    }
    SendMessageW(controls[TransitionScope], CB_SETCURSEL, transitionScope, 0);
    SendMessageW(controls[TransitionPreset], CB_SETCURSEL, int(c.kind), 0);
    SendMessageW(controls[TransitionEasing], CB_SETCURSEL, int(c.ease), 0);
    SendMessageW(controls[TransitionDirection], CB_SETCURSEL, c.direction, 0);
    SendMessageW(controls[TransitionCenter], BM_SETCHECK, c.playerCenter, 0);
    SendMessageW(controls[TransitionOverride], BM_SETCHECK, override, 0);
    ShowWindow(controls[TransitionOverride], portal ? SW_SHOW : SW_HIDE);
    setNumber(TransitionClose, c.closeSeconds);
    setNumber(TransitionHold, c.holdSeconds);
    setNumber(TransitionOpen, c.openSeconds);
    setNumber(TransitionDetail, c.detail);
    setNumber(TransitionRed, c.color[0]);
    setNumber(TransitionGreen, c.color[1]);
    setNumber(TransitionBlue, c.color[2]);
    SetWindowTextW(controls[TransitionShaderPath], wide(config.transitionShader).c_str());
}
TransitionConfig App::readTransitionForm() {
    TransitionConfig c;
    c.kind = TransitionKind(selection(TransitionPreset));
    c.ease = TransitionEase(selection(TransitionEasing));
    c.direction = std::uint8_t(selection(TransitionDirection));
    c.playerCenter = SendMessageW(controls[TransitionCenter], BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.closeSeconds = number(TransitionClose);
    c.holdSeconds = number(TransitionHold);
    c.openSeconds = number(TransitionOpen);
    c.detail = number(TransitionDetail);
    c.color = {number(TransitionRed), number(TransitionGreen), number(TransitionBlue)};
    validateTransition(c);
    if (c.kind == TransitionKind::Custom && config.transitionShader.empty())
        throw std::runtime_error("请先导入自定义转场 GLSL 函数");
    return c;
}
void App::transitionCommand(int id) {
    if (id == TransitionEditor) {
        showTransitionEditor();
        return;
    }
    if (id == TransitionScope) {
        transitionScope = selection(id);
        transitionForm();
        return;
    }
    if (id == TransitionImport) {
        auto path = choose(false, L"GLSL function\0*.glsl\0All files\0*.*\0\0",
                           L"导入切图遮罩 GLSL 函数", L"glsl");
        if (path.empty())
            return;
        if (std::filesystem::file_size(path) > 256 * 1024)
            throw std::runtime_error("转场着色器源码不能超过 256 KiB");
        std::ifstream input(path);
        std::string source((std::istreambuf_iterator<char>(input)), {});
        // Compile first; errors keep the working shader and project path intact.
        renderer.setTransitionSource(source);
        config.transitionShader = importResource(path, "shaders", AssetKind::Shader);
        SetWindowTextW(controls[TransitionShaderPath], wide(config.transitionShader).c_str());
        editor.dirty = true;
        message = "自定义转场遮罩已导入";
        return;
    }
    if (id == TransitionPreview) {
        transitionPreviewConfig = readTransitionForm();
        transitionPreview = true;
        transitionPreviewStart =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        return;
    }
    if (id == TransitionApply) {
        auto c = readTransitionForm();
        if (transitionScope == 2) {
            if (transitionPortal < 0 || transitionPortal >= int(editor.level.portals.count))
                throw std::runtime_error("请先选中传送区域再打开转场设置");
            auto r = editor.level.portals[transitionPortal].region;
            if (r.x != transitionPortalRegion.x || r.y != transitionPortalRegion.y ||
                r.w != transitionPortalRegion.w || r.h != transitionPortalRegion.h)
                throw std::runtime_error("传送区域已变化，请重新打开转场设置");
            editor.begin();
            auto& p = editor.level.portals[transitionPortal];
            p.transition = c;
            p.transitionOverride =
                SendMessageW(controls[TransitionOverride], BM_GETCHECK, 0, 0) == BST_CHECKED;
            editor.end();
        } else if (transitionScope == 1) {
            initial_->deathTransition = config.deathTransition = c;
        } else {
            initial_->mapTransition = config.mapTransition = c;
        }
        editor.dirty = true;
        refreshChrome();
        message = "转场设置已应用，记得保存工程";
    }
}
} // namespace echo
