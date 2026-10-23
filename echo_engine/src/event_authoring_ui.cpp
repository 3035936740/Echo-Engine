#include "echo/app.hpp"
#include <algorithm>
#include <stdexcept>
namespace echo {
namespace {
constexpr std::size_t draftBudget = 8ull * 1024 * 1024;
std::size_t bytes(const std::vector<EventDefinition>& states) {
    std::size_t n = states.capacity() * sizeof(EventDefinition);
    for (const auto& state : states)
        n += eventDraftBytes(state) - sizeof(EventDefinition);
    return n;
}
void trim(std::vector<EventDefinition>& destination, std::vector<EventDefinition>& other,
          std::size_t extra) {
    while (destination.size() >= 16 || bytes(destination) + bytes(other) + extra > draftBudget) {
        if (!destination.empty())
            destination.erase(destination.begin());
        else if (!other.empty())
            other.erase(other.begin());
        else
            throw std::runtime_error("Event draft exceeds 8 MiB undo budget");
    }
}
std::string definition(const EventDefinition& event, bool common) {
    EventMap map;
    map.events.push_back(event);
    return saveEvents(map, common);
}
} // namespace
std::vector<int> App::eventSelectedCommands() const {
    std::vector<int> result;
    auto found = controls.find(EventCommandList);
    if (found == controls.end() || !IsWindow(found->second))
        return result;
    for (int row = 0; row < int(eventRows.size()); ++row)
        if (SendMessageW(found->second, LB_GETSEL, row, 0) > 0)
            result.push_back(eventRows[row].source);
    return result;
}
int App::eventSelectedSource() const {
    auto selected = eventSelectedCommands();
    if (selected.empty())
        return -1;
    int caret = int(SendMessageW(controls.at(EventCommandList), LB_GETCARETINDEX, 0, 0));
    if (caret >= 0 && caret < int(eventRows.size()) &&
        SendMessageW(controls.at(EventCommandList), LB_GETSEL, caret, 0) > 0)
        return eventRows[caret].source;
    return selected.front();
}
void App::eventSetSelection(const std::vector<int>& selected) {
    auto h = controls.at(EventCommandList);
    SendMessageW(h, LB_SETSEL, FALSE, -1);
    int caret = -1;
    for (int i = 0; i < int(eventRows.size()); ++i)
        if (std::find(selected.begin(), selected.end(), eventRows[i].source) != selected.end()) {
            SendMessageW(h, LB_SETSEL, TRUE, i);
            if (caret < 0)
                caret = i;
        }
    if (caret >= 0) {
        SendMessageW(h, LB_SETCARETINDEX, caret, FALSE);
        SendMessageW(h, LB_SETANCHORINDEX, caret, 0);
    }
    if (!loadingEventForm && eventId >= 0)
        eventCommand(EventCommandList, LBN_SELCHANGE);
    EnableWindow(controls[EventCopy], !selected.empty());
    EnableWindow(controls[EventCut], !selected.empty());
    EnableWindow(controls[EventRemove], !selected.empty());
    EnableWindow(controls[EventUpdate],
                 selected.size() == 1 && selection(EventCommandType) >= 0 &&
                     !eventBlockTerminator(eventDraft.pages[eventPage].commands[selected[0]].op));
}
void App::eventCatalog(int op, bool reveal) {
    bool previous = loadingEventForm;
    loadingEventForm = true;
    auto h = controls[EventCommandType];
    if (op < 0) {
        int row = selection(EventCommandType);
        if (row >= 0)
            op = int(SendMessageW(h, CB_GETITEMDATA, row, 0));
    }
    int n = GetWindowTextLengthW(controls[EventCatalogSearch]);
    std::wstring query(n + 1, 0);
    GetWindowTextW(controls[EventCatalogSearch], query.data(), n + 1);
    query.resize(n);
    SendMessageW(h, CB_RESETCONTENT, 0, 0);
    int selected = -1;
    auto category = EventCategory(std::max(0, selection(EventCatalogCategory)));
    for (int i = 0; i <= int(EventOp::IfQuestProgress); ++i) {
        if (!eventCommandMatches(EventOp(i), category, query, eventCommon))
            continue;
        int row = int(SendMessageW(h, CB_ADDSTRING, 0,
                                   reinterpret_cast<LPARAM>(eventCommandName(EventOp(i)).data())));
        SendMessageW(h, CB_SETITEMDATA, row, i);
        if (i == op)
            selected = row;
    }
    if (selected < 0 && reveal && op >= 0) {
        SendMessageW(controls[EventCatalogCategory], CB_SETCURSEL, 0, 0);
        SetWindowTextW(controls[EventCatalogSearch], L"");
        eventCatalog(op, false);
        loadingEventForm = previous;
        return;
    }
    if (selected < 0 && SendMessageW(h, CB_GETCOUNT, 0, 0) > 0)
        selected = 0;
    SendMessageW(h, CB_SETCURSEL, selected, 0);
    EnableWindow(controls[EventInsert],
                 selected >= 0 &&
                     !eventBlockTerminator(EventOp(SendMessageW(h, CB_GETITEMDATA, selected, 0))));
    EnableWindow(controls[EventUpdate],
                 selected >= 0 && eventSelectedCommands().size() == 1 &&
                     !eventBlockTerminator(EventOp(SendMessageW(h, CB_GETITEMDATA, selected, 0))));
    if (eventId >= 0 && selected >= 0)
        eventCommandForm();
    if (selected < 0)
        SetWindowTextW(controls[EventHelp], L"没有匹配的指令；清空关键词或切换分类。");
    loadingEventForm = previous;
}
void App::eventRefreshCommands(int keep) {
    if (eventId < 0 || eventPage >= int(eventDraft.pages.size()))
        return;
    auto& commands = eventDraft.pages[eventPage].commands;
    eventRows = eventCommandRows(commands, eventCollapsed);
    auto h = controls[EventCommandList];
    SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    SendMessageW(h, LB_RESETCONTENT, 0, 0);
    for (const auto& row : eventRows) {
        // Render source PC, not visible row index: folding must not change command identity.
        std::wstring text = std::to_wstring(row.source + 1) + L"  " + std::wstring(row.depth * 2, L' ');
        if (eventBlockHeading(commands[row.source].op))
            text += row.hidden ? L"[+] " : L"[-] ";
        text += eventCommandLabel(commands[row.source]);
        if (row.hidden)
            text += L" · " + std::to_wstring(row.hidden) + L"条已折叠";
        SendMessageW(h, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, nullptr, TRUE);
    EnableWindow(controls[EventPaste], !eventClipboard.empty());
    EnableWindow(controls[EventDraftUndo], !eventDraftUndo.empty());
    EnableWindow(controls[EventDraftRedo], !eventDraftRedo.empty());
    eventSetSelection(keep < 0 ? std::vector<int>{}
                               : std::vector<int>{std::min(keep, int(commands.size()) - 1)});
}
void App::eventDraftCommit(std::vector<EventCommand> commands, int keep) {
    auto next = eventDraft;
    next.pages[eventPage].commands = std::move(commands);
    EventMap candidate;
    candidate.events.push_back(next);
    validateEvents(candidate, eventCommon);
    scripts->validateEventScripts(candidate, eventCommon, editMap);
    if (definition(next, eventCommon) == definition(eventDraft, eventCommon))
        return;
    eventDraftUndo.reserve(16);
    eventDraftRedo.reserve(16);
    auto before = eventDraft;
    eventDraftRedo.clear();
    trim(eventDraftUndo, eventDraftRedo, eventDraftBytes(before));
    eventDraftUndo.push_back(std::move(before));
    eventDraft = std::move(next);
    eventCollapsed.reset();
    eventRefreshCommands(keep);
    message = "Event draft updated; Apply commits the authoring undo transaction";
}
void App::eventDraftHistory(bool undo) {
    auto& source = undo ? eventDraftUndo : eventDraftRedo;
    auto& destination = undo ? eventDraftRedo : eventDraftUndo;
    if (source.empty())
        return;
    auto next = source.back();
    EventMap candidate;
    candidate.events.push_back(next);
    validateEvents(candidate, eventCommon);
    scripts->validateEventScripts(candidate, eventCommon, editMap);
    auto current = eventDraft;
    source.pop_back();
    trim(destination, source, eventDraftBytes(current));
    destination.push_back(std::move(current));
    eventDraft = std::move(next);
    eventCollapsed.reset();
    eventPageForm(false);
}
bool App::eventShortcut(UINT key, bool control) {
    if (!eventEditor || GetFocus() != controls[EventCommandList])
        return false;
    int action = 0;
    if (control) {
        if (key == 'C')
            action = EventCopy;
        if (key == 'X')
            action = EventCut;
        if (key == 'V')
            action = EventPaste;
        if (key == 'Z')
            action = EventDraftUndo;
        if (key == 'Y')
            action = EventDraftRedo;
        if (key == VK_UP)
            action = EventUp;
        if (key == VK_DOWN)
            action = EventDown;
        if (key == 'A') {
            std::vector<int> selected;
            for (const auto& row : eventRows)
                selected.push_back(row.source);
            eventSetSelection(selected);
            return true;
        }
    } else {
        if (key == VK_DELETE)
            action = EventRemove;
        if (key == VK_RETURN)
            action = EventFold;
    }
    if (!action)
        return false;
    eventCommand(action);
    return true;
}
} // namespace echo
