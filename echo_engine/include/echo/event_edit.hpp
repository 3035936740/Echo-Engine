#pragma once
#include "events.hpp"
#include <bitset>
#include <span>
#include <string_view>
namespace echo {
// Authoring only: selection, folds, clipboard and draft undo never enter gameplay.
enum class EventCategory { All, Dialogue, Data, Flow, World, Combat, Audio, Control, Advanced };
std::wstring_view eventCommandName(EventOp);
std::wstring_view eventCategoryName(EventCategory);
bool eventCommandMatches(EventOp, EventCategory, std::wstring_view, bool common);
bool eventBlockHeading(EventOp);
bool eventBlockTerminator(EventOp);
int eventBlockEnd(std::span<const EventCommand>, int first);
int eventBlockOwner(std::span<const EventCommand>, int index);
struct EventCommandRow {
    int source{}, depth{}, hidden{};
};
std::vector<EventCommandRow> eventCommandRows(std::span<const EventCommand>,
                                              const std::bitset<maxEventCommands>&);
std::vector<int> eventCommandSelection(std::span<const EventCommand>, std::span<const int>,
                                       bool copying = false);
std::vector<EventCommand> copyEventCommands(std::span<const EventCommand>, std::span<const int>);
std::vector<EventCommand> eraseEventCommands(std::span<const EventCommand>, std::span<const int>,
                                             bool common);
std::vector<EventCommand> pasteEventCommands(std::span<const EventCommand>,
                                             std::span<const EventCommand>, int after, bool common);
int moveEventCommands(std::vector<EventCommand>&, std::span<const int>, bool up, bool common);
std::size_t eventDraftBytes(const EventDefinition&);
} // namespace echo
