#include "echo/app.hpp"
#include "echo/control.hpp"
#include <algorithm>
#include <commctrl.h>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace echo {
namespace {
std::string readUtf8(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring text(n + 1, 0);
    GetWindowTextW(h, text.data(), n + 1);
    text.resize(n);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), n, nullptr, 0, nullptr, nullptr);
    std::string value(bytes, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), n, value.data(), bytes, nullptr, nullptr);
    return value;
}
bool condition(EventOp op) {
    return eventConditional(op);
}
bool heading(EventOp op) {
    return condition(op) || op == EventOp::Loop;
}
bool terminator(EventOp op) {
    return op == EventOp::Else || op == EventOp::EndIf || op == EventOp::EndLoop;
}
std::wstring commandLine(const EventCommand& c, const Database& db) {
    std::wostringstream out;
    out << eventCommandName(c.op);
    switch (c.op) {
    case EventOp::Loop:
        if (c.a)
            out << L" · " << c.a << L" 次";
        else
            out << L" · 持续（需等待或跳出）";
        break;
    case EventOp::PlayerControl:
        out << L" · "
            << (c.a == 0   ? L"释放本事件限制"
                : c.a == 5 ? L"自定义许可"
                           : L"控制预设")
            << L" #" << c.a;
        break;
    case EventOp::Choices:
        out << L" · " << c.choices.size() << L" 项 → " << wide(dataLabel(db, true, c.a));
        break;
    case EventOp::IfScript:
    case EventOp::Script:
    case EventOp::Text:
        out << L"：「" << wide(c.text.c_str()).substr(0, 24) << L"」";
        break;
    case EventOp::Wait:
        out << L" " << c.value << L" 秒";
        break;
    case EventOp::Switch:
    case EventOp::IfSwitch:
        out << L" " << wide(dataLabel(db, false, c.a)) << (c.b ? L" = 开" : L" = 关");
        break;
    case EventOp::SelfSwitch:
        out << L" " << wchar_t(L'A' + c.a) << (c.b ? L" = 开" : L" = 关");
        break;
    case EventOp::Variable:
    case EventOp::AddVariable:
    case EventOp::IfVariable:
        out << L" " << wide(dataLabel(db, true, c.a)) << L" " << c.value;
        break;
    case EventOp::ChangeItem:
    case EventOp::IfItem:
    case EventOp::QuestStatus:
    case EventOp::QuestProgress:
    case EventOp::IfQuestStatus:
    case EventOp::IfQuestProgress:
        out << L" " << wide(progressionLabel(db, eventQuest(c.op), std::uint32_t(c.reference)));
        if (c.op == EventOp::QuestStatus || c.op == EventOp::IfQuestStatus) {
            const wchar_t* states[] = {L"未接受", L"进行中", L"已完成", L"失败"};
            out << L" = " << states[c.b];
        } else
            out << L" " << c.value;
        break;
    case EventOp::CallCommon:
        out << L" #" << c.a;
        break;
    case EventOp::Transfer:
        out << L" 地图 #" << c.a + 1;
        break;
    case EventOp::Sound:
        out << L" SE #" << c.a + 1;
        break;
    default:
        break;
    }
    return out.str();
}
} // namespace
std::wstring App::eventCommandLabel(const EventCommand& c) const {
    return commandLine(c, database);
}
void App::showEventEditor(bool fromCanvas) {
    command(Edit);
    if (eventEditor && IsWindow(eventEditor)) {
        if (fromCanvas && editor.selectedEvent >= 0 &&
            editor.selectedEvent < int(editor.level.events.count)) {
            int next = editor.level.events[editor.selectedEvent].id;
            if (eventCommon || next != eventId) {
                eventCommand(EventApply);
                eventCommon = false;
                SendMessageW(controls[EventScope], CB_SETCURSEL, 0, 0);
                eventLoad(next);
            }
        }
        SetForegroundWindow(eventEditor);
        return;
    }
    eventCommon = false;
    eventId = -1;
    eventPage = 0;
    eventEditor =
        CreateWindowW(L"EchoEngineWindow", L"事件编辑器 · 地图事件 / 全局公共事件",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      65, 65, 1200, 820, window, nullptr, instance, this);
    auto label = [&](const wchar_t* s, int x, int y, int width, int id = 0) {
        control(L"STATIC", s, id, x, y, width, 24, 0, eventEditor);
    };
    auto edit = [&](int id, int x, int y, int width, const wchar_t* text = L"") {
        control(L"EDIT", text, id, x, y, width, 27, WS_BORDER | WS_TABSTOP, eventEditor);
    };
    auto button = [&](int id, const wchar_t* text, int x, int y, int width) {
        control(L"BUTTON", text, id, x, y, width, 29, WS_TABSTOP, eventEditor);
    };
    auto combo = [&](int id, int x, int y, int width, const std::vector<std::wstring>& items) {
        auto h =
            control(L"COMBOBOX", L"", id, x, y, width, 250,
                    CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, eventEditor);
        for (const auto& item : items)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageW(h, CB_SETCURSEL, 0, 0);
        SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
    };
    label(L"事件库", 20, 14, 245);
    combo(EventScope, 20, 42, 242, {L"当前地图事件", L"全局 / 公共事件"});
    combo(EventList, 20, 82, 242, {});
    button(EventNew, L"新建", 20, 121, 114);
    button(EventDelete, L"删除", 146, 121, 116);
    label(L"事件名称", 20, 160, 242);
    edit(EventName, 20, 184, 242);
    label(L"预设 · 应用会替换当前事件指令", 20, 218, 242);
    combo(EventPreset, 20, 244, 242,
          {L"NPC 对话", L"一次性宝箱", L"并行计时", L"地图传送", L"进入计数", L"离开计数", L"接触计数",
           L"三次计时循环", L"选项分支 NPC"});
    button(EventPresetApply, L"应用预设", 20, 282, 242);
    label(L"事件页 · 后面的满足条件页优先", 20, 323, 250);
    combo(EventPageSelect, 20, 350, 242, {});
    button(EventPageNew, L"+ 页", 20, 388, 72);
    button(EventPageCopy, L"复制页", 100, 388, 76);
    button(EventPageDelete, L"删页", 184, 388, 78);
    label(L"触发方式", 20, 429, 242);
    combo(EventTriggerMode, 20, 455, 242, {});
    button(EventSolid, L"与玩家发生实体碰撞", 20, 493, 242);
    label(L"外观（图块编号，0 为默认标记）", 20, 532, 245);
    std::vector<std::wstring> graphics{L"0 · 默认事件标记"};
    for (int i = 1; i <= 16; ++i)
        graphics.push_back(L"图块 " + std::to_wstring(i));
    combo(EventGraphic, 20, 558, 242, graphics);
    button(EventPageScriptEnabled, L"高级 · 只读 Lua 页条件", 20, 597, 242);
    control(L"EDIT", L"return true", EventPageScriptSource, 20, 634, 242, 113,
            WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
            eventEditor);
    SendMessageW(controls[EventPageScriptSource], EM_SETLIMITTEXT, 1023, 0);
    label(L"页条件 · 没有条件的页始终有效", 284, 14, 420);
    auto numbered = [&](int count, const wchar_t* prefix, bool none) {
        std::vector<std::wstring> list;
        if (none)
            list.push_back(L"不限制");
        for (int i = 1; i <= count; ++i)
            list.push_back(std::wstring(prefix) + L" " + wide(dataLabel(database, count == 64, i - 1)));
        return list;
    };
    combo(EventSwitchCondition, 284, 42, 214, numbered(128, L"全局开关", true));
    button(EventSwitchConditionValue, L"需要开启", 514, 42, 190);
    combo(EventVariableCondition, 284, 82, 214, numbered(64, L"变量", true));
    edit(EventVariableMinimum, 514, 82, 190, L"0");
    combo(EventSelfCondition, 284, 122, 214,
          {L"不限制", L"独立开关 A", L"独立开关 B", L"独立开关 C", L"独立开关 D"});
    button(EventSelfConditionValue, L"需要开启", 514, 122, 190);
    label(L"变量条件：所选变量 ≥ 右侧数值", 284, 161, 420);
    label(L"指令链 · Ctrl/Shift多选，双击折叠", 284, 197, 420);
    button(EventCopy, L"复制", 284, 226, 76);
    button(EventCut, L"剪切", 370, 226, 76);
    button(EventPaste, L"粘贴", 456, 226, 76);
    button(EventFold, L"折叠/展开", 542, 226, 76);
    button(EventExpand, L"全展开", 628, 226, 76);
    button(EventDraftUndo, L"本页撤销", 284, 260, 104);
    button(EventDraftRedo, L"重做", 398, 260, 76);
    label(L"Ctrl+C/X/V · Z/Y · Delete", 484, 263, 220);
    control(L"LISTBOX", L"", EventCommandList, 284, 300, 420, 354,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL | WS_TABSTOP, eventEditor);
    button(EventRemove, L"删除指令", 284, 666, 118);
    button(EventUp, L"↑ 上移", 418, 666, 130);
    button(EventDown, L"↓ 下移", 564, 666, 140);
    label(L"指令库 · 分类与关键词搜索", 728, 14, 426);
    std::vector<std::wstring> categories;
    for (int i = 0; i <= int(EventCategory::Advanced); ++i)
        categories.emplace_back(eventCategoryName(EventCategory(i)));
    combo(EventCatalogCategory, 728, 42, 150, categories);
    edit(EventCatalogSearch, 892, 42, 262);
    SendMessageW(controls[EventCatalogSearch], EM_SETLIMITTEXT, 64, 0);
    SendMessageW(controls[EventCatalogSearch], EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"搜索：对话 / Lua / 渐弱…"));
    combo(EventCommandType, 728, 78, 426, {});
    label(L"目标", 728, 121, 426, EventTargetLabel);
    combo(EventTarget, 728, 148, 426, {});
    label(L"设置", 728, 188, 426, EventFlagLabel);
    combo(EventFlag, 728, 215, 426, {});
    label(L"数值", 728, 255, 426, EventValueLabel);
    edit(EventValue, 728, 282, 426, L"1");
    label(L"水平", 728, 322, 202, EventXLabel);
    edit(EventX, 728, 349, 202, L"0");
    label(L"垂直", 948, 322, 206, EventYLabel);
    edit(EventY, 948, 349, 206, L"0");
    label(L"说话者", 728, 389, 200, EventSpeakerLabel);
    edit(EventSpeaker, 728, 416, 200, L"NPC");
    label(L"头像（项目相对路径，可留空）", 946, 389, 208, EventPortraitLabel);
    edit(EventPortrait, 946, 416, 208);
    button(EventImportPortrait, L"导入头像", 946, 453, 208);
    button(EventChoiceOpen, L"编辑选项与条件…", 728, 453, 202);
    control(L"EDIT", L"", EventContent, 728, 492, 426, 105,
            WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
            eventEditor);
    const wchar_t* permissions[] = {L"左右移动",    L"跳跃",     L"下蹲 / 下穿",
                                    L"低速 / 专注", L"事件互动", L"R 快速回溯",
                                    L"T 轨迹回溯",  L"重新开始", L"对话确认"};
    control(L"STATIC", L"勾选允许的输入；多个事件的限制同时生效。", EventControlHint, 728, 194, 426, 44,
            0, eventEditor);
    for (int i = 0; i < 9; ++i)
        button(EventControlFirst + i, permissions[i], 728 + (i % 2) * 218, 260 + (i / 2) * 38, 208);
    label(L"", 728, 606, 426, EventHelp);
    button(EventInsert, L"+ 插入指令", 728, 646, 202);
    button(EventUpdate, L"更新选中指令", 946, 646, 208);
    button(EventApply, L"应用并保存到编辑中的事件 · Ctrl+S 保存作品", 284, 718, 870);
    eventRefreshList();
    int chosen = -1;
    if (editor.selectedEvent >= 0 && editor.selectedEvent < int(editor.level.events.count))
        chosen = editor.level.events[editor.selectedEvent].id;
    if (chosen < 0 && !editor.events.events.empty())
        chosen = editor.events.events.front().id;
    if (chosen >= 0)
        eventLoad(chosen);
    else
        eventCommand(EventNew);
}
void App::eventRefreshList() {
    auto& map = eventCommon ? editor.common : editor.events;
    SendMessageW(controls[EventList], CB_RESETCONTENT, 0, 0);
    for (const auto& e : map.events) {
        auto text = L"#" + std::to_wstring(e.id) + L"  " + wide(e.name.c_str());
        int i = int(
            SendMessageW(controls[EventList], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
        SendMessageW(controls[EventList], CB_SETITEMDATA, i, e.id);
        if (e.id == eventId)
            SendMessageW(controls[EventList], CB_SETCURSEL, i, 0);
    }
}
void App::eventLoad(int id) {
    auto* e = findEvent(eventCommon ? editor.common : editor.events, id);
    if (!e) {
        eventId = -1;
        return;
    }
    eventDraft = *e;
    eventId = id;
    eventPage = 0;
    SetWindowTextW(controls[EventName], wide(e->name.c_str()).c_str());
    eventPageForm();
    eventRefreshList();
}
void App::eventReadPage() {
    if (eventId < 0 || eventPage >= int(eventDraft.pages.size()))
        return;
    auto& p = eventDraft.pages[eventPage];
    int trigger = selection(EventTriggerMode);
    p.trigger = EventTrigger(eventCommon ? (trigger == 0 ? 0 : trigger + 1) : trigger);
    p.switchId = selection(EventSwitchCondition) - 1;
    p.switchValue = SendMessageW(controls[EventSwitchConditionValue], BM_GETCHECK, 0, 0) != 0;
    p.variableId = selection(EventVariableCondition) - 1;
    float threshold = number(EventVariableMinimum);
    if (std::abs(threshold) > 1000000)
        throw std::runtime_error("Variable threshold must be within +/-1000000");
    p.variableMinimum = int(threshold);
    p.selfSwitch = selection(EventSelfCondition) - 1;
    p.selfValue = SendMessageW(controls[EventSelfConditionValue], BM_GETCHECK, 0, 0) != 0;
    p.solid = SendMessageW(controls[EventSolid], BM_GETCHECK, 0, 0) != 0;
    p.graphic = selection(EventGraphic);
    p.scriptConditionEnabled = SendMessageW(controls[EventPageScriptEnabled], BM_GETCHECK, 0, 0) != 0;
    auto source = readUtf8(controls[EventPageScriptSource]);
    if (source.size() >= 1024)
        throw std::runtime_error("Page condition must fit 1023 UTF-8 bytes");
    p.scriptCondition.set(source);
}
void App::eventPageForm(bool resetView) {
    if (resetView) {
        eventCollapsed.reset();
        eventDraftUndo.clear();
        eventDraftRedo.clear();
    }
    loadingEventForm = true;
    auto& p = eventDraft.pages[eventPage];
    SendMessageW(controls[EventPageSelect], CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < int(eventDraft.pages.size()); ++i) {
        auto name = L"第 " + std::to_wstring(i + 1) + L" 页";
        SendMessageW(controls[EventPageSelect], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    int presetRow = selection(EventPreset);
    int preset =
        presetRow < 0 ? 0 : int(SendMessageW(controls[EventPreset], CB_GETITEMDATA, presetRow, 0));
    SendMessageW(controls[EventPreset], CB_RESETCONTENT, 0, 0);
    const wchar_t* presets[] = {L"NPC 对话", L"一次性宝箱", L"并行计时",     L"地图传送",    L"进入计数",
                                L"离开计数", L"接触计数",   L"三次计时循环", L"选项分支 NPC"};
    int selectedPreset = 0;
    for (int i = 0; i < 9; ++i) {
        if (eventCommon && i >= 4 && i <= 6)
            continue;
        int row = int(
            SendMessageW(controls[EventPreset], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(presets[i])));
        SendMessageW(controls[EventPreset], CB_SETITEMDATA, row, i);
        if (i == preset)
            selectedPreset = row;
    }
    SendMessageW(controls[EventPreset], CB_SETCURSEL, selectedPreset, 0);
    SendMessageW(controls[EventPageSelect], CB_SETCURSEL, eventPage, 0);
    SendMessageW(controls[EventTriggerMode], CB_RESETCONTENT, 0, 0);
    const std::vector<std::wstring> modes =
        eventCommon
            ? std::vector<std::wstring>{L"仅被调用", L"自动执行 · 锁玩家", L"并行执行 · 不锁玩家"}
            : std::vector<std::wstring>{
                  L"按 E 互动", L"玩家接触（进入）", L"自动执行 · 锁玩家", L"并行执行 · 不锁玩家",
                  L"进入区域",  L"离开区域",         L"持续接触"};
    for (const auto& name : modes)
        SendMessageW(controls[EventTriggerMode], CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name.c_str()));
    int chosen = eventCommon ? (int(p.trigger) == 0 ? 0 : int(p.trigger) - 1) : int(p.trigger);
    SendMessageW(controls[EventTriggerMode], CB_SETCURSEL, chosen, 0);
    SendMessageW(controls[EventSwitchCondition], CB_SETCURSEL, p.switchId + 1, 0);
    SendMessageW(controls[EventSwitchConditionValue], BM_SETCHECK, p.switchValue, 0);
    SendMessageW(controls[EventVariableCondition], CB_SETCURSEL, p.variableId + 1, 0);
    setNumber(EventVariableMinimum, float(p.variableMinimum));
    SendMessageW(controls[EventSelfCondition], CB_SETCURSEL, p.selfSwitch + 1, 0);
    SendMessageW(controls[EventSelfConditionValue], BM_SETCHECK, p.selfValue, 0);
    SendMessageW(controls[EventSolid], BM_SETCHECK, p.solid, 0);
    SendMessageW(controls[EventGraphic], CB_SETCURSEL, p.graphic, 0);
    SendMessageW(controls[EventPageScriptEnabled], BM_SETCHECK, p.scriptConditionEnabled, 0);
    SetWindowTextW(controls[EventPageScriptSource], wide(p.scriptCondition.c_str()).c_str());
    EnableWindow(controls[EventPageScriptSource], p.scriptConditionEnabled);
    loadingEventForm = false;
    eventCatalog();
    eventRefreshCommands(p.commands.empty() ? -1 : 0);
}
void App::eventCommandForm() {
    int row = selection(EventCommandType);
    if (row < 0)
        return;
    auto op = EventOp(SendMessageW(controls[EventCommandType], CB_GETITEMDATA, row, 0));
    EventCommand c;
    c.op = op;
    c.value = 1;
    c.b = 1;
    if (op == EventOp::IfScript)
        c.text.set("return player.hp > 0 and engine.get_variable(1) >= 3");
    int selected = eventSelectedSource();
    if (selected >= 0 && selected < int(eventDraft.pages[eventPage].commands.size())) {
        const auto& old = eventDraft.pages[eventPage].commands[selected];
        if (old.op == op)
            c = old;
    }
    auto show = [&](int id, bool visible) { ShowWindow(controls[id], visible ? SW_SHOW : SW_HIDE); };
    bool choices = op == EventOp::Choices;
    bool text = op == EventOp::Text || choices;
    show(EventChoiceOpen, choices);
    if (choiceEditor)
        ShowWindow(choiceEditor, SW_HIDE);
    eventChoiceDraft = c.choices;
    if (choices && eventChoiceDraft.empty()) {
        ChoiceDefinition a, b;
        a.label.set("Yes");
        b.label.set("No");
        eventChoiceDraft = {a, b};
    }
    bool script = op == EventOp::Script || op == EventOp::IfScript;
    for (int id : {EventSpeaker, EventPortrait, EventContent, EventImportPortrait, EventSpeakerLabel,
                   EventPortraitLabel})
        show(id, text);
    show(EventContent, text || script);
    MoveWindow(controls[EventContent], 728, script ? 148 : 492, 426, script ? 449 : 105, TRUE);
    MoveWindow(controls[EventHelp], 728, 606, 426, 34, TRUE);
    std::vector<std::wstring> targets, flags;
    auto ids = [&](int count, const wchar_t* prefix) {
        for (int i = 1; i <= count; ++i)
            targets.push_back(std::wstring(prefix) + L" #" + std::to_wstring(i));
    };
    const wchar_t *targetLabel = L"目标", *valueLabel = L"数值", *xLabel = L"X", *yLabel = L"Y",
                  *flagLabel = L"设置";
    bool value = false, x = false, y = false;
    switch (op) {
    case EventOp::ChangeItem:
    case EventOp::IfItem:
    case EventOp::QuestStatus:
    case EventOp::QuestProgress:
    case EventOp::IfQuestStatus:
    case EventOp::IfQuestProgress:
        if (eventQuest(op)) {
            for (const auto& q : database.quests)
                targets.push_back(wide(progressionLabel(database, true, q.id)));
        } else
            for (const auto& i : database.items)
                targets.push_back(wide(progressionLabel(database, false, i.id)));
        targetLabel = eventQuest(op) ? L"选择数据库任务（稳定 ID）" : L"选择数据库物品（稳定 ID）";
        if (op == EventOp::IfQuestStatus)
            flags = {L"未接受", L"进行中", L"已完成", L"失败"};
        else if (op == EventOp::QuestStatus)
            flags = {L"未接受 / 重置进度", L"进行中 / 重新接受", L"已完成（进度置为目标）", L"失败"};
        else {
            value = true;
            valueLabel = op == EventOp::ChangeItem || op == EventOp::QuestProgress
                             ? L"整数增加量（负数减少，±1000000）"
                             : L"整数数量 / 进度";
        }
        if (op == EventOp::IfItem || op == EventOp::IfQuestProgress)
            flags = {L"≥ 大于等于", L"= 等于", L"< 小于", L"> 大于", L"≤ 小于等于", L"≠ 不等于"};
        if (op == EventOp::ChangeItem || op == EventOp::QuestProgress) {
            flags = {L"不保存操作结果"};
            for (int i = 0; i < 64; ++i)
                flags.push_back(wide(dataLabel(database, true, i)));
            flagLabel = L"操作结果保存到变量（可选）";
        }
        break;
    case EventOp::Choices:
        for (int i = 0; i < 64; ++i)
            targets.push_back(wide(dataLabel(database, true, i)));
        targetLabel = L"选择结果保存到变量（原始选项序号 1..6）";
        break;
    case EventOp::PlayerControl:
        targets = {L"释放本事件的控制限制",    L"锁定左右移动",  L"锁定动作与互动", L"禁止 R / T 回溯",
                   L"剧情控制（保留 T 撤回）", L"自定义允许输入"};
        targetLabel = L"控制预设 · 仅作用于当前事件任务";
        break;
    case EventOp::Switch:
    case EventOp::IfSwitch:
        for (int i = 0; i < 128; ++i)
            targets.push_back(wide(dataLabel(database, false, i)));
        targetLabel = L"选择全局开关";
        flags = {L"关闭", L"开启"};
        break;
    case EventOp::SelfSwitch:
        targets = {L"A", L"B", L"C", L"D"};
        targetLabel = L"选择本事件独立开关";
        flags = {L"关闭", L"开启"};
        break;
    case EventOp::Variable:
    case EventOp::AddVariable:
    case EventOp::IfVariable:
        for (int i = 0; i < 64; ++i)
            targets.push_back(wide(dataLabel(database, true, i)));
        targetLabel = L"选择全局变量";
        value = true;
        valueLabel = op == EventOp::AddVariable ? L"增加量（负数为减少）" : L"数值";
        if (op == EventOp::IfVariable)
            flags = {L"≥ 大于等于", L"= 等于", L"< 小于", L"> 大于", L"≤ 小于等于", L"≠ 不等于"};
        break;
    case EventOp::ClearBulletGroup:
        targets.push_back(L"默认组 #0");
        for (const auto& p : editor.barrage.patterns) {
            auto label = L"组 #" + std::to_wstring(p.group);
            if (std::find(targets.begin(), targets.end(), label) == targets.end())
                targets.push_back(label);
        }
        targetLabel = L"仅清除所选组";
        break;
    case EventOp::EmitterControl:
        for (const auto& e : editor.barrage.emitters)
            targets.push_back(wide(e.name.c_str()));
        targetLabel = L"选择发射器";
        flags = {L"关闭（保留游标）", L"开启（继续游标）", L"开启并重新开始"};
        break;
    case EventOp::PlayMusic:
        flags = {L"继续当前位置", L"从头播放"};
        break;
    case EventOp::Loop:
        value = true;
        c.value = float(c.a);
        valueLabel = L"重复次数（整数 1..1000000；0 = 持续）";
        break;
    case EventOp::Wait:
        value = true;
        valueLabel = L"等待秒数";
        break;
    case EventOp::Transfer:
        for (const auto& name : config.maps)
            targets.push_back(wide(name));
        targetLabel = L"目标地图";
        flags = {L"使用下方指定落点", L"使用目标地图出生点"};
        x = y = true;
        xLabel = L"落点 X（逻辑坐标）";
        yLabel = L"落点 Y（逻辑坐标）";
        break;
    case EventOp::Sound:
        ids(32, L"SE");
        targetLabel = L"音效槽";
        value = true;
        valueLabel = L"音量（0..1）";
        break;
    case EventOp::FadeMusic:
        value = x = true;
        valueLabel = L"渐变秒数";
        xLabel = L"目标音量（0..1）";
        break;
    case EventOp::Background:
        targets = {L"黎明", L"白昼", L"黄昏", L"夜晚", L"流星", L"洞窟", L"极光"};
        break;
    case EventOp::Move:
        flags = {L"穿透（兼容旧工程）", L"碰撞阻挡 / 沿墙滑动"};
        flagLabel = L"移动碰撞方式";
        value = x = y = true;
        valueLabel = L"移动用时（秒）";
        xLabel = L"水平位移";
        yLabel = L"垂直位移";
        break;
    case EventOp::CallCommon:
        ids(16, L"公共事件");
        targetLabel = L"被调用的公共事件";
        break;
    default:
        break;
    }
    SendMessageW(controls[EventTarget], CB_RESETCONTENT, 0, 0);
    for (const auto& name : targets)
        SendMessageW(controls[EventTarget], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendMessageW(controls[EventTarget], CB_SETCURSEL,
                 op == EventOp::CallCommon ? std::max(0, c.a - 1) : c.a, 0);
    if (op == EventOp::EmitterControl || op == EventOp::ClearBulletGroup) {
        int selectedTarget = 0;
        for (int i = 0; i < int(targets.size()); ++i) {
            std::uint64_t key{};
            if (op == EventOp::EmitterControl)
                key = editor.barrage.emitters[i].id;
            else if (i)
                key = std::stoull(targets[i].substr(targets[i].find(L'#') + 1));
            SendMessageW(controls[EventTarget], CB_SETITEMDATA, i, LPARAM(key));
            if (key == (op == EventOp::EmitterControl ? c.reference : std::uint64_t(c.a)))
                selectedTarget = i;
        }
        SendMessageW(controls[EventTarget], CB_SETCURSEL, selectedTarget, 0);
    }
    if (eventProgression(op)) {
        int chosen = -1, index = 0;
        auto add = [&](std::uint32_t id) {
            SendMessageW(controls[EventTarget], CB_SETITEMDATA, index, LPARAM(id));
            if (id == c.reference || (!c.reference && index == 0))
                chosen = index;
            ++index;
        };
        if (eventQuest(op)) {
            for (const auto& q : database.quests)
                add(q.id);
        } else {
            for (const auto& i : database.items)
                add(i.id);
        }
        SendMessageW(controls[EventTarget], CB_SETCURSEL, chosen, 0);
    }
    SendMessageW(controls[EventFlag], CB_RESETCONTENT, 0, 0);
    for (const auto& name : flags)
        SendMessageW(controls[EventFlag], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendMessageW(controls[EventFlag], CB_SETCURSEL,
                 op == EventOp::ChangeItem || op == EventOp::QuestProgress ? c.c : c.b, 0);
    for (auto pair :
         {std::pair{EventTargetLabel, !targets.empty()}, std::pair{EventTarget, !targets.empty()},
          std::pair{EventFlagLabel, !flags.empty()}, std::pair{EventFlag, !flags.empty()},
          std::pair{EventValueLabel, value}, std::pair{EventValue, value}, std::pair{EventXLabel, x},
          std::pair{EventX, x}, std::pair{EventYLabel, y}, std::pair{EventY, y}})
        show(pair.first, pair.second);
    SetWindowTextW(controls[EventTargetLabel], targetLabel);
    SetWindowTextW(controls[EventValueLabel], valueLabel);
    SetWindowTextW(controls[EventXLabel], xLabel);
    SetWindowTextW(controls[EventYLabel], yLabel);
    SetWindowTextW(controls[EventFlagLabel], flagLabel);
    bool control = op == EventOp::PlayerControl;
    show(EventControlHint, control);
    auto mask = controlProfile(c.a, std::uint16_t(c.b));
    for (int i = 0; i < 9; ++i) {
        show(EventControlFirst + i, control);
        SendMessageW(controls[EventControlFirst + i], BM_SETCHECK, (mask & (1 << i)) != 0, 0);
        EnableWindow(controls[EventControlFirst + i], control && c.a == 5);
    }
    setNumber(EventValue, c.value);
    setNumber(EventX, eventProgression(op) ? float(c.c) : c.x);
    setNumber(EventY, c.y);
    SetWindowTextW(controls[EventSpeaker], wide(c.speaker.c_str()).c_str());
    SetWindowTextW(controls[EventPortrait], wide(c.portrait.c_str()).c_str());
    SetWindowTextW(controls[EventContent], wide(c.text.c_str()).c_str());
    SetWindowTextW(controls[EventHelp],
                   eventProgression(op) ? L"结果：0成功 /1缺失 /2上限 /3不足 /4未进行。"
                   : op == EventOp::Loop ? L"最多8层；等待可跨帧；跳出只结束最近一层；预算满自动让出。"
                   : op == EventOp::IfScript ? L"只读Lua；必须 return true/false；≤1023 UTF-8字节。"
                   : script ? L"Lua ≤1023 UTF-8字节；跨帧用 engine.state；等待用指令。"
                   : control ? L"结束、取消、换页、消除自动释放；不暂停物理或事件时钟。"
                   : choices ? L"左右键选可用项，确认键提交；条件为灰显或隐藏；T可撤回。"
                   : text ? L"正文每行一页；配置的确认键继续，T 可撤回。"
                          : L"插入到选中项之后；更新会替换选中项。");
}
EventCommand App::eventReadCommand() {
    EventCommand c;
    c.op = EventOp(
        SendMessageW(controls[EventCommandType], CB_GETITEMDATA, selection(EventCommandType), 0));
    c.a = std::max(0, selection(EventTarget));
    if (c.op == EventOp::CallCommon)
        ++c.a;
    c.b = std::max(0, selection(EventFlag));
    if (c.op == EventOp::EmitterControl || c.op == EventOp::ClearBulletGroup) {
        auto row = selection(EventTarget);
        if (row < 0)
            throw std::runtime_error("No emitter / group available");
        auto key = std::uint64_t(SendMessageW(controls[EventTarget], CB_GETITEMDATA, row, 0));
        if (c.op == EventOp::EmitterControl)
            c.reference = key;
        else
            c.a = int(key);
    }
    if (c.op == EventOp::PlayerControl) {
        c.b = 0;
        for (int i = 0; i < 9; ++i)
            if (SendMessageW(controls[EventControlFirst + i], BM_GETCHECK, 0, 0))
                c.b |= 1 << i;
    }
    c.value = number(EventValue);
    if (c.op == EventOp::Loop) {
        if (!std::isfinite(c.value) || c.value < 0 || c.value > 1000000 ||
            std::floor(c.value) != c.value)
            throw std::runtime_error("Loop count must be an integer 0..1000000");
        c.a = int(c.value);
    }
    c.x = number(EventX);
    c.y = number(EventY);
    if (eventProgression(c.op)) {
        int row = selection(EventTarget);
        if (row < 0)
            throw std::runtime_error("Choose an item/quest from the database first");
        c.reference = std::uint64_t(SendMessageW(controls[EventTarget], CB_GETITEMDATA, row, 0));
        c.c = 0;
        if (c.op == EventOp::ChangeItem || c.op == EventOp::QuestProgress) {
            c.c = c.b;
            c.b = 0;
        }
        c.a = 0;
        c.x = 0;
        c.y = 0;
    }
    auto text = readUtf8(controls[EventContent]), speaker = readUtf8(controls[EventSpeaker]),
         portrait = readUtf8(controls[EventPortrait]);
    if (text.size() >= 1024 || speaker.size() >= 64 || portrait.size() >= 256)
        throw std::runtime_error("Event text / speaker / portrait is too long");
    c.text.set(text);
    c.speaker.set(speaker);
    c.portrait.set(portrait);
    if (c.op == EventOp::Choices) {
        if (choiceEditor && IsWindowVisible(choiceEditor))
            choiceRead();
        c.choices = eventChoiceDraft;
    }
    return c;
}
void App::eventCommand(int id, int notification) {
    if (id == EventEditor) {
        showEventEditor();
        return;
    }
    if (loadingEventForm || !eventEditor)
        return;
    if (id == EventPageScriptEnabled) {
        bool enabled = SendMessageW(controls[EventPageScriptEnabled], BM_GETCHECK, 0, 0) != 0;
        if (enabled && readUtf8(controls[EventPageScriptSource]).empty())
            SetWindowTextW(controls[EventPageScriptSource], L"return true");
        EnableWindow(controls[EventPageScriptSource], enabled);
        return;
    }
    if (id == EventScope) {
        if (eventId >= 0)
            eventCommand(EventApply);
        eventCommon = selection(EventScope) == 1;
        eventId = -1;
        eventRefreshList();
        auto& map = eventCommon ? editor.common : editor.events;
        if (map.events.empty())
            eventCommand(EventNew);
        else
            eventLoad(map.events.front().id);
        return;
    }
    if (id == EventList) {
        int index = selection(EventList);
        if (index < 0)
            return;
        int chosen = int(SendMessageW(controls[EventList], CB_GETITEMDATA, index, 0));
        if (eventId >= 0)
            eventCommand(EventApply);
        eventLoad(chosen);
        return;
    }
    if (id == EventNew) {
        if (eventId >= 0)
            eventCommand(EventApply);
        auto& map = eventCommon ? editor.common : editor.events;
        int limit = eventCommon ? 16 : 32;
        if (map.events.size() >= std::size_t(limit))
            throw std::runtime_error("Event capacity reached");
        int newId = 1;
        while (findEvent(map, newId))
            ++newId;
        editor.begin();
        map.events.push_back(eventPreset(newId, eventCommon ? 2 : 0));
        if (!eventCommon) {
            EventPlacement placed;
            placed.id = newId;
            placed.region = {editor.level.spawn.x + 96, editor.level.spawn.y, 32, 48};
            editor.level.events.push(placed);
            editor.selectedEvent = int(editor.level.events.count) - 1;
        }
        editor.end();
        editor.dirty = true;
        eventLoad(newId);
        return;
    }
    if (id == EventCatalogCategory || id == EventCatalogSearch) {
        eventCatalog();
        return;
    }
    if (eventId < 0)
        return;
    if (id == EventCopy || id == EventCut) {
        auto selected = eventSelectedCommands();
        if (selected.empty())
            return;
        auto commands = eventDraft.pages[eventPage].commands;
        auto normalized = eventCommandSelection(commands, selected, true);
        auto copied = copyEventCommands(commands, selected);
        if (id == EventCut)
            eventDraftCommit(eraseEventCommands(commands, normalized, eventCommon), normalized.front());
        eventClipboard = std::move(copied);
        EnableWindow(controls[EventPaste], !eventClipboard.empty());
        message = "Copied " + std::to_string(eventClipboard.size()) + " commands (same project)";
        return;
    }
    if (id == EventPaste) {
        if (eventClipboard.empty())
            return;
        auto& commands = eventDraft.pages[eventPage].commands;
        auto selected = eventSelectedCommands();
        int after = selected.empty() ? int(commands.size()) - 1 : selected.back();
        if (after >= 0 && eventCollapsed[after] && heading(commands[after].op))
            after = eventBlockEnd(commands, after) - 1;
        auto next = pasteEventCommands(commands, eventClipboard, after, eventCommon);
        eventDraftCommit(std::move(next), after + 1);
        std::vector<int> pasted;
        for (int i = 0; i < int(eventClipboard.size()); ++i)
            pasted.push_back(after + 1 + i);
        eventSetSelection(pasted);
        return;
    }
    if (id == EventFold || id == EventExpand || (id == EventCommandList && notification == LBN_DBLCLK)) {
        int source = eventSelectedSource();
        if (id == EventExpand)
            eventCollapsed.reset();
        else {
            int owner = eventBlockOwner(eventDraft.pages[eventPage].commands, source);
            if (owner < 0)
                return;
            eventCollapsed.flip(owner);
            source = owner;
        }
        eventRefreshCommands(source);
        return;
    }
    if (id == EventDraftUndo || id == EventDraftRedo) {
        eventDraftHistory(id == EventDraftUndo);
        return;
    }
    if (id == EventDelete) {
        editor.begin();
        auto& map = eventCommon ? editor.common : editor.events;
        std::erase_if(map.events, [&](const auto& e) { return e.id == eventId; });
        if (!eventCommon)
            for (std::size_t i = 0; i < editor.level.events.count; ++i)
                if (editor.level.events[i].id == eventId) {
                    editor.level.events.erase(i);
                    break;
                }
        editor.end();
        editor.dirty = true;
        eventId = -1;
        eventRefreshList();
        if (!map.events.empty())
            eventLoad(map.events.front().id);
        else {
            SendMessageW(controls[EventCommandList], LB_RESETCONTENT, 0, 0);
            SetWindowTextW(controls[EventName], L"");
        }
        return;
    }
    if (id == EventPageSelect) {
        int chosen = selection(EventPageSelect);
        eventReadPage();
        eventPage = chosen;
        eventPageForm();
        return;
    }
    if (id == EventPageNew || id == EventPageCopy || id == EventPageDelete) {
        eventReadPage();
        if (id == EventPageDelete) {
            if (eventDraft.pages.size() == 1)
                throw std::runtime_error("Keep at least one event page");
            eventDraft.pages.erase(eventDraft.pages.begin() + eventPage);
            eventPage = std::min(eventPage, int(eventDraft.pages.size()) - 1);
        } else {
            if (eventDraft.pages.size() >= 4)
                throw std::runtime_error("Event page capacity is 4");
            EventPage p = id == EventPageCopy ? eventDraft.pages[eventPage] : EventPage{};
            eventDraft.pages.push_back(std::move(p));
            eventPage = int(eventDraft.pages.size()) - 1;
        }
        eventPageForm();
        return;
    }
    if (id == EventPresetApply) {
        eventDraft = eventPreset(eventId, int(SendMessageW(controls[EventPreset], CB_GETITEMDATA,
                                                           selection(EventPreset), 0)));
        eventPage = 0;
        SetWindowTextW(controls[EventName], wide(eventDraft.name.c_str()).c_str());
        eventPageForm();
        return;
    }
    if (id == EventCommandType) {
        eventCommandForm();
        return;
    }
    if (id == EventTarget &&
        SendMessageW(controls[EventCommandType], CB_GETITEMDATA, selection(EventCommandType), 0) ==
            int(EventOp::PlayerControl)) {
        int profile = selection(EventTarget);
        for (int i = 0; i < 9; ++i) {
            if (profile != 5)
                SendMessageW(controls[EventControlFirst + i], BM_SETCHECK,
                             (controlProfile(profile) & (1 << i)) != 0, 0);
            EnableWindow(controls[EventControlFirst + i], profile == 5);
        }
        return;
    }
    if (id == EventCommandList && notification == LBN_SELCHANGE) {
        int chosen = eventSelectedSource();
        EnableWindow(controls[EventCopy], chosen >= 0);
        EnableWindow(controls[EventCut], chosen >= 0);
        EnableWindow(controls[EventRemove], chosen >= 0);
        EnableWindow(controls[EventUpdate], eventSelectedCommands().size() == 1);
        if (chosen < 0)
            return;
        eventCatalog(int(eventDraft.pages[eventPage].commands[chosen].op), true);
        eventCommandForm();
        return;
    }
    if (id == EventImportPortrait) {
        auto path = choose(false, L"Portrait\0*.png;*.bmp\0\0", L"选择头像", L"png");
        if (path.empty())
            return;
        auto ref = importResource(path, "assets", AssetKind::Image);
        SetWindowTextW(controls[EventPortrait], wide(ref).c_str());
        return;
    }
    if (id == EventInsert || id == EventUpdate || id == EventRemove || id == EventUp ||
        id == EventDown) {
        auto commands = eventDraft.pages[eventPage].commands;
        int selected = eventSelectedSource();
        int keep = selected;
        int movedCount = 0;
        eventReadPage();
        if (id == EventInsert) {
            auto c = eventReadCommand();
            if (terminator(c.op))
                throw std::runtime_error("Insert a block heading; its end is created automatically");
            int added = condition(c.op) ? 3 : c.op == EventOp::Loop ? 2 : 1;
            if (commands.size() + added > 32)
                throw std::runtime_error("Command capacity is 32 per page");
            keep = selected < 0 ? int(commands.size())
                   : eventCollapsed[selected] && heading(commands[selected].op)
                       ? eventBlockEnd(commands, selected)
                       : selected + 1;
            commands.insert(commands.begin() + keep, c);
            if (condition(c.op)) {
                EventCommand otherwise, end;
                otherwise.op = EventOp::Else;
                end.op = EventOp::EndIf;
                commands.insert(commands.begin() + keep + 1, otherwise);
                commands.insert(commands.begin() + keep + 2, end);
            } else if (c.op == EventOp::Loop) {
                EventCommand end;
                end.op = EventOp::EndLoop;
                commands.insert(commands.begin() + keep + 1, end);
            }
        } else if (selected < 0 || selected >= int(commands.size()))
            return;
        else if (id == EventUpdate) {
            if (eventSelectedCommands().size() != 1)
                throw std::runtime_error("Select one command to edit parameters");
            auto next = eventReadCommand();
            if (condition(next.op) != condition(commands[selected].op) ||
                (next.op == EventOp::Loop) != (commands[selected].op == EventOp::Loop) ||
                terminator(next.op) || terminator(commands[selected].op))
                throw std::runtime_error("Use Insert/Delete to change a block type");
            commands[selected] = next;
        } else if (id == EventRemove) {
            auto selectedCommands = eventSelectedCommands();
            auto normalized = eventCommandSelection(commands, selectedCommands);
            commands = eraseEventCommands(commands, selectedCommands, eventCommon);
            keep = normalized.empty() ? -1 : std::min(normalized.front(), int(commands.size()) - 1);
        } else if (id == EventUp || id == EventDown) {
            movedCount = int(eventCommandSelection(commands, eventSelectedCommands()).size());
            keep = moveEventCommands(commands, eventSelectedCommands(), id == EventUp, eventCommon);
        }
        eventDraftCommit(std::move(commands), keep);
        if (movedCount > 1) {
            std::vector<int> group;
            for (int i = 0; i < movedCount; ++i)
                group.push_back(keep + i);
            eventSetSelection(group);
        }
        return;
    }
    if (id == EventApply) {
        eventReadPage();
        auto name = readUtf8(controls[EventName]);
        if (name.size() >= 64)
            throw std::runtime_error("Event name must fit 63 UTF-8 bytes");
        eventDraft.name.set(name);
        EventMap validation;
        validation.events.push_back(eventDraft);
        validateEvents(validation, eventCommon);
        scripts->validateEventScripts(validation, eventCommon, editMap);
        auto& map = eventCommon ? editor.common : editor.events;
        if (const auto* previous = findEvent(map, eventId)) {
            EventMap old;
            old.events.push_back(*previous);
            if (saveEvents(old, eventCommon) == saveEvents(validation, eventCommon))
                return;
        }
        editor.begin();
        for (auto& e : map.events)
            if (e.id == eventId)
                e = eventDraft;
        if (!eventCommon)
            for (auto& placed : editor.level.events)
                if (placed.id == eventId) {
                    placed.graphic = eventDraft.pages[0].graphic;
                    placed.solid = eventDraft.pages[0].solid;
                }
        editor.end();
        editor.dirty = true;
        eventRefreshList();
        refreshChrome();
        message = "Event commands applied";
        return;
    }
}
} // namespace echo
