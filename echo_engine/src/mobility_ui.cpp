#include "echo/app.hpp"
#include "echo/platform_features.hpp"
#include <stdexcept>
namespace echo {
namespace {
const wchar_t* featureNames[] = {L"\u68af\u5b50",
                                 L"\u6c34\u57df",
                                 L"\u4f20\u9001\u5e26",
                                 L"\u5f39\u7c27",
                                 L"\u843d\u4e0b\u5e73\u53f0",
                                 L"升降 / 路线平台"};
const wchar_t* mobilityLabels[] = {L"\u722c\u68af\u901f\u5ea6 px/s",
                                   L"\u6e38\u6cf3\u901f\u5ea6 px/s",
                                   L"\u6c34\u4e2d\u91cd\u529b\u500d\u7387 0..2",
                                   L"\u6e38\u6cf3\u8df3\u8dc3\u901f\u5ea6 px/s",
                                   L"\u51b2\u523a\u901f\u5ea6 px/s",
                                   L"\u51b2\u523a\u65f6\u957f s",
                                   L"\u51b2\u523a\u51b7\u5374 s",
                                   L"\u8d34\u5899\u6ed1\u843d\u901f\u5ea6 px/s",
                                   L"\u5899\u8df3\u6c34\u5e73\u901f\u5ea6 px/s",
                                   L"\u5899\u8df3\u5411\u4e0a\u901f\u5ea6 px/s",
                                   L"\u5899\u8df3\u65b9\u5411\u9501\u5b9a s",
                                   L"水中下落上限 px/s"};
auto mobilityValues(MobilityConfig& c) {
    return std::array<float*, 12>{&c.climbSpeed, &c.swimSpeed,    &c.waterGravity, &c.swimJump,
                                  &c.dashSpeed,  &c.dashDuration, &c.dashCooldown, &c.wallSlideSpeed,
                                  &c.wallJumpX,  &c.wallJumpY,    &c.wallLock,     &c.waterFallSpeed};
}
} // namespace
void App::featureControlState() {
    auto kind = PlatformKind(selection(FeatureKindControl));
    bool lift = kind == PlatformKind::Lift, falling = kind == PlatformKind::Falling;
    SetWindowTextW(controls[FeatureSpeedLabel], kind == PlatformKind::Spring ? L"弹射速度 px/s"
                                                : falling                    ? L"下落加速度 px/s²"
                                                          : L"移动速度 px/s（0暂停）");
    SetWindowTextW(controls[FeatureDelayLabel], lift ? L"端点停留 s" : L"落下前站立延迟 s");
    EnableWindow(controls[FeatureSpeed],
                 lift || falling || kind == PlatformKind::Spring || kind == PlatformKind::Conveyor);
    EnableWindow(controls[FeatureDelay], lift || falling);
    EnableWindow(controls[FeatureReset], falling);
    EnableWindow(controls[FeatureTravelX], lift);
    EnableWindow(controls[FeatureTravelY], lift);
    EnableWindow(controls[FeatureRepeat], lift);
}
void App::mobilityForm() {
    if (!mobilityEditor)
        return;
    loadingMobility = true;
    auto& c = initial_->mobility;
    SendMessageW(controls[MobilityDash], BM_SETCHECK, c.dashEnabled, 0);
    SendMessageW(controls[MobilityWall], BM_SETCHECK, c.wallJumpEnabled, 0);
    SendMessageW(controls[MobilityCrush], BM_SETCHECK, c.crushKills, 0);
    auto values = mobilityValues(c);
    for (int i = 0; i < 12; ++i)
        setNumber(MobilityFirst + i, *values[i]);
    SendMessageW(controls[MobilityDashKey], CB_SETCURSEL, c.dashKey - 8, 0);
    SendMessageW(controls[MobilityUpKey], CB_SETCURSEL, c.climbKey - 8, 0);
    SendMessageW(controls[FeatureList], CB_RESETCONTENT, 0, 0);
    for (const auto& f : editor.level.features) {
        auto label = std::wstring(featureNames[int(f.kind)]) + L" #" + std::to_wstring(f.id);
        SendMessageW(controls[FeatureList], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    int index = editor.selectedFeature;
    PlatformFeature value = platformPreset(editor.featureKind, {160, 400, 96, 32}, 1);
    if (index >= 0 && index < int(editor.level.features.count))
        value = editor.level.features[index];
    SendMessageW(controls[FeatureList], CB_SETCURSEL, index, 0);
    SendMessageW(controls[FeatureKindControl], CB_SETCURSEL, int(value.kind), 0);
    setNumber(FeatureX, value.base.x);
    setNumber(FeatureY, value.base.y);
    setNumber(FeatureW, value.base.w);
    setNumber(FeatureH, value.base.h);
    setNumber(FeatureSpeed, value.speed);
    setNumber(FeatureDelay, value.delay);
    setNumber(FeatureReset, value.resetSeconds);
    setNumber(FeatureTravelX, value.travel.x);
    setNumber(FeatureTravelY, value.travel.y);
    SendMessageW(controls[FeatureRepeat], BM_SETCHECK, value.repeat, 0);
    featureControlState();
    SendMessageW(controls[FeatureEnabled], BM_SETCHECK, value.enabled, 0);
    loadingMobility = false;
}
void App::mobilityCommand(int id, int notification) {
    if (id == FeatureSelectTool) {
        command(Edit);
        editor.tool = 24;
        editor.featurePlacementArmed = false;
        refreshChrome();
        return;
    }
    if (id == MobilityEditor) {
        command(Edit);
        if (mobilityEditor && IsWindow(mobilityEditor)) {
            mobilityForm();
            if (!verifyMode && !smoke)
                ShowWindow(mobilityEditor, SW_SHOW);
            SetForegroundWindow(mobilityEditor);
            return;
        }
        mobilityEditor = CreateWindowW(
            L"EchoEngineWindow", L"\u5e73\u53f0\u52a8\u4f5c\u4e0e\u673a\u5173\u9884\u8bbe",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE), 80, 70,
            1080, 735, window, nullptr, instance, this);
        auto label = [&](const wchar_t* text, int x, int y, int width = 195, int cid = 0) {
            control(L"STATIC", text, cid, x, y, width, 22, 0, mobilityEditor);
        };
        auto edit = [&](int cid, int x, int y, int width = 195) {
            control(L"EDIT", L"", cid, x, y, width, 28, WS_BORDER | WS_TABSTOP, mobilityEditor);
        };
        auto button = [&](int cid, const wchar_t* text, int x, int y, int width = 410) {
            control(L"BUTTON", text, cid, x, y, width, 29, WS_TABSTOP, mobilityEditor);
        };
        auto combo = [&](int cid, int x, int y, int width, const std::vector<std::wstring>& list) {
            auto h = control(L"COMBOBOX", L"", cid, x, y, width, 240,
                             CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                             mobilityEditor);
            for (const auto& name : list)
                SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            SendMessageW(h, CB_SETCURSEL, 0, 0);
        };
        label(L"\u73a9\u5bb6\u52a8\u4f5c \u00b7 "
              L"\u53c2\u6570\u3001\u8ba1\u65f6\u4e0e\u8f93\u5165\u8fb9\u6cbf\u53ef\u56de\u6eaf",
              20, 16, 410);
        button(MobilityDash, L"\u542f\u7528\u51b2\u523a\uff08\u9ed8\u8ba4\u5173\u95ed\uff09", 20, 48);
        button(
            MobilityWall,
            L"\u542f\u7528\u5899\u8df3 / \u8d34\u5899\u6ed1\u843d\uff08\u9ed8\u8ba4\u5173\u95ed\uff09",
            20, 84);
        button(MobilityCrush, L"\u88ab\u79fb\u52a8\u5e73\u53f0\u6324\u538b\u65f6\u6b7b\u4ea1", 20, 120);
        for (int i = 0; i < 12; ++i) {
            int x = 20 + i % 2 * 215, y = 169 + i / 2 * 62;
            label(mobilityLabels[i], x, y);
            edit(MobilityFirst + i, x, y + 25);
        }
        label(L"\u51b2\u523a\u952e\uff08\u9ed8\u8ba4 X\uff09", 20, 548);
        label(L"\u5411\u4e0a\u722c\u68af\u952e\uff08\u9ed8\u8ba4 W\uff09", 235, 548);
        std::vector<std::wstring> keys;
        for (int vk = 8; vk <= 254; ++vk)
            keys.push_back(keyName(vk));
        combo(MobilityDashKey, 20, 576, 195, keys);
        combo(MobilityUpKey, 235, 576, 195, keys);
        button(MobilityApply,
               L"\u5e94\u7528\u73a9\u5bb6\u52a8\u4f5c \u00b7 "
               L"\u91cd\u65b0\u5f00\u59cb\u8bd5\u73a9\u5386\u53f2",
               20, 629);
        label(L"\u5730\u56fe\u673a\u5173 \u00b7 "
              L"\u9009\u9884\u8bbe\u540e\u5728\u5730\u56fe\u62d6\u51fa\u533a\u57df",
              464, 16, 574);
        combo(FeatureList, 464, 48, 574, {});
        combo(FeatureKindControl, 464, 95, 574,
              {featureNames[0], featureNames[1], featureNames[2], featureNames[3], featureNames[4],
               featureNames[5]});
        const wchar_t* names[] = {L"\u4f4d\u7f6e X\uff08\u903b\u8f91 px\uff09",
                                  L"\u4f4d\u7f6e Y\uff08\u903b\u8f91 px\uff09",
                                  L"\u5bbd\u5ea6\uff08\u903b\u8f91 px\uff09",
                                  L"\u9ad8\u5ea6\uff08\u903b\u8f91 px\uff09",
                                  L"\u901f\u5ea6 / \u5f39\u529b / \u843d\u4e0b\u52a0\u901f\u5ea6",
                                  L"\u843d\u4e0b\u524d\u7ad9\u7acb\u5ef6\u8fdf s",
                                  L"\u843d\u4e0b\u540e\u91cd\u751f\u7b49\u5f85 s",
                                  L"路线终点偏移 X px",
                                  L"路线终点偏移 Y px"};
        int ids[] = {FeatureX,     FeatureY,     FeatureW,       FeatureH,      FeatureSpeed,
                     FeatureDelay, FeatureReset, FeatureTravelX, FeatureTravelY};
        for (int i = 0; i < 9; ++i) {
            int x = 464 + i % 2 * 290, y = 151 + i / 2 * 69;
            label(names[i], x, y, 284,
                  i == 4   ? FeatureSpeedLabel
                  : i == 5 ? FeatureDelayLabel
                  : i == 6 ? FeatureResetLabel
                           : 0);
            edit(ids[i], x, y + 25, 284);
        }
        button(FeatureRepeat, L"往返路线（关闭为单程）", 754, 452, 284);
        button(FeatureEnabled, L"\u673a\u5173\u542f\u7528", 464, 493, 574);
        label(L"路线 X=0、Y<0 为上升；两轴同时设置为斜向，顶部单向。", 464, 532, 574);
        label(L"使用作品爬梯 / 下穿键；移动、停留与挤压状态均可回溯。", 464, 553, 574);
        button(FeatureDraw, L"\u5728\u5730\u56fe\u7ed8\u5236\u6240\u9009\u9884\u8bbe", 464, 576, 276);
        button(FeatureDelete, L"\u5220\u9664\u9009\u4e2d\u673a\u5173", 756, 576, 282);
        button(FeatureApply,
               L"\u5e94\u7528\u9009\u4e2d\u673a\u5173\u5c5e\u6027 \u00b7 \u652f\u6301\u64a4\u9500 / "
               L"\u91cd\u505a",
               464, 629, 574);
        mobilityForm();
        return;
    }
    if (!mobilityEditor || loadingMobility)
        return;
    if (id == FeatureList && notification == CBN_SELCHANGE) {
        editor.selectedFeature = selection(FeatureList);
        mobilityForm();
        return;
    }
    if (id == FeatureKindControl && notification == CBN_SELCHANGE) {
        featureControlState();
        return;
    }
    if (id == FeatureDraw) {
        editor.featureKind = PlatformKind(selection(FeatureKindControl));
        editor.featurePlacementArmed = true;
        editor.tool = 24;
        ShowWindow(mobilityEditor, SW_HIDE);
        SetFocus(canvas);
        refreshChrome();
        return;
    }
    if (id == MobilityApply) {
        auto candidate = initial_->mobility;
        candidate.dashEnabled = SendMessageW(controls[MobilityDash], BM_GETCHECK, 0, 0) != 0;
        candidate.wallJumpEnabled = SendMessageW(controls[MobilityWall], BM_GETCHECK, 0, 0) != 0;
        candidate.crushKills = SendMessageW(controls[MobilityCrush], BM_GETCHECK, 0, 0) != 0;
        auto values = mobilityValues(candidate);
        for (int i = 0; i < 12; ++i)
            *values[i] = number(MobilityFirst + i);
        candidate.dashKey = selection(MobilityDashKey) + 8;
        candidate.climbKey = selection(MobilityUpKey) + 8;
        validateMobility(candidate);
        for (int key : config.keys.keys)
            if (key == candidate.climbKey || (candidate.dashEnabled && key == candidate.dashKey))
                throw std::runtime_error(
                    "Platform action keys must differ from existing project action keys");
        initial_->mobility = config.mobility = candidate;
        simulation->reset(*initial_, HistoryClearReason::MobilitySettings);
        editor.dirty = true;
        message = "Mobility saved to project settings; F5 starts a fresh timeline";
        refreshChrome();
        return;
    }
    int index = editor.selectedFeature;
    if (index < 0 || index >= int(editor.level.features.count))
        return;
    if (id == FeatureDelete) {
        editor.begin();
        editor.level.features.erase(index);
        editor.selectedFeature = -1;
        editor.end();
        editor.dirty = true;
        mobilityForm();
        refreshChrome();
        return;
    }
    if (id == FeatureApply) {
        auto next = editor.level.features[index];
        next.kind = PlatformKind(selection(FeatureKindControl));
        next.base =
            next.bounds = {number(FeatureX), number(FeatureY), number(FeatureW), number(FeatureH)};
        next.previous = {next.base.x, next.base.y};
        next.speed = number(FeatureSpeed);
        next.delay = number(FeatureDelay);
        next.resetSeconds = number(FeatureReset);
        next.travel = {number(FeatureTravelX), number(FeatureTravelY)};
        next.repeat = SendMessageW(controls[FeatureRepeat], BM_GETCHECK, 0, 0) != 0;
        next.progress = 0;
        next.direction = 1;
        next.enabled = SendMessageW(controls[FeatureEnabled], BM_GETCHECK, 0, 0) != 0;
        next.stage = 0;
        next.elapsed = next.velocity = next.cooldown = 0;
        // Validate the candidate in a heap-backed map, keeping Win32's default stack bounded.
        auto level = std::make_unique<LevelState>(editor.level);
        level->features[index] = next;
        validateFeatures(*level);
        editor.begin();
        editor.level.features[index] = next;
        editor.end();
        editor.dirty = true;
        mobilityForm();
        refreshChrome();
        return;
    }
}
} // namespace echo
