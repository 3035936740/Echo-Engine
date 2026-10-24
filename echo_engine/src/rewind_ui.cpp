#include "echo/app.hpp"
#include <cmath>
#include <stdexcept>
namespace echo {
void App::rewindRulesCommand(int id) {
    if (id == RewindRulesEditor) {
        command(Edit);
        if (rewindRulesEditor && IsWindow(rewindRulesEditor)) {
            ShowWindow(rewindRulesEditor, SW_SHOW);
            SetForegroundWindow(rewindRulesEditor);
            return;
        }
        rewindRulesEditor = CreateWindowW(L"EchoEngineWindow", L"\u5feb\u901f\u56de\u6eaf\u89c4\u5219",
                                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                              ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                                          190, 150, 610, 285, window, nullptr, instance, this);
        control(L"BUTTON",
                L"\u63a5\u8fd1\u6709\u4f24\u5bb3\u7684\u5f39\u5e55\u65f6\u505c\u6b62\u5feb\u901f\u56de"
                L"\u6eaf",
                RewindNearEnabled, 20, 22, 550, 28, WS_TABSTOP, rewindRulesEditor);
        SendMessageW(controls[RewindNearEnabled], BM_SETCHECK, initial_->rewind.fast_stop_near_bullets,
                     0);
        control(L"STATIC",
                L"\u5f39\u5e55\u5224\u5b9a\u6269\u5927\u91cf \u00b7 "
                L"\u903b\u8f91\u50cf\u7d20\uff080\u201364\uff0c\u9ed8\u8ba4 2\uff09",
                0, 20, 72, 550, 24, 0, rewindRulesEditor);
        control(L"EDIT", L"", RewindMargin, 20, 102, 550, 29, WS_BORDER | WS_TABSTOP, rewindRulesEditor);
        setNumber(RewindMargin, initial_->rewind.fast_bullet_margin);
        control(L"STATIC",
                L"\u91c7\u7528\u89d2\u8272\u53d7\u51fb\u533a\u57df\u8fde\u7eed\u68c0\u6d4b\uff1b\u505c"
                L"\u6b62\u4fdd\u7559\u5f39\u5e55\u5e76\u622a\u65ad\u65e7\u672a\u6765\u3002",
                0, 20, 149, 550, 24, 0, rewindRulesEditor);
        control(L"BUTTON", L"\u5e94\u7528\u5feb\u901f\u56de\u6eaf\u89c4\u5219", RewindRulesApply, 20,
                194, 550, 32, WS_TABSTOP, rewindRulesEditor);
    } else if (id == RewindRulesApply) {
        auto candidate = simulation->world().rewind;
        candidate.fast_stop_near_bullets =
            SendMessageW(controls[RewindNearEnabled], BM_GETCHECK, 0, 0) == BST_CHECKED;
        candidate.fast_bullet_margin = number(RewindMargin);
        if (!std::isfinite(candidate.fast_bullet_margin) || candidate.fast_bullet_margin < 0 ||
            candidate.fast_bullet_margin > 64)
            throw std::runtime_error("边距需在 0–64 逻辑像素之间");
        simulation->configureRewind(candidate);
        initial_->rewind = simulation->world().rewind;
        editor.dirty = true;
        message = "快速回溯规则已应用，保存工程后保留";
        refreshChrome();
    }
}
} // namespace echo
