#pragma once
#include "world.hpp"
#include <string>
#include <vector>
namespace echo {
// Immutable project definitions. Runtime values remain in WorldState::events and WorldState::progression.
struct NamedValue {
    Text<64> name;
    Text<256> note;
    int initial{};
};
struct ItemDefinition {
    std::uint32_t id{};
    Text<64> name;
    Text<256> note;
    int limit = 99, initial{};
};
struct QuestDefinition {
    std::uint32_t id{};
    Text<64> name;
    Text<256> note;
    int target = 1;
    QuestStatus initial{};
    bool autoComplete = true;
};
struct Database {
    std::array<NamedValue, 128> switches;
    std::array<NamedValue, 64> variables;
    std::vector<ItemDefinition> items;
    std::vector<QuestDefinition> quests;
    std::uint32_t nextItemId = 1, nextQuestId = 1;
};
void validateDatabase(const Database&);
std::uint32_t allocateDataId(Database&, bool quest);
std::string progressionLabel(const Database&, bool quest, std::uint32_t id);
const ItemState* findItem(const WorldState&, std::uint32_t id);
const QuestState* findQuest(const WorldState&, std::uint32_t id);
ProgressResult changeItem(WorldState&, std::uint32_t id, int delta);
ProgressResult setQuestStatus(WorldState&, std::uint32_t id, QuestStatus);
ProgressResult addQuestProgress(WorldState&, std::uint32_t id, int delta);
bool progressionReferenceValid(const Database&, bool quest, std::uint64_t id);
Database loadDatabase(const std::string&);
std::string saveDatabase(const Database&);
std::string dataLabel(const Database&, bool variable, int index);
void applyDatabaseDefaults(WorldState&, const Database&);
} // namespace echo
