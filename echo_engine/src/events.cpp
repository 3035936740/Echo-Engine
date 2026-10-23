#include "echo/events.hpp"
#include "echo/database.hpp"
#include "echo/control.hpp"
#include "echo/animations.hpp"
#include "echo/collision.hpp"
#include "echo/audio_state.hpp"
#include "echo/barrage.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace echo {
std::size_t EventMap::bytes() const {
    std::size_t n = sizeof(*this) + events.capacity() * sizeof(EventDefinition);
    for (const auto& e : events) {
        n += e.pages.capacity() * sizeof(EventPage);
        for (const auto& p : e.pages)
            n += p.commands.capacity() * sizeof(EventCommand);
        for (const auto& p : e.pages)
            for (const auto& c : p.commands)
                n += c.choices.capacity() * sizeof(ChoiceDefinition);
    }
    return n;
}
const EventDefinition* findEvent(const EventMap& map, int id) {
    for (const auto& e : map.events)
        if (e.id == id)
            return &e;
    return nullptr;
}
void validateEvents(const EventMap& map, bool common) {
    if (map.events.size() > std::size_t(common ? maxCommonEvents : maxMapEvents))
        throw std::runtime_error("Event capacity exceeded");
    std::array<bool, 33> used{};
    for (const auto& e : map.events) {
        if (e.id < 1 || e.id > (common ? maxCommonEvents : maxMapEvents) || used[e.id] ||
            e.pages.empty() || e.pages.size() > maxEventPages)
            throw std::runtime_error("Invalid event ID / pages");
        used[e.id] = true;
        for (const auto& p : e.pages) {
            if (int(p.trigger) > 6 ||
                (common && (p.trigger == EventTrigger::Touch || int(p.trigger) >= 4)) ||
                p.switchId < -1 || p.switchId >= 128 || p.variableId < -1 || p.variableId >= 64 ||
                p.selfSwitch < -1 || p.selfSwitch > 3 || p.graphic < 0 || p.graphic > 16 ||
                p.commands.size() > maxEventCommands)
                throw std::runtime_error("Invalid event page");
            if (p.scriptConditionEnabled &&
                std::string_view(p.scriptCondition.c_str()).find_first_not_of(" \t\r\n") ==
                    std::string::npos)
                throw std::runtime_error("Event page Lua condition must not be empty");
            std::array<bool, maxEventCommands> hadElse{};
            std::array<EventOp, maxEventCommands> blocks{};
            int loopDepth = 0;
            int depth = 0;
            for (const auto& c : p.commands) {
                if (int(c.op) > int(EventOp::IfQuestProgress) || !std::isfinite(c.value) ||
                    !std::isfinite(c.x) || !std::isfinite(c.y))
                    throw std::runtime_error("Invalid event command");
                if ((c.op == EventOp::Switch || c.op == EventOp::IfSwitch) && (c.a < 0 || c.a >= 128))
                    throw std::runtime_error("Switch ID must be 1..128");
                if ((c.op == EventOp::Variable || c.op == EventOp::AddVariable ||
                     c.op == EventOp::IfVariable) &&
                    (c.a < 0 || c.a >= 64 || std::abs(c.value) > 1000000))
                    throw std::runtime_error("Invalid variable parameter");
                if (c.op == EventOp::IfVariable && (c.b < 0 || c.b > 5))
                    throw std::runtime_error("Invalid comparison");
                if (eventProgression(c.op)) {
                    if (!c.reference || c.reference > 1000000 || c.c < 0 || c.c > 64 ||
                        std::floor(c.value) != c.value || std::abs(c.value) > 1000000 ||
                        ((c.op == EventOp::QuestStatus || c.op == EventOp::IfQuestStatus) &&
                         (c.b < 0 || c.b > 3)) ||
                        ((c.op == EventOp::IfItem || c.op == EventOp::IfQuestProgress) &&
                         (c.b < 0 || c.b > 5)))
                        throw std::runtime_error("Invalid item / quest command");
                }
                if (c.op == EventOp::SelfSwitch && (c.a < 0 || c.a > 3))
                    throw std::runtime_error("Self switch must be A..D");
                if (c.op == EventOp::Wait && (c.value < 0 || c.value > 3600))
                    throw std::runtime_error("Wait must be 0..3600 seconds");
                if (c.op == EventOp::Transfer && (c.a < 0 || c.a > 7))
                    throw std::runtime_error("Map must be 1..8");
                if (c.op == EventOp::Sound && (c.a < 0 || c.a >= 32 || c.value < 0 || c.value > 1))
                    throw std::runtime_error("Invalid SE slot / volume");
                if (c.op == EventOp::FadeMusic && (c.value < 0 || c.value > 3600 || c.x < 0 || c.x > 1))
                    throw std::runtime_error("Invalid BGM fade");
                if (c.op == EventOp::Background && (c.a < 0 || c.a > 6))
                    throw std::runtime_error("Invalid background");
                if (c.op == EventOp::Move && (c.b < 0 || c.b > 1 || c.value < .01f || c.value > 3600 ||
                                              std::abs(c.x) > 8192 || std::abs(c.y) > 8192 || common))
                    throw std::runtime_error("Move needs a map event and positive duration");
                if (c.op == EventOp::CallCommon && (c.a < 1 || c.a > maxCommonEvents))
                    throw std::runtime_error("Common event ID must be 1..16");
                if (c.op == EventOp::ClearBulletGroup && (c.a < 0 || c.a > 65535))
                    throw std::runtime_error("Bullet group: 0..65535");
                if (c.op == EventOp::PlayerControl &&
                    (c.a < 0 || c.a > 5 || c.b < 0 || c.b > allPlayerControls))
                    throw std::runtime_error("Invalid player control profile / permissions");
                if (c.op == EventOp::EmitterControl && (!c.reference || c.b < 0 || c.b > 2))
                    throw std::runtime_error("Emitter reference / operation invalid");
                if ((c.op == EventOp::Script || c.op == EventOp::IfScript) &&
                    std::string_view(c.text.c_str()).find_first_not_of(" \t\r\n") == std::string::npos)
                    throw std::runtime_error("Event Lua source must not be empty");
                if (c.op == EventOp::Choices) {
                    if (c.a < 0 || c.a >= 64 || c.choices.empty() || c.choices.size() > 6)
                        throw std::runtime_error("Choices need a result variable and 1..6 options");
                    for (const auto& o : c.choices) {
                        if (!o.label.c_str()[0] || int(o.condition) > 3 || !std::isfinite(o.threshold) ||
                            std::abs(o.threshold) > 1000000 ||
                            (o.condition == ChoiceCondition::Switch && (o.id < 0 || o.id >= 128)) ||
                            (o.condition == ChoiceCondition::Variable &&
                             (o.id < 0 || o.id >= 64 || o.comparison < 0 || o.comparison > 5)) ||
                            (o.condition == ChoiceCondition::Lua &&
                             std::string_view(o.script.c_str()).find_first_not_of(" \t\r\n") ==
                                 std::string::npos))
                            throw std::runtime_error("Invalid choice label / condition");
                    }
                } else if (!c.choices.empty())
                    throw std::runtime_error("Only Choices commands may own options");
                if (c.op == EventOp::Loop && (c.a < 0 || c.a > 1000000))
                    throw std::runtime_error("Loop count must be 0..1000000");
                if (eventConditional(c.op) || c.op == EventOp::Loop) {
                    if (c.op == EventOp::Choices) {
                        if (c.a < 0 || c.a >= 64 || c.choices.empty() || c.choices.size() > 6)
                            throw std::runtime_error("Choices need a result variable and 1..6 options");
                        for (const auto& o : c.choices) {
                            if (!o.label.c_str()[0] || int(o.condition) > 3 ||
                                !std::isfinite(o.threshold) || std::abs(o.threshold) > 1000000 ||
                                (o.condition == ChoiceCondition::Switch && (o.id < 0 || o.id >= 128)) ||
                                (o.condition == ChoiceCondition::Variable &&
                                 (o.id < 0 || o.id >= 64 || o.comparison < 0 || o.comparison > 5)) ||
                                (o.condition == ChoiceCondition::Lua &&
                                 std::string_view(o.script.c_str()).find_first_not_of(" \t\r\n") ==
                                     std::string::npos))
                                throw std::runtime_error("Invalid choice label / condition");
                        }
                    } else if (!c.choices.empty())
                        throw std::runtime_error("Only Choices commands may own options");
                    if (c.op == EventOp::Loop && ++loopDepth > maxEventLoopDepth)
                        throw std::runtime_error("Loop nesting capacity is 8");
                    blocks[depth] = c.op;
                    hadElse[depth++] = false;
                }
                if (c.op == EventOp::Else) {
                    if (!depth || blocks[depth - 1] == EventOp::Loop || hadElse[depth - 1])
                        throw std::runtime_error("Else without matching condition");
                    hadElse[depth - 1] = true;
                }
                if (c.op == EventOp::EndIf || c.op == EventOp::EndLoop) {
                    if (!depth || (blocks[depth - 1] == EventOp::Loop) != (c.op == EventOp::EndLoop))
                        throw std::runtime_error("Mismatched event block end");
                    --depth;
                    if (c.op == EventOp::EndLoop)
                        --loopDepth;
                }
                if (c.op == EventOp::BreakLoop && !loopDepth)
                    throw std::runtime_error("BreakLoop needs an enclosing loop");
            }
            if (depth)
                throw std::runtime_error("Event block needs its matching end");
        }
    }
}
EventMap loadEvents(const std::string& text, bool common) {
    EventMap result;
    if (text.empty())
        return result;
    std::istringstream in(text);
    std::string magic;
    int count = -1;
    in >> magic >> count;
    if ((magic != "ECHO_EVENTS_1" && magic != "ECHO_EVENTS_2" && magic != "ECHO_EVENTS_3" &&
         magic != "ECHO_EVENTS_4" && magic != "ECHO_EVENTS_5" && magic != "ECHO_EVENTS_6" &&
         magic != "ECHO_EVENTS_7" && magic != "ECHO_EVENTS_8" && magic != "ECHO_EVENTS_9" &&
         magic != "ECHO_EVENTS_10") ||
        count < 0 || count > (common ? maxCommonEvents : maxMapEvents))
        throw std::runtime_error("Invalid event file");
    for (int i = 0; i < count; ++i) {
        EventDefinition e;
        std::string name;
        int pages = -1;
        in >> e.id >> std::quoted(name) >> pages;
        if (name.size() >= 64 || pages < 1 || pages > 4)
            throw std::runtime_error("Invalid event name/pages");
        e.name.set(name);
        for (int j = 0; j < pages; ++j) {
            EventPage p;
            int trigger = -1, commands = -1;
            in >> trigger >> p.switchId >> p.switchValue >> p.variableId >> p.variableMinimum >>
                p.selfSwitch >> p.selfValue >> p.solid >> p.graphic >> commands;
            if (trigger < 0 ||
                trigger >
                    ((magic == "ECHO_EVENTS_3" || magic == "ECHO_EVENTS_4" || magic == "ECHO_EVENTS_5" ||
                      magic == "ECHO_EVENTS_6" || magic == "ECHO_EVENTS_7" || magic == "ECHO_EVENTS_8" ||
                      (magic == "ECHO_EVENTS_9" || magic == "ECHO_EVENTS_10"))
                         ? 6
                         : 3) ||
                commands < 0 || commands > maxEventCommands)
                throw std::runtime_error("Invalid event commands");
            if (magic == "ECHO_EVENTS_8" || (magic == "ECHO_EVENTS_9" || magic == "ECHO_EVENTS_10")) {
                std::string marker, source;
                int enabled = -1;
                in >> marker >> enabled >> std::quoted(source);
                if (marker != "PAGE_CONDITION" || enabled < 0 || enabled > 1 || source.size() >= 1024 ||
                    source.find('\0') != std::string::npos)
                    throw std::runtime_error("Invalid event page Lua condition");
                p.scriptConditionEnabled = enabled != 0;
                p.scriptCondition.set(source);
            }
            p.trigger = EventTrigger(trigger);
            for (int k = 0; k < commands; ++k) {
                EventCommand c;
                int op = -1;
                std::string text, speaker, portrait;
                in >> op >> c.a >> c.b >> c.c >> c.value >> c.x >> c.y >> std::quoted(text) >>
                    std::quoted(speaker) >> std::quoted(portrait);
                if (magic != "ECHO_EVENTS_1")
                    in >> c.reference;
                if (op < 0 ||
                    op > int(magic == "ECHO_EVENTS_10" ? EventOp::IfQuestProgress
                             : (magic == "ECHO_EVENTS_9" || magic == "ECHO_EVENTS_10") ? EventOp::Choices
                             : (magic == "ECHO_EVENTS_7" || magic == "ECHO_EVENTS_8") ? EventOp::IfScript
                             : magic == "ECHO_EVENTS_6" ? EventOp::BreakLoop
                             : magic == "ECHO_EVENTS_5" ? EventOp::Script
                             : magic == "ECHO_EVENTS_4" ? EventOp::PlayerControl
                                                        : EventOp::EmitterControl) ||
                    text.size() >= 1024 || speaker.size() >= 64 || portrait.size() >= 256)
                    throw std::runtime_error("Invalid event text");
                c.op = EventOp(op);
                if ((magic == "ECHO_EVENTS_1" || magic == "ECHO_EVENTS_2") && c.op == EventOp::Move)
                    c.b = 0;
                c.text.set(text);
                c.speaker.set(speaker);
                c.portrait.set(portrait);
                if ((magic == "ECHO_EVENTS_9" || magic == "ECHO_EVENTS_10") &&
                    c.op == EventOp::Choices) {
                    std::string marker;
                    int options = -1;
                    in >> marker >> options;
                    if (marker != "CHOICES" || options < 1 || options > 6)
                        throw std::runtime_error("Invalid choice count");
                    for (int n = 0; n < options; ++n) {
                        ChoiceDefinition o;
                        std::string label, source;
                        int kind = -1, switchValue = -1, hide = -1;
                        in >> std::quoted(label) >> kind >> o.id >> o.comparison >> o.threshold >>
                            switchValue >> hide >> std::quoted(source);
                        if (label.empty() || label.size() >= 128 || source.size() >= 1024 ||
                            label.find('\0') != std::string::npos ||
                            source.find('\0') != std::string::npos || kind < 0 || kind > 3 ||
                            switchValue < 0 || switchValue > 1 || hide < 0 || hide > 1)
                            throw std::runtime_error("Invalid choice definition");
                        o.label.set(label);
                        o.script.set(source);
                        o.condition = ChoiceCondition(kind);
                        o.switchValue = switchValue != 0;
                        o.hideUnavailable = hide != 0;
                        c.choices.push_back(o);
                    }
                }
                p.commands.push_back(c);
            }
            e.pages.push_back(std::move(p));
        }
        result.events.push_back(std::move(e));
    }
    if (!in)
        throw std::runtime_error("Truncated event file");
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Trailing event data");
    validateEvents(result, common);
    return result;
}
std::string saveEvents(const EventMap& map, bool common) {
    validateEvents(map, common);
    std::ostringstream out;
    out.precision(9);
    out << "ECHO_EVENTS_10\n" << map.events.size() << '\n';
    for (const auto& e : map.events) {
        out << e.id << ' ' << std::quoted(e.name.c_str()) << ' ' << e.pages.size() << '\n';
        for (const auto& p : e.pages) {
            out << int(p.trigger) << ' ' << p.switchId << ' ' << p.switchValue << ' ' << p.variableId
                << ' ' << p.variableMinimum << ' ' << p.selfSwitch << ' ' << p.selfValue << ' '
                << p.solid << ' ' << p.graphic << ' ' << p.commands.size() << '\n';
            out << "PAGE_CONDITION " << p.scriptConditionEnabled << ' '
                << std::quoted(p.scriptCondition.c_str()) << '\n';
            for (const auto& c : p.commands) {
                out << int(c.op) << ' ' << c.a << ' ' << c.b << ' ' << c.c << ' ' << c.value << ' '
                    << c.x << ' ' << c.y << ' ' << std::quoted(c.text.c_str()) << ' '
                    << std::quoted(c.speaker.c_str()) << ' ' << std::quoted(c.portrait.c_str()) << ' '
                    << c.reference << '\n';
                if (c.op == EventOp::Choices) {
                    out << "CHOICES " << c.choices.size() << '\n';
                    for (const auto& o : c.choices)
                        out << std::quoted(o.label.c_str()) << ' ' << int(o.condition) << ' ' << o.id
                            << ' ' << o.comparison << ' ' << o.threshold << ' ' << o.switchValue << ' '
                            << o.hideUnavailable << ' ' << std::quoted(o.script.c_str()) << '\n';
                }
            }
        }
    }
    return out.str();
}
EventDefinition eventPreset(int id, int preset) {
    EventDefinition e;
    e.id = id;
    e.name.set(preset == 1 ? "Treasure" : preset == 2 ? "Parallel timer" : preset == 3 ? "Door" : "NPC");
    EventPage page;
    EventCommand c;
    if (preset >= 4 && preset <= 6) {
        e.name.set(preset == 4 ? "Region enter" : preset == 5 ? "Region leave" : "Region contact");
        page.trigger = EventTrigger(preset);
        c.op = EventOp::AddVariable;
        c.a = 0;
        c.value = 1;
        page.commands.push_back(c);
    } else if (preset == 7) {
        e.name.set("Three timed iterations");
        c.op = EventOp::Loop;
        c.a = 3;
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::Wait;
        c.value = 1;
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::AddVariable;
        c.value = 1;
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::EndLoop;
        page.commands.push_back(c);
    } else if (preset == 8) {
        e.name.set("Choice NPC");
        c.op = EventOp::Choices;
        c.speaker.set("NPC");
        c.text.set("What would you like to do?");
        ChoiceDefinition a, b, locked;
        a.label.set("Accept");
        b.label.set("Leave");
        locked.label.set("Secret route");
        locked.condition = ChoiceCondition::Switch;
        locked.id = 0;
        c.choices = {a, b, locked};
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::IfVariable;
        c.b = 1;
        c.value = 1;
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::Switch;
        c.b = 1;
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::Else;
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::Wait;
        c.value = .1f;
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::EndIf;
        page.commands.push_back(c);
    } else if (preset == 2) {
        page.trigger = EventTrigger::Parallel;
        c.op = EventOp::Wait;
        c.value = 1;
        page.commands.push_back(c);
        c = {};
        c.op = EventOp::AddVariable;
        c.a = 0;
        c.value = 1;
        page.commands.push_back(c);
    } else if (preset == 3) {
        c.op = EventOp::Transfer;
        c.a = 0;
        c.b = 1;
        page.commands.push_back(c);
    } else {
        c.op = EventOp::Text;
        c.speaker.set(preset == 1 ? "Treasure" : "NPC");
        c.text.set(preset == 1 ? "Found a treasure!" : "Hello! Change this text in the event editor.");
        page.commands.push_back(c);
        if (preset == 1) {
            c = {};
            c.op = EventOp::AddVariable;
            c.value = 1;
            page.commands.push_back(c);
            c = {};
            c.op = EventOp::SelfSwitch;
            c.b = 1;
            page.commands.push_back(c);
        }
    }
    e.pages.push_back(page);
    if (preset == 1) {
        EventPage opened;
        opened.selfSwitch = 0;
        opened.graphic = 2;
        e.pages.push_back(opened);
    }
    return e;
}
const DialogueDefinition& activeDialogue(const WorldState& w) {
    return w.dialogue.active == 8 ? w.events.dialogue : w.level.dialogues[w.dialogue.active];
}
bool eventLocksPlayer(const WorldState& w) {
    return !resolveControl(w).permits(PlayerControl::Move);
}
namespace {
EventRunner& runner(WorldState& w, int owner) {
    return owner < 32 ? w.events.local[owner] : w.events.common[owner - 32];
}
void cancelRunner(WorldState& w, int owner) {
    auto& r = runner(w, owner);
    auto generation = r.callGeneration + ((r.running || r.requested) ? 1 : 0);
    int pageFault = r.pageConditionFault;
    r = {};
    r.pageConditionFault = pageFault;
    r.callGeneration = generation;
    if (w.events.foreground == owner)
        w.events.foreground = -1;
    if (w.dialogue.active == 8 && w.events.dialogueOwner == owner) {
        w.dialogue = {};
        w.events.dialogueOwner = -1;
        w.gameplayClockPaused = w.state == TimeState::Hit;
    }
}
} // namespace
void cancelEventChildren(WorldState& w, int owner) {
    // Valid calls form a bounded acyclic tree. Iterative cancellation cannot allocate.
    std::array<bool, 48> cancelled{};
    cancelled[owner] = true;
    for (int pass = 0; pass < 16; ++pass) {
        bool changed = false;
        for (int i = 0; i < 16; ++i) {
            auto& r = w.events.common[i];
            if (r.called && r.caller >= 0 && r.caller < 48 && cancelled[r.caller] &&
                !cancelled[i + 32]) {
                cancelled[i + 32] = true;
                cancelRunner(w, i + 32);
                changed = true;
            }
        }
        if (!changed)
            break;
    }
}
void cancelEvents(WorldState& w, bool includeCommon) {
    if (includeCommon && w.state == TimeState::Hit) {
        w.dialogue = {};
        w.events.dialogueOwner = -1;
        w.gameplayClockPaused = true;
    }
    for (int i = 0; i < (includeCommon ? 48 : 32); ++i) {
        cancelEventChildren(w, i);
        cancelRunner(w, i);
        runner(w, i).pageConditionFault = -1;
    }
}
namespace {
bool compare(int lhs, int rhs, int op) {
    switch (op) {
    case 0:
        return lhs >= rhs;
    case 1:
        return lhs == rhs;
    case 2:
        return lhs < rhs;
    case 3:
        return lhs > rhs;
    case 4:
        return lhs <= rhs;
    default:
        return lhs != rhs;
    }
}
Text<1024> eventText(const WorldState& w, const Text<1024>& source) {
    Text<1024> result;
    std::string_view input(source.c_str());
    std::size_t output = 0;
    auto append = [&](std::string_view value) {
        if (output + value.size() > 1023)
            return false;
        std::copy(value.begin(), value.end(), result.data.begin() + output);
        output += value.size();
        return true;
    };
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] == '\\' && i + 3 < input.size() && input[i + 1] == 'V' && input[i + 2] == '[') {
            auto end = input.find(']', i + 3);
            if (end != std::string_view::npos) {
                int id = 0;
                auto parsed = std::from_chars(input.data() + i + 3, input.data() + end, id);
                if (parsed.ec == std::errc{} && parsed.ptr == input.data() + end && id >= 1 &&
                    id <= 64) {
                    char number[32];
                    auto last = std::to_chars(number, number + 32, w.events.variables[id - 1]);
                    if (!append({number, std::size_t(last.ptr - number)}))
                        break;
                    i = end + 1;
                    continue;
                }
            }
        }
        auto lead = static_cast<unsigned char>(input[i]);
        std::size_t bytes = lead < 128 ? 1 : (lead < 224 ? 2 : (lead < 240 ? 3 : 4));
        bytes = std::min(bytes, input.size() - i);
        if (!append(input.substr(i, bytes)))
            break;
        i += bytes;
    }
    return result;
}
int loopEnd(const EventPage& p, int pc) {
    int depth = 0;
    for (int i = pc + 1; i < int(p.commands.size()); ++i) {
        if (p.commands[i].op == EventOp::Loop)
            ++depth;
        if (p.commands[i].op == EventOp::EndLoop) {
            if (!depth)
                return i;
            --depth;
        }
    }
    return int(p.commands.size());
}
int skipBranch(const EventPage& p, int pc, bool allowElse) {
    int depth = 0;
    for (int i = pc + 1; i < int(p.commands.size()); ++i) {
        auto op = p.commands[i].op;
        if (eventConditional(op))
            ++depth;
        if (op == EventOp::EndIf) {
            if (!depth)
                return i + 1;
            --depth;
        }
        if (op == EventOp::Else && !depth && allowElse)
            return i + 1;
    }
    return int(p.commands.size());
}
} // namespace
void stepEvents(WorldState& w, const EventMap& local, const EventMap& common, bool interact, float dt,
                EventScriptExecutor* scripts) {
    if (scripts)
        scripts->beginEventStep(w);
    bool pressed = interact && !w.events.previousInteract;
    w.events.previousInteract = interact;
    // Conditions are sampled on first query per owner/substep. No result survives a frame:
    // persistent page, task progress and faults live in WorldState, never in this cache.
    std::array<int, 48> selectedPages;
    selectedPages.fill(-2);
    std::array<bool, 48> deferred{};
    auto pageFor = [&](const EventDefinition& d, int bank) {
        int owner = bank == 8 ? 32 + d.id - 1 : d.id - 1;
        auto& r = runner(w, owner);
        if (r.pageConditionFault >= 0)
            return -1;
        bool scripted = std::any_of(d.pages.begin(), d.pages.end(),
                                    [](const EventPage& p) { return p.scriptConditionEnabled; });
        if (scripted && selectedPages[owner] != -2)
            return selectedPages[owner];
        for (int i = int(d.pages.size()) - 1; i >= 0; --i) {
            const auto& p = d.pages[i];
            if (p.switchId >= 0 && bool(w.events.switches[p.switchId]) != p.switchValue)
                continue;
            if (p.variableId >= 0 && w.events.variables[p.variableId] < p.variableMinimum)
                continue;
            if (p.selfSwitch >= 0 &&
                bool(w.events.self[bank][d.id - 1] & (1 << p.selfSwitch)) != p.selfValue)
                continue;
            if (p.scriptConditionEnabled) {
                if (scripts && !scripts->eventConditionAvailable()) {
                    // Budget pressure freezes this owner until a fair later substep; no stale
                    // commands run and no valid definition is permanently faulted for capacity.
                    deferred[owner] = true;
                    ++w.events.pageConditionDeferrals;
                    return selectedPages[owner] = r.page;
                }
                bool result = false;
                if (!scripts ||
                    !scripts->eventCondition(w, {w.activeMap, d.id, i, -1, bank == 8}, result)) {
                    cancelEventChildren(w, owner);
                    cancelRunner(w, owner);
                    r.pageConditionFault = i;
                    ++w.events.errors;
                    return selectedPages[owner] = -1;
                }
                if (!result)
                    continue;
            }
            return selectedPages[owner] = i;
        }
        return selectedPages[owner] = -1;
    };
    int budget = 256;
    int interactId = -1;
    float nearest = 64 * 64;
    if (pressed && w.events.foreground < 0)
        for (const auto& placed : w.level.events) {
            auto* d = findEvent(local, placed.id);
            if (!d || placed.erased ||
                !collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger))
                continue;
            int page = pageFor(*d, w.activeMap);
            if (deferred[placed.id - 1] || page < 0 || d->pages[page].trigger != EventTrigger::Action)
                continue;
            float dx = placed.region.x + placed.region.w * .5f - w.kino.x - 14;
            float dy = placed.region.y + placed.region.h * .5f - w.kino.y - 24;
            float distance = dx * dx + dy * dy;
            if (dx * w.kino.facing >= -14 && distance < nearest) {
                nearest = distance;
                interactId = placed.id;
            }
        }
    auto run = [&](const EventDefinition& d, EventRunner& r, EventPlacement* placed, int owner,
                   int bank) {
        int page = placed && placed->erased ? -1 : pageFor(d, bank);
        if (deferred[owner])
            return;
        if (r.page != page) {
            cancelEventChildren(w, owner);
            if (w.events.foreground == owner)
                w.events.foreground = -1;
            // Page switching cancels the old interpreter, including its owned dialogue.
            if (w.dialogue.active == 8 && w.events.dialogueOwner == owner) {
                w.dialogue = {};
                w.events.dialogueOwner = -1;
                w.gameplayClockPaused = false;
            }
            int pageFault = r.pageConditionFault;
            bool requested = r.requested, called = r.requested && r.called;
            int caller = r.caller;
            auto callGeneration = r.callGeneration + (r.running && r.called ? 1 : 0);
            bool entered = r.entered, initialized = r.contactInitialized;
            Rect previousContact = r.previousContact, previousRegion = r.previousRegion;
            r = {};
            r.pageConditionFault = pageFault;
            r.entered = entered;
            r.contactInitialized = initialized;
            r.previousContact = previousContact;
            r.previousRegion = previousRegion;
            r.callGeneration = callGeneration;
            r.page = page;
            r.requested = requested;
            r.called = called;
            r.caller = called ? caller : -1;
        }
        if (page < 0 || (placed && placed->erased)) {
            cancelEventChildren(w, owner);
            r.controlActive = false;
            r.controlAllowed = allPlayerControls;
            r.caller = -1;
            if (w.events.foreground == owner)
                w.events.foreground = -1;
            if (w.dialogue.active == 8 && w.events.dialogueOwner == owner) {
                w.dialogue = {};
                w.events.dialogueOwner = -1;
                w.gameplayClockPaused = false;
            }
            if (r.running && r.called)
                ++r.callGeneration;
            r.running = false;
            r.loops = {};
            r.loopDepth = 0;
            r.called = false;
            r.triggerPending = false;
            r.entered = false;
            r.contactInitialized = false;
            if (placed) {
                placed->visible = false;
                placed->solid = false;
            }
            if (r.requested) {
                r.requested = false;
                r.called = false;
                ++w.events.errors;
            }
            return;
        }
        const auto& p = d.pages[page];
        if (r.scriptFault)
            return;
        if (placed) {
            placed->solid = p.solid;
            placed->graphic = p.graphic;
            placed->visible = true;
        }
        const auto body = kinoBody(w);
        bool inside = placed && collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger) &&
                      body.x + body.w >= placed->region.x - .1f &&
                      body.x <= placed->region.x + placed->region.w + .1f &&
                      body.y + body.h >= placed->region.y - .1f &&
                      body.y <= placed->region.y + placed->region.h + .1f;
        bool crossed = false;
        if (placed && r.contactInitialized && !r.entered && !inside &&
            collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger)) {
            StaticCollider c;
            c.bounds = r.previousRegion;
            float hit;
            crossed =
                sweepCollider(r.previousContact,
                              {body.x - r.previousContact.x - placed->region.x + r.previousRegion.x,
                               body.y - r.previousContact.y - placed->region.y + r.previousRegion.y},
                              c, hit);
        }
        bool touched = (inside && !r.entered) || crossed;
        bool left = (!inside && r.entered) || crossed;
        r.entered = inside;
        r.previousContact = body;
        if (placed)
            r.previousRegion = placed->region;
        r.contactInitialized = placed != nullptr;
        if (((p.trigger == EventTrigger::Touch || p.trigger == EventTrigger::Enter) && touched) ||
            (p.trigger == EventTrigger::Leave && left))
            r.triggerPending = true;
        if (!collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger))
            r.triggerPending = false;
        bool near = placed && placed->id == interactId;
        if (!r.running) {
            bool start =
                p.trigger == EventTrigger::Parallel ||
                (p.trigger == EventTrigger::Autorun && w.events.foreground < 0) ||
                (placed &&
                 (p.trigger == EventTrigger::Touch || p.trigger == EventTrigger::Enter ||
                  p.trigger == EventTrigger::Leave) &&
                 r.triggerPending && w.events.foreground < 0) ||
                (placed && p.trigger == EventTrigger::Contact && inside && w.events.foreground < 0) ||
                (placed && p.trigger == EventTrigger::Action && pressed && near &&
                 w.events.foreground < 0) ||
                r.requested;
            if (!start)
                return;
            r.requested = false;
            r.triggerPending = false;
            r.running = true;
            r.pc = 0;
            r.loops = {};
            r.loopDepth = 0;
            if (p.trigger != EventTrigger::Parallel && !r.called)
                w.events.foreground = owner;
        }
        if (r.waitCommon >= 0) {
            auto& other = w.events.common[r.waitCommon];
            if (other.callGeneration == r.waitCommonGeneration && (other.running || other.requested))
                return;
            r.waitCommon = -1;
        }
        if (r.waitDialogue) {
            if (w.dialogue.active == 8 && w.events.dialogueOwner == owner)
                return;
            r.waitDialogue = false;
        }
        if (r.moveRemaining > 0 && placed) {
            float step = std::min(dt, r.moveRemaining);
            Vec2 delta{r.moveVelocity.x * step, r.moveVelocity.y * step};
            if (r.moveCollide)
                delta = moveEventBody(w, placed->id, placed->region, delta, r.moveBlocked);
            placed->region.x += delta.x;
            placed->region.y += delta.y;
            r.moveRemaining = std::max(0.0f, r.moveRemaining - dt);
            if (r.moveRemaining > 0)
                return;
        }
        if (r.wait > 0) {
            r.wait = std::max(0.0f, r.wait - dt);
            if (r.wait > 0)
                return;
        }
        for (int steps = 0; steps < 16 && budget > 0 && r.running; ++steps) {
            --budget;
            if (r.pc >= int(p.commands.size())) {
                r.running = false;
                r.loops = {};
                r.loopDepth = 0;
                r.controlActive = false;
                r.controlAllowed = allPlayerControls;
                r.caller = -1;
                if (r.called)
                    ++r.callGeneration;
                r.called = false;
                if (w.events.foreground == owner)
                    w.events.foreground = -1;
                break;
            }
            const auto& c = p.commands[r.pc];
            switch (c.op) {
            case EventOp::Loop: {
                // Fixed capacity; per-runner and global command budgets also bound zero-wait loops.
                if (r.loopDepth >= maxEventLoopDepth) {
                    cancelEventChildren(w, owner);
                    cancelRunner(w, owner);
                    ++w.events.errors;
                    return;
                }
                r.loops[r.loopDepth++] = {r.pc, loopEnd(p, r.pc), c.a};
                break;
            }
            case EventOp::EndLoop: {
                if (!r.loopDepth || r.loops[r.loopDepth - 1].end != r.pc) {
                    cancelEventChildren(w, owner);
                    cancelRunner(w, owner);
                    ++w.events.errors;
                    return;
                }
                auto& loop = r.loops[r.loopDepth - 1];
                if (loop.remaining == 0 || --loop.remaining > 0) {
                    r.pc = loop.begin + 1;
                    continue;
                }
                loop = {};
                --r.loopDepth;
                break;
            }
            case EventOp::BreakLoop:
                if (r.loopDepth) {
                    r.pc = r.loops[r.loopDepth - 1].end + 1;
                    r.loops[--r.loopDepth] = {};
                    continue;
                }
                cancelEventChildren(w, owner);
                cancelRunner(w, owner);
                ++w.events.errors;
                return;
            case EventOp::IfScript:
            case EventOp::Script: {
                bool result = false;
                EventScriptContext context{w.activeMap, d.id, page, r.pc, bank == 8};
                bool success =
                    scripts && (c.op == EventOp::IfScript ? scripts->eventCondition(w, context, result)
                                                          : scripts->eventScript(w, context));
                if (!success) {
                    // Lua rolls back its command writes; terminate only this owner and children.
                    int failedPc = r.pc;
                    cancelEventChildren(w, owner);
                    cancelRunner(w, owner);
                    r.page = page;
                    r.pc = failedPc;
                    r.scriptFault = true;
                    ++w.events.errors;
                    return;
                }
                if (c.op == EventOp::IfScript && !result) {
                    r.pc = skipBranch(p, r.pc, true);
                    continue;
                }
                break;
            }
            case EventOp::PlayerControl:
                r.controlActive = c.a != 0;
                r.controlAllowed = controlProfile(c.a, std::uint16_t(c.b));
                break;
            case EventOp::Choices: {
                if (w.dialogue.active >= 0)
                    return;
                DialogueState next;
                next.active = 8;
                next.choosing = true;
                next.resultVariable = c.a;
                next.previousConfirm =
                    interact || (w.controlInput.physicalHeld & controlBit(PlayerControl::Confirm));
                bool any = false, success = true;
                for (int option = 0; option < int(c.choices.size()); ++option) {
                    const auto& o = c.choices[option];
                    bool enabled = true;
                    if (o.condition == ChoiceCondition::Switch)
                        enabled = bool(w.events.switches[o.id]) == o.switchValue;
                    else if (o.condition == ChoiceCondition::Variable) {
                        int v = w.events.variables[o.id];
                        enabled = o.comparison == 0   ? v >= o.threshold
                                  : o.comparison == 1 ? v == o.threshold
                                  : o.comparison == 2 ? v < o.threshold
                                  : o.comparison == 3 ? v > o.threshold
                                  : o.comparison == 4 ? v <= o.threshold
                                                      : v != o.threshold;
                    } else if (o.condition == ChoiceCondition::Lua) {
                        EventScriptContext context{w.activeMap, d.id, page, r.pc, bank == 8, option};
                        if (scripts && !scripts->eventConditionAvailable()) {
                            ++w.events.budgetYields;
                            return; // retry the whole read-only condition set on a later tick
                        }
                        success = scripts && scripts->eventCondition(w, context, enabled);
                        if (!success)
                            break;
                    }
                    if (!enabled && o.hideUnavailable)
                        continue;
                    DialogueOption runtime;
                    runtime.label = o.label; // immutable UTF-8 label; no unbounded runtime formatting
                    runtime.result = option + 1; // authored index survives hidden/disabled entries
                    runtime.enabled = enabled;
                    if (enabled && !any)
                        next.selected = int(next.options.count);
                    any |= enabled;
                    next.options.push(runtime);
                }
                if (!success || !any) {
                    int failedPc = r.pc;
                    cancelEventChildren(w, owner);
                    cancelRunner(w, owner);
                    r.page = page;
                    r.pc = failedPc;
                    r.scriptFault = true;
                    ++w.events.errors;
                    return; // no selectable option must never deadlock the entire game
                }
                w.events.dialogue = {};
                w.events.dialogue.text = eventText(w, c.text);
                w.events.dialogue.speaker = c.speaker;
                w.events.dialogue.portrait = c.portrait;
                w.dialogue = next;
                w.events.dialogueOwner = owner;
                w.gameplayClockPaused = true;
                r.waitDialogue = true;
                ++r.pc;
                return;
            }
            case EventOp::Text:
                if (w.dialogue.active >= 0)
                    return;
                w.events.dialogue = {};
                w.events.dialogue.text = eventText(w, c.text);
                w.events.dialogue.speaker = c.speaker;
                w.events.dialogue.portrait = c.portrait;
                w.dialogue = {};
                w.dialogue.active = 8;
                w.dialogue.previousConfirm =
                    interact || (w.controlInput.physicalHeld & controlBit(PlayerControl::Confirm));
                w.gameplayClockPaused = true;
                w.events.dialogueOwner = owner;
                r.waitDialogue = true;
                ++r.pc;
                return;
            case EventOp::Wait:
                r.wait = c.value;
                ++r.pc;
                return;
            case EventOp::Switch:
                w.events.switches[c.a] = c.b != 0;
                break;
            case EventOp::SelfSwitch:
                if (c.b)
                    w.events.self[bank][d.id - 1] |= 1 << c.a;
                else
                    w.events.self[bank][d.id - 1] &= ~(1 << c.a);
                ++r.pc;
                return; // Re-evaluate higher-priority pages before another instruction.
            case EventOp::Variable:
                w.events.variables[c.a] = int(c.value);
                break;
            case EventOp::AddVariable:
                w.events.variables[c.a] =
                    int(std::clamp(std::int64_t(w.events.variables[c.a]) + int(c.value),
                                   std::int64_t(std::numeric_limits<int>::min()),
                                   std::int64_t(std::numeric_limits<int>::max())));
                break;
            case EventOp::ChangeItem:
            case EventOp::QuestStatus:
            case EventOp::QuestProgress: {
                auto id = std::uint32_t(c.reference);
                auto result = c.op == EventOp::ChangeItem    ? changeItem(w, id, int(c.value))
                              : c.op == EventOp::QuestStatus ? setQuestStatus(w, id, QuestStatus(c.b))
                                                             : addQuestProgress(w, id, int(c.value));
                if (c.c)
                    w.events.variables[c.c - 1] = int(result);
                if (result == ProgressResult::Missing)
                    ++w.events.errors;
                break;
            }
            case EventOp::IfItem:
            case EventOp::IfQuestStatus:
            case EventOp::IfQuestProgress: {
                bool matches = false;
                if (c.op == EventOp::IfItem) {
                    if (auto* item = findItem(w, std::uint32_t(c.reference)))
                        matches = compare(item->count, int(c.value), c.b);
                } else if (auto* q = findQuest(w, std::uint32_t(c.reference)))
                    matches = c.op == EventOp::IfQuestStatus ? int(q->status) == c.b
                                                             : compare(q->progress, int(c.value), c.b);
                if (!matches) {
                    r.pc = skipBranch(p, r.pc, true);
                    continue;
                }
                break;
            }
            case EventOp::IfSwitch:
                if (bool(w.events.switches[c.a]) != (c.b != 0)) {
                    r.pc = skipBranch(p, r.pc, true);
                    continue;
                }
                break;
            case EventOp::IfVariable:
                if (!compare(w.events.variables[c.a], int(c.value), c.b)) {
                    r.pc = skipBranch(p, r.pc, true);
                    continue;
                }
                break;
            case EventOp::Else:
                r.pc = skipBranch(p, r.pc, false);
                continue;
            case EventOp::EndIf:
                break;
            case EventOp::Transfer:
                w.events.transferMap = c.a;
                w.events.transferSpawn = c.b != 0;
                w.events.transferPoint = {c.x, c.y};
                ++r.pc;
                return;
            case EventOp::Sound: {
                // Audio requests use the same snapshotted mixer API as Lua.
                playSound(w, c.a, c.value, 1);
                break;
            }
            case EventOp::FadeMusic:
                w.audio.fadeStart = w.audio.musicVolume;
                w.audio.fadeTarget = c.x;
                w.audio.fadeDuration = c.value;
                w.audio.fadeElapsed = 0;
                if (c.value == 0)
                    w.audio.musicVolume = c.x;
                break;
            case EventOp::PlayMusic:
                w.audio.paused = false;
                startMusic(w, c.b != 0);
                break;
            case EventOp::StopMusic:
                stopMusic(w);
                break;
            case EventOp::ClearBulletGroup:
                clearBulletGroup(w, std::uint16_t(c.a));
                break;
            case EventOp::EmitterControl:
                if (!setBarrageEmitter(w, c.reference, c.b > 0, c.b == 2))
                    ++w.events.errors;
                break;
            case EventOp::Background:
                w.level.background = c.a;
                break;
            case EventOp::Move:
                if (placed) {
                    r.moveVelocity = {c.x / c.value, c.y / c.value};
                    r.moveRemaining = c.value;
                    r.moveCollide = c.b == 1;
                    r.moveBlocked = false;
                }
                ++r.pc;
                return;
            case EventOp::Erase:
                if (placed) {
                    placed->erased = true;
                    placed->visible = false;
                    placed->solid = false;
                }
                r.pc = int(p.commands.size());
                continue;
            case EventOp::Exit:
                r.pc = int(p.commands.size());
                continue;
            case EventOp::CallCommon: {
                const auto* target = findEvent(common, c.a);
                auto& other = w.events.common[c.a - 1];
                bool cycle = owner == 32 + c.a - 1;
                int link = other.waitCommon, depth = 0;
                while (link >= 0 && depth++ < 8) {
                    if (owner == 32 + link)
                        cycle = true;
                    link = w.events.common[link].waitCommon;
                }
                int targetPage = target ? pageFor(*target, 8) : -1;
                if (deferred[32 + c.a - 1])
                    return;
                if (!target || targetPage < 0 ||
                    target->pages[targetPage].trigger != EventTrigger::Action ||
                    (other.scriptFault && other.page == targetPage) || cycle || depth >= 8) {
                    ++w.events.errors;
                    break;
                }
                if (other.running || other.requested)
                    return;
                other.requested = true;
                other.called = true;
                other.caller = owner;
                r.waitCommonGeneration = ++other.callGeneration;
                r.waitCommon = c.a - 1;
                ++r.pc;
                return;
            }
            }
            ++r.pc;
        }
        if (r.running && budget > 0)
            ++w.events.budgetYields;
    };
    // Round-robin scheduling is itself snapshot state: busy parallel programs
    // cannot permanently starve later map or global events.
    for (int i = 0; i < 48; ++i) {
        int slot = int((w.events.scheduleCursor + i) % 48);
        if (slot < 32) {
            for (auto& placed : w.level.events)
                if (placed.id == slot + 1) {
                    auto* d = findEvent(local, placed.id);
                    if (d)
                        run(*d, w.events.local[slot], &placed, slot, w.activeMap);
                    break;
                }
        } else {
            auto* d = findEvent(common, slot - 32 + 1);
            if (d)
                run(*d, w.events.common[slot - 32], nullptr, slot, 8);
        }
    }
    for (int slot = 0; slot < 48; ++slot) {
        bool exists = slot >= 32 ? findEvent(common, slot - 31) != nullptr : false;
        if (slot < 32 && findEvent(local, slot + 1))
            for (const auto& placed : w.level.events)
                if (placed.id == slot + 1)
                    exists = true;
        if (!exists) {
            cancelEventChildren(w, slot);
            cancelRunner(w, slot);
            runner(w, slot).pageConditionFault = -1;
        }
    }
    w.events.scheduleCursor = (w.events.scheduleCursor + 1) % 48;
    if (budget <= 0)
        ++w.events.budgetYields;
}
bool eventUsesEmitter(const EventMap& map, std::uint64_t id) {
    for (const auto& e : map.events)
        for (const auto& page : e.pages)
            for (const auto& command : page.commands)
                if (command.op == EventOp::EmitterControl && command.reference == id)
                    return true;
    return false;
}
} // namespace echo
