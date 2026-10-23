#include "echo/database.hpp"
#include <iomanip>
#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace echo {
void validateDatabase(const Database& db) {
    if (db.items.size() > 64 || db.quests.size() > 64 || db.nextItemId < 1 || db.nextItemId > 1000001 ||
        db.nextQuestId < 1 || db.nextQuestId > 1000001)
        throw std::runtime_error("Database capacity / ID exhausted");
    for (const auto& sw : db.switches)
        if (sw.initial != 0 && sw.initial != 1)
            throw std::runtime_error("Switch default must be 0/1");
    auto validate = [&](const auto& entries, std::uint32_t next) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            if (!e.id || e.id >= next || e.id > 1000000 || e.name.empty())
                throw std::runtime_error("Database definition needs a stable ID and name");
            for (std::size_t j = 0; j < i; ++j)
                if (entries[j].id == e.id)
                    throw std::runtime_error("Duplicate database definition ID");
        }
    };
    validate(db.items, db.nextItemId);
    validate(db.quests, db.nextQuestId);
    for (const auto& e : db.items)
        if (e.limit < 1 || e.limit > 1000000 || e.initial < 0 || e.initial > e.limit)
            throw std::runtime_error("Item limit / initial amount outside range");
    for (const auto& e : db.quests)
        if (e.target < 1 || e.target > 1000000 || int(e.initial) > 3)
            throw std::runtime_error("Quest target / status outside range");
}
std::uint32_t allocateDataId(Database& db, bool quest) {
    auto& id = quest ? db.nextQuestId : db.nextItemId;
    if (id > 1000000)
        throw std::runtime_error("Database ID space exhausted");
    return id++;
}
bool progressionReferenceValid(const Database& db, bool quest, std::uint64_t id) {
    if (quest) {
        for (const auto& q : db.quests)
            if (q.id == id)
                return true;
    } else {
        for (const auto& i : db.items)
            if (i.id == id)
                return true;
    }
    return false;
}
std::string progressionLabel(const Database& db, bool quest, std::uint32_t id) {
    std::string result = "#" + std::to_string(id);
    if (quest) {
        for (const auto& q : db.quests)
            if (q.id == id)
                return result + "  " + q.name.c_str();
    } else {
        for (const auto& i : db.items)
            if (i.id == id)
                return result + "  " + i.name.c_str();
    }
    return result + " [missing]";
}
const ItemState* findItem(const WorldState& w, std::uint32_t id) {
    for (const auto& e : w.progression.items)
        if (e.id == id)
            return &e;
    return nullptr;
}
const QuestState* findQuest(const WorldState& w, std::uint32_t id) {
    for (const auto& e : w.progression.quests)
        if (e.id == id)
            return &e;
    return nullptr;
}
static ProgressResult result(WorldState& w, ProgressResult r) {
    w.progression.lastResult = r;
    if (r != ProgressResult::Success)
        ++w.progression.rejected;
    return r;
}
ProgressResult changeItem(WorldState& w, std::uint32_t id, int delta) {
    for (auto& e : w.progression.items)
        if (e.id == id) {
            auto next = std::int64_t(e.count) + delta;
            if (next < 0)
                return result(w, ProgressResult::Insufficient);
            if (next > e.limit)
                return result(w, ProgressResult::Full);
            e.count = int(next);
            return result(w, ProgressResult::Success);
        }
    return result(w, ProgressResult::Missing);
}
ProgressResult setQuestStatus(WorldState& w, std::uint32_t id, QuestStatus status) {
    if (int(status) > 3)
        return result(w, ProgressResult::InactiveQuest);
    for (auto& e : w.progression.quests)
        if (e.id == id) {
            if (status == QuestStatus::Inactive || (status == QuestStatus::Active && e.status != status))
                e.progress = 0;
            if (status == QuestStatus::Completed)
                e.progress = e.target;
            e.status = status;
            return result(w, ProgressResult::Success);
        }
    return result(w, ProgressResult::Missing);
}
ProgressResult addQuestProgress(WorldState& w, std::uint32_t id, int delta) {
    for (auto& e : w.progression.quests)
        if (e.id == id) {
            if (e.status != QuestStatus::Active)
                return result(w, ProgressResult::InactiveQuest);
            e.progress = int(
                std::clamp(std::int64_t(e.progress) + delta, std::int64_t(0), std::int64_t(e.target)));
            if (e.autoComplete && e.progress == e.target)
                e.status = QuestStatus::Completed;
            return result(w, ProgressResult::Success);
        }
    return result(w, ProgressResult::Missing);
}
Database loadDatabase(const std::string& text) {
    if (text.size() > 256 * 1024)
        throw std::runtime_error("Database exceeds 256 KiB");
    Database db;
    std::istringstream s(text);
    std::string magic;
    s >> magic;
    if (magic != "ECHO_DATABASE_1" && magic != "ECHO_DATABASE_2")
        throw std::runtime_error("Unknown database version");
    for (int category = 0; category < 2; ++category) {
        int count = -1;
        s >> count;
        int limit = category ? 64 : 128;
        if (count < 0 || count > limit)
            throw std::runtime_error("Invalid database entry count");
        std::array<bool, 128> used{};
        for (int i = 0; i < count; ++i) {
            int id = -1, initial = 0;
            std::string name, note;
            s >> id >> initial >> std::quoted(name) >> std::quoted(note);
            if (!s || id < 1 || id > limit || used[id - 1] || name.size() >= 64 || note.size() >= 256 ||
                (!category && initial != 0 && initial != 1) || name.find('\0') != std::string::npos ||
                note.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid named switch/variable definition");
            used[id - 1] = true;
            auto& value = category ? db.variables[id - 1] : db.switches[id - 1];
            value.name.set(name);
            value.note.set(note);
            value.initial = initial;
        }
    }
    if (magic == "ECHO_DATABASE_2") {
        std::string marker;
        int count = -1;
        s >> marker >> db.nextItemId >> count;
        if (marker != "ITEMS" || count < 0 || count > 64)
            throw std::runtime_error("Invalid item section");
        for (int i = 0; i < count; ++i) {
            ItemDefinition item;
            std::string name, note;
            s >> item.id >> item.limit >> item.initial >> std::quoted(name) >> std::quoted(note);
            if (!s || name.size() >= 64 || note.size() >= 256 || name.find('\0') != std::string::npos ||
                note.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid item row");
            item.name.set(name);
            item.note.set(note);
            db.items.push_back(item);
        }
        count = -1;
        s >> marker >> db.nextQuestId >> count;
        if (marker != "QUESTS" || count < 0 || count > 64)
            throw std::runtime_error("Invalid quest section");
        for (int i = 0; i < count; ++i) {
            QuestDefinition quest;
            std::string name, note;
            int status = -1, automatic = -1;
            s >> quest.id >> quest.target >> status >> automatic >> std::quoted(name) >>
                std::quoted(note);
            if (!s || status < 0 || status > 3 || automatic < 0 || automatic > 1 || name.size() >= 64 ||
                note.size() >= 256 || name.find('\0') != std::string::npos ||
                note.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid quest row");
            quest.initial = QuestStatus(status);
            quest.autoComplete = automatic;
            quest.name.set(name);
            quest.note.set(note);
            db.quests.push_back(quest);
        }
    }
    validateDatabase(db);
    s >> std::ws;
    if (!s.eof())
        throw std::runtime_error("Unexpected database data");
    return db;
}
std::string saveDatabase(const Database& db) {
    std::ostringstream s;
    validateDatabase(db);
    s << "ECHO_DATABASE_2\n";
    auto write = [&](const auto& values, bool variable) {
        int count = 0;
        for (const auto& v : values)
            if (!v.name.empty() || !v.note.empty() || v.initial)
                ++count;
        s << count << '\n';
        for (int i = 0; i < int(values.size()); ++i) {
            const auto& v = values[i];
            if (!variable && v.initial != 0 && v.initial != 1)
                throw std::runtime_error("Switch default must be 0/1");
            if (!v.name.empty() || !v.note.empty() || v.initial)
                s << i + 1 << ' ' << v.initial << ' ' << std::quoted(v.name.c_str()) << ' '
                  << std::quoted(v.note.c_str()) << '\n';
        }
    };
    write(db.switches, false);
    write(db.variables, true);
    s << "ITEMS " << db.nextItemId << ' ' << db.items.size() << '\n';
    for (const auto& item : db.items)
        s << item.id << ' ' << item.limit << ' ' << item.initial << ' ' << std::quoted(item.name.c_str())
          << ' ' << std::quoted(item.note.c_str()) << '\n';
    s << "QUESTS " << db.nextQuestId << ' ' << db.quests.size() << '\n';
    for (const auto& quest : db.quests)
        s << quest.id << ' ' << quest.target << ' ' << int(quest.initial) << ' ' << quest.autoComplete
          << ' ' << std::quoted(quest.name.c_str()) << ' ' << std::quoted(quest.note.c_str()) << '\n';
    auto serialized = s.str();
    if (serialized.size() > 256 * 1024)
        throw std::runtime_error("Database exceeds 256 KiB");
    return serialized;
}
std::string dataLabel(const Database& db, bool variable, int index) {
    if (index < 0 || index >= (variable ? 64 : 128))
        throw std::runtime_error("Invalid data slot");
    const auto& entry = variable ? db.variables[index] : db.switches[index];
    std::string text = "#" + std::to_string(index + 1);
    if (!entry.name.empty())
        text += "  " + std::string(entry.name.c_str());
    return text;
}
void applyDatabaseDefaults(WorldState& w, const Database& db) {
    validateDatabase(db);
    w.progression = {};
    for (const auto& item : db.items)
        w.progression.items.push({item.id, item.initial, item.limit});
    for (const auto& quest : db.quests)
        w.progression.quests.push({quest.id, quest.initial,
                                   quest.initial == QuestStatus::Completed ? quest.target : 0,
                                   quest.target, quest.autoComplete});
    for (int i = 0; i < 128; ++i)
        w.events.switches[i] = db.switches[i].initial != 0;
    for (int i = 0; i < 64; ++i)
        w.events.variables[i] = db.variables[i].initial;
}
} // namespace echo
