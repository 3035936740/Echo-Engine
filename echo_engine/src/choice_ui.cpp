#include "echo/app.hpp"
#include <cmath>
#include <stdexcept>
namespace echo {
namespace {
std::string choiceText(HWND h) {
    int length = GetWindowTextLengthW(h);
    std::wstring value(length + 1, 0);
    GetWindowTextW(h, value.data(), length + 1);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), length, nullptr, 0, nullptr, nullptr);
    std::string result(bytes, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), length, result.data(), bytes, nullptr, nullptr);
    return result;
}
} // namespace
void App::showChoiceEditor() {
    if (!eventEditor || eventChoiceDraft.empty())
        return;
    if (choiceEditor && IsWindow(choiceEditor)) {
        if (!verifyMode && !smoke)
            ShowWindow(choiceEditor, SW_SHOW);
        SetForegroundWindow(choiceEditor);
        return;
    }
    choiceEditor =
        CreateWindowW(L"EchoEngineWindow",
                      L"\u5bf9\u8bdd\u9009\u9879 \u00b7 \u6761\u4ef6\u4e0e\u5206\u652f\u7ed3\u679c",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      180, 100, 740, 690, eventEditor, nullptr, instance, this);
    auto label = [&](const wchar_t* text, int x, int y, int width) {
        control(L"STATIC", text, 0, x, y, width, 25, 0, choiceEditor);
    };
    auto combo = [&](int id, int x, int y, int width, const std::vector<std::wstring>& items) {
        auto h =
            control(L"COMBOBOX", L"", id, x, y, width, 250,
                    CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, choiceEditor);
        for (const auto& item : items)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageW(h, CB_SETCURSEL, 0, 0);
    };
    label(L"\u9009\u9879 \u00b7 \u6700\u591a 6 \u4e2a", 20, 16, 215);
    control(L"LISTBOX", L"", ChoiceList, 20, 47, 215, 340,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, choiceEditor);
    control(L"BUTTON", L"+ \u65b0\u589e", ChoiceNew, 20, 403, 100, 29, WS_TABSTOP, choiceEditor);
    control(L"BUTTON", L"\u5220\u9664", ChoiceDelete, 135, 403, 100, 29, WS_TABSTOP, choiceEditor);
    label(L"\u9009\u9879\u6587\u5b57\uff08\u6700\u591a 127 UTF-8 \u5b57\u8282\uff09", 258, 16, 440);
    control(L"EDIT", L"", ChoiceLabel, 258, 47, 440, 28, WS_BORDER | WS_TABSTOP, choiceEditor);
    label(L"\u53ef\u7528\u6761\u4ef6", 258, 91, 440);
    combo(ChoiceConditionType, 258, 120, 440,
          {L"\u59cb\u7ec8\u53ef\u7528", L"\u5168\u5c40\u5f00\u5173", L"\u53d8\u91cf\u6bd4\u8f83",
           L"\u53ea\u8bfb Lua\uff08\u9ad8\u7ea7\uff09"});
    label(L"\u6761\u4ef6\u5bf9\u8c61", 258, 160, 440);
    combo(ChoiceTarget, 258, 188, 440, {});
    label(L"\u6bd4\u8f83 / \u5f00\u5173\u503c", 258, 227, 220);
    combo(ChoiceComparison, 258, 255, 220, {});
    control(L"EDIT", L"0", ChoiceThreshold, 496, 255, 202, 28, WS_BORDER | WS_TABSTOP, choiceEditor);
    control(L"BUTTON",
            L"\u6761\u4ef6\u4e0d\u6ee1\u8db3\u65f6\u9690\u85cf\uff08\u5426\u5219\u7070\u663e\uff09",
            ChoiceHide, 258, 302, 440, 29, WS_TABSTOP, choiceEditor);
    label(
        L"\u9ad8\u7ea7\u6761\u4ef6 \u00b7 return true / false\uff1b\u53ea\u8bfb\u5f15\u64ce\u72b6\u6001",
        258, 345, 440);
    control(L"EDIT", L"return true", ChoiceLua, 258, 374, 440, 166,
            WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
            choiceEditor);
    label(L"\u786e\u8ba4\u540e\u5199\u5165\u539f\u59cb\u5e8f\u53f7 "
          L"1..6\uff0c\u9690\u85cf\u4e0d\u4f1a\u6539\u53d8\u7f16\u53f7\u3002",
          20, 555, 680);
    label(L"\u4fdd\u5b58\u9009\u9879\u540e\uff0c\u5728\u4e8b\u4ef6\u7a97\u53e3\u63d2\u5165 / "
          L"\u66f4\u65b0\uff0c\u518d\u5e94\u7528\u4e8b\u4ef6\u3002",
          20, 580, 680);
    control(L"BUTTON", L"\u4fdd\u5b58\u5f53\u524d\u9009\u9879\u5230\u8349\u7a3f", ChoiceApply, 258, 611,
            440, 29, WS_TABSTOP, choiceEditor);
    choiceIndex = 0;
    choiceForm();
}
void App::choiceForm() {
    if (!choiceEditor || eventChoiceDraft.empty())
        return;
    loadingChoice = true;
    const auto& o = eventChoiceDraft.at(choiceIndex);
    SendMessageW(controls[ChoiceList], LB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < eventChoiceDraft.size(); ++i) {
        auto label = std::to_wstring(i + 1) + L" \u00b7 " + wide(eventChoiceDraft[i].label.c_str());
        SendMessageW(controls[ChoiceList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(controls[ChoiceList], LB_SETCURSEL, choiceIndex, 0);
    SetWindowTextW(controls[ChoiceLabel], wide(o.label.c_str()).c_str());
    SendMessageW(controls[ChoiceConditionType], CB_SETCURSEL, int(o.condition), 0);
    SendMessageW(controls[ChoiceTarget], CB_RESETCONTENT, 0, 0);
    int count = o.condition == ChoiceCondition::Switch     ? 128
                : o.condition == ChoiceCondition::Variable ? 64
                                                           : 0;
    for (int i = 0; i < count; ++i) {
        auto label = wide(dataLabel(database, count == 64, i));
        SendMessageW(controls[ChoiceTarget], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(controls[ChoiceTarget], CB_SETCURSEL, o.id, 0);
    SendMessageW(controls[ChoiceComparison], CB_RESETCONTENT, 0, 0);
    auto comparisons = o.condition == ChoiceCondition::Switch
                           ? std::vector<std::wstring>{L"\u5173\u95ed", L"\u5f00\u542f"}
                           : std::vector<std::wstring>{L"\u2265 \u5927\u4e8e\u7b49\u4e8e",
                                                       L"= \u7b49\u4e8e",
                                                       L"< \u5c0f\u4e8e",
                                                       L"> \u5927\u4e8e",
                                                       L"\u2264 \u5c0f\u4e8e\u7b49\u4e8e",
                                                       L"\u2260 \u4e0d\u7b49\u4e8e"};
    for (const auto& label : comparisons)
        SendMessageW(controls[ChoiceComparison], CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
    SendMessageW(controls[ChoiceComparison], CB_SETCURSEL,
                 o.condition == ChoiceCondition::Switch ? int(o.switchValue) : o.comparison, 0);
    setNumber(ChoiceThreshold, o.threshold);
    SendMessageW(controls[ChoiceHide], BM_SETCHECK, o.hideUnavailable, 0);
    SetWindowTextW(controls[ChoiceLua], wide(o.script.c_str()).c_str());
    EnableWindow(controls[ChoiceTarget], count != 0);
    EnableWindow(controls[ChoiceComparison], count != 0);
    EnableWindow(controls[ChoiceThreshold], o.condition == ChoiceCondition::Variable);
    EnableWindow(controls[ChoiceLua], o.condition == ChoiceCondition::Lua);
    loadingChoice = false;
}
void App::choiceRead() {
    if (!choiceEditor || eventChoiceDraft.empty() || loadingChoice)
        return;
    auto next = eventChoiceDraft.at(choiceIndex);
    auto label = choiceText(controls[ChoiceLabel]), source = choiceText(controls[ChoiceLua]);
    if (label.empty() || label.size() >= 128 || source.size() >= 1024)
        throw std::runtime_error("选项文字或脚本超出长度上限");
    next.label.set(label);
    next.script.set(source);
    next.condition = ChoiceCondition(selection(ChoiceConditionType));
    next.id = std::max(0, selection(ChoiceTarget));
    next.comparison = std::max(0, selection(ChoiceComparison));
    next.switchValue = next.comparison != 0;
    if (next.condition == ChoiceCondition::Switch)
        next.comparison = 0;
    next.threshold = number(ChoiceThreshold);
    next.hideUnavailable = SendMessageW(controls[ChoiceHide], BM_GETCHECK, 0, 0) != 0;
    EventCommand command;
    command.op = EventOp::Choices;
    command.choices = {next};
    EventPage page;
    page.commands = {command};
    EventDefinition definition;
    definition.pages = {page};
    validateEvents(EventMap{{definition}});
    eventChoiceDraft[choiceIndex] = next;
}
void App::choiceCommand(int id, int notification) {
    if (id == EventChoiceOpen) {
        showChoiceEditor();
        return;
    }
    if (!choiceEditor || loadingChoice)
        return;
    if (id == ChoiceConditionType && notification == CBN_SELCHANGE) {
        // Preserve the previous form before constructing the new type's defaults.
        int kind = selection(ChoiceConditionType);
        auto& o = eventChoiceDraft.at(choiceIndex);
        auto label = choiceText(controls[ChoiceLabel]), source = choiceText(controls[ChoiceLua]);
        if (label.empty() || label.size() >= 128 || source.size() >= 1024)
            throw std::runtime_error("选项文字超出长度上限");
        o.label.set(label);
        o.script.set(source);
        o.hideUnavailable = SendMessageW(controls[ChoiceHide], BM_GETCHECK, 0, 0) != 0;
        o.condition = ChoiceCondition(kind);
        o.id = o.comparison = 0;
        if (kind == int(ChoiceCondition::Lua) && source.empty())
            o.script.set("return true");
        choiceForm();
        return;
    }
    if (id == ChoiceList && notification == LBN_SELCHANGE) {
        int next = int(SendMessageW(controls[ChoiceList], LB_GETCURSEL, 0, 0));
        if (next < 0)
            return;
        choiceRead();
        choiceIndex = next;
        choiceForm();
        return;
    }
    if (id == ChoiceApply) {
        choiceRead();
        choiceForm();
        return;
    }
    if (id == ChoiceNew) {
        choiceRead();
        if (eventChoiceDraft.size() >= 6)
            throw std::runtime_error("最多 6 个选项");
        ChoiceDefinition o;
        o.label.set("New choice");
        eventChoiceDraft.push_back(o);
        choiceIndex = int(eventChoiceDraft.size()) - 1;
        choiceForm();
        return;
    }
    if (id == ChoiceDelete) {
        if (eventChoiceDraft.size() <= 1)
            throw std::runtime_error("至少保留一个选项");
        eventChoiceDraft.erase(eventChoiceDraft.begin() + choiceIndex);
        choiceIndex = std::min(choiceIndex, int(eventChoiceDraft.size()) - 1);
        choiceForm();
    }
}
} // namespace echo
