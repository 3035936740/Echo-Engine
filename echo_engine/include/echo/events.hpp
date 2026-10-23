#pragma once
#include "world.hpp"
#include <string>
#include <vector>
namespace echo {
// Resource programs are immutable during Play. ALL execution state is in WorldState.
inline constexpr int maxMapEvents = 32, maxCommonEvents = 16, maxEventPages = 4, maxEventCommands = 32;
bool eventUsesEmitter(const struct EventMap&, std::uint64_t);
enum class EventTrigger : unsigned char { Action, Touch, Autorun, Parallel, Enter, Leave, Contact };
enum class EventOp : unsigned char {
    Text,
    Wait,
    Switch,
    SelfSwitch,
    Variable,
    AddVariable,
    IfSwitch,
    IfVariable,
    Else,
    EndIf,
    Transfer,
    Sound,
    FadeMusic,
    Background,
    Move,
    Erase,
    Exit,
    CallCommon,
    PlayMusic,
    StopMusic,
    ClearBulletGroup,
    EmitterControl,
    PlayerControl,
    Script,
    Loop,
    EndLoop,
    BreakLoop,
    IfScript,
    Choices,
    ChangeItem,
    IfItem,
    QuestStatus,
    QuestProgress,
    IfQuestStatus,
    IfQuestProgress
};
constexpr bool eventConditional(EventOp op) {
    return op == EventOp::IfSwitch || op == EventOp::IfVariable || op == EventOp::IfScript ||
           op == EventOp::IfItem || op == EventOp::IfQuestStatus || op == EventOp::IfQuestProgress;
}
constexpr bool eventProgression(EventOp op) {
    return op >= EventOp::ChangeItem && op <= EventOp::IfQuestProgress;
}
constexpr bool eventQuest(EventOp op) {
    return op >= EventOp::QuestStatus && op <= EventOp::IfQuestProgress;
}
enum class ChoiceCondition : unsigned char { Always, Switch, Variable, Lua };
struct ChoiceDefinition {
    Text<128> label;
    ChoiceCondition condition{};
    int id{}, comparison{};
    float threshold{};
    bool switchValue = true, hideUnavailable{};
    Text<1024> script;
};
struct EventCommand {
    EventOp op{};
    int a{}, b{}, c{};
    std::uint64_t reference{};
    float value{}, x{}, y{};
    Text<1024> text;
    Text<64> speaker;
    Text<256> portrait;
    std::vector<ChoiceDefinition> choices; // Immutable authoring data, never cross-frame execution.
};
struct EventPage {
    EventTrigger trigger = EventTrigger::Action;
    int switchId = -1, variableId = -1, selfSwitch = -1;
    bool switchValue = true, selfValue = true, solid = false;
    int variableMinimum{}, graphic = 0;
    // Immutable definition; only selection and fault progress belong to WorldState.
    bool scriptConditionEnabled{};
    Text<1024> scriptCondition;
    std::vector<EventCommand> commands;
};
struct EventDefinition {
    int id = 1;
    Text<64> name;
    std::vector<EventPage> pages;
};
struct EventMap {
    std::vector<EventDefinition> events;
    std::size_t bytes() const;
};
const EventDefinition* findEvent(const EventMap&, int id);
void validateEvents(const EventMap&, bool common = false);
EventMap loadEvents(const std::string&, bool common = false);
std::string saveEvents(const EventMap&, bool common = false);
EventDefinition eventPreset(int id, int preset);
struct EventScriptContext {
    int map{}, event{}, page{}, command{};
    bool common{};
    int option = -1; // -1: ordinary/page condition; otherwise 0..5 choice availability.
};
// Definitions/compiled Lua are immutable; PC, faults and persistent data are WorldState.
struct EventScriptExecutor {
    virtual ~EventScriptExecutor() = default;
    virtual void beginEventStep(WorldState&) {}
    virtual bool eventConditionAvailable() const {
        return true;
    }
    virtual bool eventCondition(WorldState&, const EventScriptContext&, bool& result) {
        result = false;
        return false;
    }
    virtual bool eventScript(WorldState&, const EventScriptContext&) {
        return false;
    }
};
void stepEvents(WorldState&, const EventMap&, const EventMap&, bool interact, float dt,
                EventScriptExecutor* scripts = nullptr);
bool eventLocksPlayer(const WorldState&);
void cancelEventChildren(WorldState&, int owner);
void cancelEvents(WorldState&, bool includeCommon);
const DialogueDefinition& activeDialogue(const WorldState&);
} // namespace echo
