#include "echo/recovery.hpp"
#include "echo/objects.hpp"
#include "echo/animations.hpp"
#include "echo/barrage.hpp"
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <set>
namespace echo {
std::string saveRecovery(const Recovery& r) {
    if (r.files.size() > 9)
        throw std::runtime_error("Too many recovery entries");
    std::ostringstream s;
    s << "ECHO_RECOVERY_1\n" << std::quoted(r.map) << '\n' << r.files.size() << '\n';
    for (const auto& [name, text] : r.files)
        s << std::quoted(name) << ' ' << std::quoted(text) << '\n';
    auto result = s.str();
    if (result.size() > recoveryBudget)
        throw std::runtime_error("Recovery exceeds 32 MiB");
    return result;
}
Recovery loadRecovery(std::string_view text) {
    if (text.size() > recoveryBudget)
        throw std::runtime_error("Recovery exceeds 32 MiB");
    std::istringstream s{std::string(text)};
    std::string magic;
    std::size_t count;
    Recovery r;
    if (!(s >> magic >> std::quoted(r.map) >> count) || magic != "ECHO_RECOVERY_1" || count > 9)
        throw std::runtime_error("Invalid recovery header");
    for (std::size_t i = 0; i < count; ++i) {
        std::string name, value;
        if (!(s >> std::quoted(name) >> std::quoted(value)) || !r.files.emplace(name, value).second)
            throw std::runtime_error("Invalid or duplicate recovery entry");
    }
    s >> std::ws;
    if (!s.eof())
        throw std::runtime_error("Trailing recovery data");
    return r;
}
void validateRecovery(const Recovery& r, const Resources& original) {
    std::set<std::string> required{"project.echo", "database.echo", "events/common.echo", r.map,
                                   r.map + ".events"};
    if (r.files.contains("objects.echo"))
        required.insert("objects.echo");
    if (r.files.contains("animations.echo"))
        required.insert("animations.echo");
    if (r.files.contains("barrage.echo"))
        required.insert("barrage.echo");
    if (r.files.contains("assets.echo"))
        required.insert("assets.echo");
    if (required.size() < 5 || r.files.size() != required.size())
        throw std::runtime_error("Unexpected recovery resources");
    for (const auto& [name, value] : r.files)
        if (!required.contains(name))
            throw std::runtime_error("Unexpected recovery resource");
    Resources candidate = original;
    candidate.setEditorOverlay(r.files);
    auto config = candidate.config();
    if (std::find(config.maps.begin(), config.maps.end(), r.map) == config.maps.end())
        throw std::runtime_error("Recovery map is absent from project");
    loadDatabase(candidate.text("database.echo"));
    ObjectLibrary objects;
    if (candidate.contains("objects.echo"))
        objects = loadObjects(candidate.text("objects.echo"));
    if (candidate.contains("animations.echo"))
        loadAnimations(candidate.text("animations.echo"));
    if (candidate.contains("barrage.echo"))
        validateBarrage(loadBarrage(candidate.text("barrage.echo")), int(config.maps.size()));
    loadEvents(candidate.text("events/common.echo"), true);
    for (const auto& map : config.maps) {
        auto level = loadLevel(candidate.text(map));
        auto events = candidate.contains(map + ".events") ? loadEvents(candidate.text(map + ".events"))
                                                          : EventMap{};
        for (const auto& placed : level.objects)
            if (!findObjectTemplate(objects, placed.templateId))
                throw std::runtime_error("Recovery has orphan object template");
        for (const auto& placed : level.events)
            if (!findEvent(events, placed.id))
                throw std::runtime_error("Recovery has orphan event placement");
    }
}
std::vector<ProjectIssue> inspectProject(const Resources& r) {
    std::vector<ProjectIssue> issues;
    auto issue = [&](bool error, std::string where, std::string message) {
        if (issues.size() < 512)
            issues.push_back({error, std::move(where), std::move(message)});
    };
    auto asset = [&](const std::string& name, const std::string& where) {
        if (name.empty())
            return;
        try {
            if (!r.contains(name))
                issue(true, where, "Missing resource: " + name);
        } catch (const std::exception& e) {
            issue(true, where, e.what());
        }
    };
    ProjectConfig c;
    try {
        c = r.config();
    } catch (const std::exception& e) {
        issue(true, "project.echo", e.what());
        return issues;
    }
    asset(c.player.path, "Player");
    asset(c.boss.path, "Boss");
    asset(c.tileSprite, "Tiles");
    asset(c.backgroundShader, "Background shader");
    asset(c.lightingShader, "Lighting shader");
    asset(c.fluidShader, "Fluid shader");
    asset(c.raymarchShader, "Raymarch shader");
    asset(c.transitionShader, "Transition shader");
    asset(c.music, "BGM");
    for (const auto& se : c.effects)
        asset(se, "SE");
    for (const auto& script : c.scripts)
        asset(script, "Lua");
    ObjectLibrary objects;
    try {
        if (r.contains("objects.echo"))
            objects = loadObjects(r.text("objects.echo"));
        for (const auto& t : objects.templates)
            asset(t.sprite.c_str(), "Object template #" + std::to_string(t.id));
    } catch (const std::exception& e) {
        issue(true, "objects.echo", e.what());
    }
    try {
        if (r.contains("animations.echo")) {
            const auto animations = loadAnimations(r.text("animations.echo"));
            for (const auto& clip : animations.clips)
                asset(clip.sprite.c_str(), "Animation #" + std::to_string(clip.id));
            for (const auto& binding : animations.bindings)
                if (!findObjectTemplate(objects, binding.templateId))
                    issue(true, "animations.echo",
                          "Missing animation object template: " + std::to_string(binding.templateId));
        }
    } catch (const std::exception& e) {
        issue(true, "animations.echo", e.what());
    }
    BarrageLibrary barrage;
    try {
        if (r.contains("barrage.echo")) {
            barrage = loadBarrage(r.text("barrage.echo"));
            validateBarrage(barrage, int(c.maps.size()));
            for (const auto& e : barrage.emitters) {
                if (e.origin != BarrageOrigin::Object && e.target != BarrageTarget::Object)
                    continue;
                auto level = loadLevel(r.text(c.maps[e.map]));
                auto exists = [&](std::uint64_t id) {
                    return std::any_of(level.objects.begin(), level.objects.end(),
                                       [&](const auto& o) { return o.id == id; });
                };
                if (e.origin == BarrageOrigin::Object && !exists(e.object))
                    issue(true, "barrage.echo",
                          "Emitter #" + std::to_string(e.id) + " references missing parent object #" +
                              std::to_string(e.object));
                if (e.target == BarrageTarget::Object && !exists(e.targetObject))
                    issue(true, "barrage.echo",
                          "Emitter #" + std::to_string(e.id) + " references missing target object #" +
                              std::to_string(e.targetObject));
            }
        }
    } catch (const std::exception& e) {
        issue(true, "barrage.echo", e.what());
    }
    EventMap common;
    try {
        if (r.contains("events/common.echo"))
            common = loadEvents(r.text("events/common.echo"), true);
    } catch (const std::exception& e) {
        issue(true, "events/common.echo", e.what());
    }
    auto progressionDb = std::make_unique<Database>();
    try {
        if (r.contains("database.echo"))
            *progressionDb = loadDatabase(r.text("database.echo"));
    } catch (const std::exception& e) {
        issue(true, "database.echo", e.what());
    }
    auto eventCheck = [&](const EventMap& map, const std::string& where) {
        for (const auto& event : map.events)
            for (const auto& page : event.pages)
                for (const auto& cmd : page.commands) {
                    std::string location = where + " / event #" + std::to_string(event.id);
                    if (eventProgression(cmd.op) &&
                        !progressionReferenceValid(*progressionDb, eventQuest(cmd.op), cmd.reference))
                        issue(true, location, "Missing item/quest #" + std::to_string(cmd.reference));
                    if (cmd.op == EventOp::EmitterControl &&
                        std::none_of(barrage.emitters.begin(), barrage.emitters.end(),
                                     [&](const auto& e) { return e.id == cmd.reference; }))
                        issue(true, location, "Emitter target is absent");
                    if (cmd.op == EventOp::Text)
                        asset(cmd.portrait.c_str(), location);
                    if (cmd.op == EventOp::Transfer && (cmd.a < 0 || cmd.a >= int(c.maps.size())))
                        issue(true, location, "Transfer target map is absent");
                    if (cmd.op == EventOp::CallCommon && !findEvent(common, cmd.a))
                        issue(true, location, "Common event target is absent");
                    if (cmd.op == EventOp::Sound &&
                        (cmd.a < 0 || cmd.a >= 32 || c.effects[cmd.a].empty()))
                        issue(false, location, "SE slot is empty");
                }
    };
    eventCheck(common, "events/common.echo");
    for (const auto& name : c.maps) {
        try {
            auto level = loadLevel(r.text(name));
            auto map = r.contains(name + ".events") ? loadEvents(r.text(name + ".events")) : EventMap{};
            for (const auto& placed : level.objects) {
                if (!findObjectTemplate(objects, placed.templateId))
                    issue(true, name, "Missing object template #" + std::to_string(placed.templateId));
                if (placed.eventId && !findEvent(map, placed.eventId))
                    issue(true, name,
                          "Object references missing event #" + std::to_string(placed.eventId));
            }
            for (const auto& placed : level.events)
                if (!findEvent(map, placed.id))
                    issue(true, name, "Orphan event #" + std::to_string(placed.id));
            for (const auto& event : map.events) {
                bool used = false;
                for (const auto& placed : level.events)
                    used |= placed.id == event.id;
                if (!used)
                    issue(false, name, "Event has no placement: #" + std::to_string(event.id));
            }
            for (const auto& layer : level.layers)
                asset(layer.image.c_str(), name);
            for (const auto& dialogue : level.dialogues)
                asset(dialogue.portrait.c_str(), name);
            for (const auto& portal : level.portals) {
                if (portal.targetMap < 0 || portal.targetMap >= int(c.maps.size()))
                    issue(true, name, "Portal target map is absent");
            }
            int x = int(level.spawn.x / tileSize), y = int(level.spawn.y / tileSize);
            if (level.spawn.x < 0 || level.spawn.y < 0 || x >= mapWidth || y >= mapHeight)
                issue(true, name, "Player spawn is outside the map");
            else if (level.at(x, y) == Tile::Solid || level.at(x, y) == Tile::Spike)
                issue(false, name, "Player spawn overlaps solid terrain or spikes");
            eventCheck(map, name);
        } catch (const std::exception& e) {
            issue(true, name, e.what());
        }
    }
    try {
        if (r.contains("database.echo")) {
            auto db = loadDatabase(r.text("database.echo"));
            auto duplicates = [&](const auto& entries, const char* kind) {
                std::set<std::string> names;
                for (const auto& entry : entries)
                    if (!entry.name.empty() && !names.insert(entry.name.c_str()).second)
                        issue(false, "database.echo",
                              std::string("Duplicate ") + kind +
                                  " name: " + std::string(entry.name.c_str()));
            };
            duplicates(db.switches, "switch");
            duplicates(db.variables, "variable");
        }
    } catch (const std::exception& e) {
        issue(true, "database.echo", e.what());
    }
    return issues;
}
} // namespace echo
