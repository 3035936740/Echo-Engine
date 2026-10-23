#include "echo/event_edit.hpp"
#include <algorithm>
#include <array>
#include <cwctype>
#include <stdexcept>
namespace echo {
namespace {
constexpr std::array names = {L"显示对话 / 头像",
                              L"等待",
                              L"控制全局开关",
                              L"控制独立开关",
                              L"设置变量",
                              L"变量增加",
                              L"条件：开关",
                              L"条件：变量",
                              L"否则",
                              L"条件结束",
                              L"切换地图",
                              L"播放音效",
                              L"BGM 渐变",
                              L"改变背景",
                              L"移动本事件",
                              L"暂时消除事件",
                              L"结束执行",
                              L"调用公共事件",
                              L"播放 / 重播 BGM",
                              L"停止 BGM",
                              L"清除弹幕分组",
                              L"控制弹幕发射器",
                              L"玩家控制权",
                              L"执行 Lua 脚本（高级）",
                              L"循环",
                              L"循环结束",
                              L"跳出当前循环",
                              L"条件：Lua（高级）",
                              L"显示选项 / 条件选项",
                              L"增减物品数量",
                              L"条件：持有物品",
                              L"设置任务状态",
                              L"增加任务进度",
                              L"条件：任务状态",
                              L"条件：任务进度"};
static_assert(names.size() == std::size_t(EventOp::IfQuestProgress) + 1);
constexpr std::array categories = {L"全部指令",   L"对话与选项", L"数据与任务",
                                   L"流程与条件", L"地图与事件", L"弹幕与战斗",
                                   L"音乐与音效", L"玩家控制",   L"高级脚本"};
constexpr std::array aliases = {L"text dialogue portrait talk",
                                L"wait delay seconds",
                                L"Switch",
                                L"SelfSwitch",
                                L"Variable",
                                L"AddVariable",
                                L"IfSwitch",
                                L"IfVariable",
                                L"Else",
                                L"EndIf",
                                L"transfer map teleport",
                                L"sound se audio",
                                L"bgm music fade 渐强 渐弱 淡入 淡出",
                                L"Background",
                                L"Move",
                                L"Erase",
                                L"Exit",
                                L"CallCommon",
                                L"bgm music audio",
                                L"bgm music audio",
                                L"bullet barrage group clear",
                                L"emitter barrage bullet",
                                L"control input player",
                                L"Script",
                                L"Loop",
                                L"EndLoop",
                                L"BreakLoop",
                                L"IfScript",
                                L"choice dialogue option",
                                L"item inventory amount",
                                L"if item inventory condition",
                                L"quest status task",
                                L"quest progress task",
                                L"if quest status condition",
                                L"if quest progress condition"};
static_assert(aliases.size() == names.size());
void bounded(std::span<const EventCommand> commands) {
    if (commands.size() > maxEventCommands)
        throw std::runtime_error("Command capacity is 32 per page");
}
void validate(std::span<const EventCommand> commands, bool common) {
    EventDefinition e;
    e.pages.emplace_back();
    e.pages[0].commands.assign(commands.begin(), commands.end());
    EventMap m;
    m.events.push_back(std::move(e));
    validateEvents(m, common);
}
EventCategory category(EventOp op) {
    if (op == EventOp::Text || op == EventOp::Choices)
        return EventCategory::Dialogue;
    if (op == EventOp::Script || op == EventOp::IfScript)
        return EventCategory::Advanced;
    if (op == EventOp::PlayerControl)
        return EventCategory::Control;
    if (op == EventOp::ClearBulletGroup || op == EventOp::EmitterControl)
        return EventCategory::Combat;
    if (op == EventOp::Sound || op == EventOp::FadeMusic || op == EventOp::PlayMusic ||
        op == EventOp::StopMusic)
        return EventCategory::Audio;
    if (op == EventOp::Transfer || op == EventOp::Background || op == EventOp::Move ||
        op == EventOp::Erase)
        return EventCategory::World;
    if (eventProgression(op) && !eventConditional(op))
        return EventCategory::Data;
    if (op == EventOp::Switch || op == EventOp::SelfSwitch || op == EventOp::Variable ||
        op == EventOp::AddVariable)
        return EventCategory::Data;
    return EventCategory::Flow;
}
} // namespace
std::wstring_view eventCommandName(EventOp op) {
    if (std::size_t(op) >= names.size())
        throw std::runtime_error("Unknown event command");
    return names[std::size_t(op)];
}
std::wstring_view eventCategoryName(EventCategory c) {
    if (std::size_t(c) >= categories.size())
        throw std::runtime_error("Unknown command category");
    return categories[std::size_t(c)];
}
bool eventCommandMatches(EventOp op, EventCategory c, std::wstring_view query, bool common) {
    eventCommandName(op);
    eventCategoryName(c);
    if (common && (op == EventOp::Move || op == EventOp::Erase))
        return false;
    if (c != EventCategory::All && c != category(op) &&
        !(c == EventCategory::Data && eventProgression(op)) &&
        !(c == EventCategory::Flow && eventConditional(op)))
        return false;
    if (query.size() > 64)
        throw std::runtime_error("Command search is limited to 64 characters");
    std::wstring hay(names[std::size_t(op)]);
    hay += L" ";
    hay += aliases[std::size_t(op)];
    std::transform(hay.begin(), hay.end(), hay.begin(),
                   [](wchar_t x) { return wchar_t(std::towlower(x)); });
    std::wstring needle(query);
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](wchar_t x) { return wchar_t(std::towlower(x)); });
    std::size_t start = 0;
    while ((start = needle.find_first_not_of(L" \t\r\n", start)) != std::wstring::npos) {
        auto end = needle.find_first_of(L" \t\r\n", start);
        if (hay.find(needle.substr(start, end - start)) == std::wstring::npos)
            return false;
        if (end == std::wstring::npos)
            break;
        start = end;
    }
    return true;
}
bool eventBlockHeading(EventOp op) {
    return eventConditional(op) || op == EventOp::Loop;
}
bool eventBlockTerminator(EventOp op) {
    return op == EventOp::Else || op == EventOp::EndIf || op == EventOp::EndLoop;
}
int eventBlockEnd(std::span<const EventCommand> commands, int first) {
    bounded(commands);
    if (first < 0 || first >= int(commands.size()) || !eventBlockHeading(commands[first].op))
        throw std::runtime_error("Select a condition or loop heading");
    int depth = 0;
    for (int i = first; i < int(commands.size()); ++i) {
        if (eventBlockHeading(commands[i].op))
            ++depth;
        if ((commands[i].op == EventOp::EndIf || commands[i].op == EventOp::EndLoop) && --depth == 0)
            return i + 1;
    }
    throw std::runtime_error("Event block needs End");
}
int eventBlockOwner(std::span<const EventCommand> commands, int index) {
    bounded(commands);
    if (index < 0 || index >= int(commands.size()))
        return -1;
    std::array<int, maxEventCommands> stack{};
    int depth = 0;
    for (int i = 0; i <= index; ++i) {
        auto op = commands[i].op;
        if (eventBlockHeading(op))
            stack[depth++] = i;
        if (i == index)
            return depth ? stack[depth - 1] : -1;
        if ((op == EventOp::EndIf || op == EventOp::EndLoop) && depth)
            --depth;
    }
    return -1;
}
std::vector<EventCommandRow> eventCommandRows(std::span<const EventCommand> commands,
                                              const std::bitset<maxEventCommands>& collapsed) {
    bounded(commands);
    std::vector<EventCommandRow> rows;
    rows.reserve(commands.size());
    int depth = 0;
    for (int i = 0; i < int(commands.size()); ++i) {
        auto op = commands[i].op;
        if (eventBlockTerminator(op))
            depth = std::max(0, depth - 1);
        int end = eventBlockHeading(op) && collapsed[i] ? eventBlockEnd(commands, i) : i + 1;
        rows.push_back({i, depth, end - i - 1});
        if (end != i + 1)
            i = end - 1;
        else if (eventBlockHeading(op) || op == EventOp::Else)
            ++depth;
    }
    return rows;
}
std::vector<int> eventCommandSelection(std::span<const EventCommand> commands,
                                       std::span<const int> selection, bool copying) {
    bounded(commands);
    std::bitset<maxEventCommands> chosen;
    for (int index : selection) {
        if (index < 0 || index >= int(commands.size()))
            throw std::runtime_error("Invalid command selection");
        int first = index;
        if (eventBlockTerminator(commands[index].op))
            first = eventBlockOwner(commands, index);
        if (copying && commands[index].op == EventOp::BreakLoop) {
            first = eventBlockOwner(commands, index);
            while (first >= 0 && commands[first].op != EventOp::Loop) {
                // Find the enclosing scope BEFORE this heading, not a preceding sibling.
                int parent = -1;
                for (int j = 0; j < first; ++j)
                    if (eventBlockHeading(commands[j].op) && eventBlockEnd(commands, j) > first)
                        parent = j;
                first = parent;
            }
        }
        if (first < 0)
            throw std::runtime_error("Unmatched event block");
        int end = eventBlockHeading(commands[first].op) ? eventBlockEnd(commands, first) : first + 1;
        for (int i = first; i < end; ++i)
            chosen[i] = true;
    }
    if (copying) {
        for (int i = 0; i < int(commands.size()); ++i) {
            if (!chosen[i] || commands[i].op != EventOp::BreakLoop)
                continue;
            int loop = -1;
            for (int j = 0; j < i; ++j)
                if (commands[j].op == EventOp::Loop && eventBlockEnd(commands, j) > i)
                    loop = j;
            if (loop < 0)
                throw std::runtime_error("BreakLoop needs its enclosing loop");
            for (int j = loop; j < eventBlockEnd(commands, loop); ++j)
                chosen[j] = true;
        }
    }
    std::vector<int> result;
    for (int i = 0; i < int(commands.size()); ++i)
        if (chosen[i])
            result.push_back(i);
    return result;
}
std::vector<EventCommand> copyEventCommands(std::span<const EventCommand> commands,
                                            std::span<const int> selection) {
    auto indices = eventCommandSelection(commands, selection, true);
    std::vector<EventCommand> result;
    result.reserve(indices.size());
    for (int i : indices)
        result.push_back(commands[i]);
    return result;
}
std::vector<EventCommand> eraseEventCommands(std::span<const EventCommand> commands,
                                             std::span<const int> selection, bool common) {
    auto indices = eventCommandSelection(commands, selection);
    std::vector<EventCommand> result;
    for (int i = 0; i < int(commands.size()); ++i)
        if (!std::binary_search(indices.begin(), indices.end(), i))
            result.push_back(commands[i]);
    validate(result, common);
    return result;
}
std::vector<EventCommand> pasteEventCommands(std::span<const EventCommand> commands,
                                             std::span<const EventCommand> clipboard, int after,
                                             bool common) {
    bounded(commands);
    bounded(clipboard);
    if (commands.size() + clipboard.size() > maxEventCommands)
        throw std::runtime_error("Paste exceeds 32 commands per page");
    if (after < -1 || after >= int(commands.size()))
        throw std::runtime_error("Invalid paste location");
    std::vector<EventCommand> result(commands.begin(), commands.end());
    result.insert(result.begin() + after + 1, clipboard.begin(), clipboard.end());
    validate(result, common);
    return result;
}
int moveEventCommands(std::vector<EventCommand>& commands, std::span<const int> selection, bool up,
                      bool common) {
    auto indices = eventCommandSelection(commands, selection);
    if (indices.empty())
        return -1;
    int first = indices.front(), end = indices.back() + 1;
    if (end - first != int(indices.size()))
        throw std::runtime_error("Move requires one contiguous command group");
    auto result = commands;
    int keep = first;
    if (up && first > 0) {
        int previous = first - 1;
        if (commands[previous].op == EventOp::Else || eventBlockHeading(commands[previous].op))
            return first;
        if (eventBlockTerminator(commands[previous].op))
            previous = eventBlockOwner(commands, previous);
        if (previous < 0)
            return first;
        std::rotate(result.begin() + previous, result.begin() + first, result.begin() + end);
        keep = previous;
    } else if (!up && end < int(commands.size())) {
        if (eventBlockTerminator(commands[end].op))
            return first;
        int next = eventBlockHeading(commands[end].op) ? eventBlockEnd(commands, end) : end + 1;
        std::rotate(result.begin() + first, result.begin() + end, result.begin() + next);
        keep = first + next - end;
    }
    validate(result, common);
    commands = std::move(result);
    return keep;
}
std::size_t eventDraftBytes(const EventDefinition& e) {
    std::size_t bytes = sizeof(e) + e.pages.capacity() * sizeof(EventPage);
    for (const auto& p : e.pages) {
        bytes += p.commands.capacity() * sizeof(EventCommand);
        for (const auto& c : p.commands)
            bytes += c.choices.capacity() * sizeof(ChoiceDefinition);
    }
    return bytes;
}
} // namespace echo
