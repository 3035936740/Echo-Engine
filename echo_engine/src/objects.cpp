#include "echo/focus.hpp"
#include "echo/platform_features.hpp"
#include "echo/objects.hpp"
#include "echo/animations.hpp"
#include "echo/collision.hpp"
#include "echo/simulation.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace echo {
namespace {
bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
bool liveHere(const WorldState& w, const ObjectState& o) {
    return o.active && o.alive && o.map == w.activeMap;
}
struct MotionResult {
    float fraction = 1;
    int support = -1;
    bool collided{};
};
MotionResult objectMotion(const WorldState& w, const ObjectState& o, Rect box, Vec2 motion,
                          int ignoredSupport = -1) {
    MotionResult result;
    if (motion.x > 0 && box.x + box.w + motion.x > mapWidth * tileSize) {
        result.fraction = std::clamp((mapWidth * tileSize - box.x - box.w) / motion.x, 0.f, 1.f);
        result.collided = true;
    }
    if (motion.x < 0 && box.x + motion.x < 0) {
        result.fraction = std::clamp(box.x / -motion.x, 0.f, 1.f);
        result.collided = true;
    }
    auto test = [&](const StaticCollider& c, int support = -1) {
        if (motion.y == 0 &&
            (c.bounds.y >= box.y + box.h - .001f || c.bounds.y + c.bounds.h <= box.y + .001f))
            return;
        if (motion.x == 0 &&
            (c.bounds.x >= box.x + box.w - .001f || c.bounds.x + c.bounds.w <= box.x + .001f))
            return;
        float t;
        if (sweepCollider(box, motion, c, t) && t <= result.fraction) {
            result.fraction = t;
            result.support = support;
            result.collided = true;
        }
    };
    int x0 = std::max(0, int(std::floor(std::min(box.x, box.x + motion.x) / tileSize)));
    int x1 =
        std::min(mapWidth - 1, int(std::floor((std::max(box.x, box.x + motion.x) + box.w) / tileSize)));
    int y0 = std::max(0, int(std::floor(std::min(box.y, box.y + motion.y) / tileSize)));
    int y1 =
        std::min(mapHeight - 1, int(std::floor((std::max(box.y, box.y + motion.y) + box.h) / tileSize)));
    if ((o.mask & 1) && collisionAllowed(w, objectCollisionClass(o), CollisionClass::Terrain)) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                auto tile = w.level.at(x, y);
                if (tile == Tile::Solid ||
                    (motion.y > 0 && tile == Tile::OneWay && box.y + box.h <= y * tileSize + .05f)) {
                    StaticCollider c;
                    c.bounds = {x * tileSize, y * tileSize, tileSize, tileSize};
                    test(c);
                }
            }
        for (const auto& c : w.level.colliders)
            test(c);
        if (motion.y > 0) {
            for (const auto& f : w.level.features)
                if (featureSupports(f) && featureSupportId(f) != ignoredSupport &&
                    box.y + box.h <= f.bounds.y + .05f) {
                    StaticCollider c;
                    c.bounds = f.bounds;
                    test(c, featureSupportId(f));
                }
            for (const auto& p : w.level.platforms)
                if (p.id != ignoredSupport && box.y + box.h <= p.pos.y + .05f) {
                    StaticCollider c;
                    c.bounds = {p.pos.x, p.pos.y, p.base.w, p.base.h};
                    test(c, p.id);
                }
        }
    }
    if (o.mask & 1)
        for (const auto& e : w.level.events)
            if (e.solid && !e.erased &&
                collisionAllowed(w, objectCollisionClass(o), CollisionClass::Trigger)) {
                StaticCollider c;
                c.bounds = e.region;
                test(c);
            }
    for (const auto& other : w.objects.slots)
        if (&other != &o && liveHere(w, other) && other.kind != ObjectKind::Player &&
            !(ignoredSupport != -1 && other.ridesPlatforms && other.grounded &&
              other.standingPlatformId == ignoredSupport) &&
            other.solid && (o.mask & other.category) && (other.mask & o.category) &&
            collisionAllowed(w, objectCollisionClass(o), objectCollisionClass(other))) {
            StaticCollider c;
            c.bounds = objectBody(w, other);
            test(c);
        }
    return result;
}
bool canRide(const WorldState& w, const ObjectState& o) {
    return liveHere(w, o) && o.ridesPlatforms && o.kind != ObjectKind::Player &&
           o.kind != ObjectKind::Boss && (o.mask & 1) &&
           collisionAllowed(w, objectCollisionClass(o), CollisionClass::Terrain);
}
void detach(ObjectState& o) {
    o.standingPlatformId = -1;
    o.grounded = false;
}
void carryObject(WorldState& w, ObjectState& o, Vec2 delta) {
    if (delta.x == 0 && delta.y == 0)
        return;
    Rect body = objectBody(w, o);
    auto result = objectMotion(w, o, body, delta, o.standingPlatformId);
    auto move = [&](Vec2 d) {
        o.x += d.x;
        o.y += d.y;
        o.homeX += d.x;
        o.homeY += d.y;
    };
    move({delta.x * result.fraction, delta.y * result.fraction});
    if (result.fraction >= 1 - 1e-5f)
        return;
    if (delta.y > 0) {
        float horizontal = objectMotion(w, o, body, {delta.x, 0}, o.standingPlatformId).fraction;
        if (horizontal >= 1 - 1e-5f) {
            move({delta.x * (horizontal - result.fraction), 0});
            detach(o);
            o.vy = 0;
            return; // Leave rider on a floor, including diagonal descent.
        }
    }
    o.crushed = true;
    detach(o);
    o.vx = o.vy = 0;
    if (o.crushKills) {
        o.hp = 0;
        auto slot = std::size_t(&o - w.objects.slots.data());
        destroyObject(
            w, {std::uint16_t(slot), o.generation}); // bypass invulnerability; preserve tombstones.
    }
}
void moveAxis(WorldState& w, ObjectState& o, float delta, bool vertical) {
    if (delta == 0 && !vertical)
        return;
    float probe = delta == 0 ? .001f : delta;
    auto result = objectMotion(w, o, objectBody(w, o), {vertical ? 0.f : probe, vertical ? probe : 0.f});
    if (vertical)
        o.y += delta * result.fraction;
    else
        o.x += delta * result.fraction;
    if (result.collided) {
        if (vertical) {
            if (delta >= 0) {
                o.grounded = true;
                o.standingPlatformId = o.ridesPlatforms && !o.crushed ? result.support : -1;
            }
            o.vy = 0;
        } else {
            o.vx = 0;
            o.facing = -o.facing;
        }
    }
}
} // namespace
void prepareObjectPlatforms(WorldState& w) {
    for (auto& o : w.objects.slots) {
        if (!liveHere(w, o) || o.kind == ObjectKind::Player || o.kind == ObjectKind::Boss)
            continue;
        o.px = o.x;
        o.py = o.y;
        o.crushed = false;
        if (o.standingPlatformId == -1)
            continue;
        Rect support{};
        bool found = false;
        if (canRide(w, o)) {
            if (auto* f = supportFeature(w.level, o.standingPlatformId)) {
                support = f->bounds;
                found = true;
            }
            for (const auto& p : w.level.platforms)
                if (p.id == o.standingPlatformId) {
                    support = {p.pos.x, p.pos.y, p.base.w, p.base.h};
                    found = true;
                    break;
                }
        }
        Rect body = objectBody(w, o);
        if (!found || !o.grounded || std::abs(body.y + body.h - support.y) > .5f ||
            body.x + body.w <= support.x || body.x >= support.x + support.w)
            detach(o);
    }
}
bool objectPlatformRider(const WorldState& w, int id) {
    for (const auto& o : w.objects.slots)
        if (canRide(w, o) && o.grounded && o.standingPlatformId == id)
            return true;
    return false;
}
void carryPlatformObjects(WorldState& w, int id, Vec2 delta) {
    for (auto& o : w.objects.slots)
        if (canRide(w, o) && o.grounded && o.standingPlatformId == id)
            carryObject(w, o, delta);
}
const ObjectTemplate* findObjectTemplate(const ObjectLibrary& library, std::uint64_t id) {
    for (const auto& t : library.templates)
        if (t.id == id)
            return &t;
    return nullptr;
}
void validateObjects(const ObjectLibrary& library) {
    if (library.templates.size() > 256)
        throw std::runtime_error("Object template capacity is 256");
    for (std::size_t i = 0; i < library.templates.size(); ++i) {
        const auto& t = library.templates[i];
        if (!t.id || t.id == UINT64_MAX || t.name.empty() || int(t.kind) > 6 || int(t.behavior) > 5 ||
            !std::isfinite(t.width) || !std::isfinite(t.height) || t.width <= 0 || t.height <= 0 ||
            t.width > 2048 || t.height > 2048 || !std::isfinite(t.speed) || t.speed < 0 ||
            t.speed > 10000 || !std::isfinite(t.patrolRange) || t.patrolRange < 0 ||
            t.patrolRange > 10000 || !std::isfinite(t.detectRange) || t.detectRange < 0 ||
            t.detectRange > 10000 || !std::isfinite(t.attackInterval) || t.attackInterval < .05f ||
            t.attackInterval > 600 || t.hp < 1 || t.hp > 1000000 || t.damage < 0 || t.damage > 1000000 ||
            t.pickupVariable < -1 || t.pickupVariable >= 64 || t.pickupValue < -1000000 ||
            t.pickupValue > 1000000 || !t.category)
            throw std::runtime_error("Invalid object template");
        for (std::size_t j = 0; j < i; ++j)
            if (library.templates[j].id == t.id)
                throw std::runtime_error("Duplicate object template ID");
    }
}
ObjectLibrary loadObjects(const std::string& text) {
    if (text.size() > 256 * 1024)
        throw std::runtime_error("Object library exceeds 256 KiB");
    std::istringstream in(text);
    std::string header;
    int count;
    ObjectLibrary library;
    if (!(in >> header >> count) ||
        (header != "ECHO_OBJECTS_1" && header != "ECHO_OBJECTS_2" && header != "ECHO_OBJECTS_3") ||
        count < 0 || count > 256)
        throw std::runtime_error("Invalid object library header");
    if (header != "ECHO_OBJECTS_1" && (!(in >> library.nextId) || !library.nextId))
        throw std::runtime_error("Invalid template ID allocator");
    for (int i = 0; i < count; ++i) {
        ObjectTemplate t;
        std::string name, sprite;
        int kind, behavior, gravity, solid, category, mask, faction;
        if (!(in >> t.id >> std::quoted(name) >> std::quoted(sprite) >> kind >> behavior >> t.width >>
              t.height >> t.speed >> t.patrolRange >> t.detectRange >> t.attackInterval >> t.hp >>
              t.damage >> t.pickupVariable >> t.pickupValue >> category >> mask >> faction >> gravity >>
              solid) ||
            name.size() >= 64 || sprite.size() >= 256 || kind < 0 || kind > 6 || behavior < 0 ||
            behavior > 5 || category < 1 || category > 65535 || mask < 0 || mask > 65535 ||
            faction < 0 || faction > 65535 || (gravity != 0 && gravity != 1) ||
            (solid != 0 && solid != 1))
            throw std::runtime_error("Invalid object template row");
        if (header == "ECHO_OBJECTS_3") {
            int ride = -1, crush = -1;
            if (!(in >> ride >> crush) || ride < 0 || ride > 1 || crush < 0 || crush > 1)
                throw std::runtime_error("Invalid platform rider flags");
            t.ridesPlatforms = ride;
            t.crushKills = crush;
        } else
            t.ridesPlatforms = gravity != 0; // retain old anchored non-gravity presets.
        t.name.set(name);
        t.sprite.set(sprite);
        t.kind = ObjectKind(kind);
        t.behavior = ObjectBehavior(behavior);
        t.category = std::uint16_t(category);
        t.mask = std::uint16_t(mask);
        t.faction = std::uint16_t(faction);
        t.gravity = gravity != 0;
        t.solid = solid != 0;
        if (header == "ECHO_OBJECTS_1")
            library.nextId = std::max(library.nextId, t.id + 1);
        else if (t.id >= library.nextId)
            throw std::runtime_error("Template ID allocator must exceed existing IDs");
        library.templates.push_back(t);
    }
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Trailing object library data");
    validateObjects(library);
    return library;
}
std::uint64_t allocateObjectTemplateId(ObjectLibrary& library) {
    for (const auto& t : library.templates) {
        if (t.id >= UINT64_MAX - 1)
            throw std::runtime_error("Template ID space exhausted");
        library.nextId = std::max(library.nextId, t.id + 1);
    }
    if (!library.nextId || library.nextId >= UINT64_MAX - 1)
        throw std::runtime_error("Template ID space exhausted");
    return library.nextId++;
}
std::string saveObjects(const ObjectLibrary& library) {
    validateObjects(library);
    std::ostringstream out;
    auto next = library.nextId;
    for (const auto& t : library.templates)
        next = std::max(next, t.id + 1);
    out << "ECHO_OBJECTS_3 " << library.templates.size() << ' ' << next << "\n" << std::setprecision(9);
    for (const auto& t : library.templates)
        out << t.id << ' ' << std::quoted(t.name.c_str()) << ' ' << std::quoted(t.sprite.c_str()) << ' '
            << int(t.kind) << ' ' << int(t.behavior) << ' ' << t.width << ' ' << t.height << ' '
            << t.speed << ' ' << t.patrolRange << ' ' << t.detectRange << ' ' << t.attackInterval << ' '
            << t.hp << ' ' << t.damage << ' ' << t.pickupVariable << ' ' << t.pickupValue << ' '
            << t.category << ' ' << t.mask << ' ' << t.faction << ' ' << t.gravity << ' ' << t.solid
            << ' ' << t.ridesPlatforms << ' ' << t.crushKills << "\n";
    return out.str();
}
ObjectTemplate objectPreset(std::uint64_t id, int preset) {
    ObjectTemplate t;
    t.id = id;
    switch (preset) {
    case 0:
        t.name.set("巡逻敌人");
        break;
    case 1:
        t.name.set("追击敌人");
        t.behavior = ObjectBehavior::Chase;
        break;
    case 2:
        t.name.set("弹幕炮台");
        t.behavior = ObjectBehavior::Turret;
        t.gravity = false;
        t.ridesPlatforms = false;
        t.speed = 0;
        t.width = t.height = 32;
        break;
    case 3:
        t.name.set("NPC / 对话角色");
        t.kind = ObjectKind::NPC;
        t.behavior = ObjectBehavior::Idle;
        t.damage = 0;
        t.faction = 0;
        break;
    case 4:
        t.name.set("门 / 机关");
        t.kind = ObjectKind::Mechanism;
        t.behavior = ObjectBehavior::Door;
        t.solid = true;
        t.gravity = false;
        t.ridesPlatforms = false;
        t.damage = 0;
        t.category = 1;
        t.mask = 65535;
        t.faction = 0;
        t.width = 32;
        t.height = 64;
        break;
    case 5:
        t.name.set("拾取物");
        t.kind = ObjectKind::Pickup;
        t.behavior = ObjectBehavior::Collect;
        t.gravity = false;
        t.ridesPlatforms = false;
        t.damage = 0;
        t.category = 8;
        t.faction = 0;
        t.width = t.height = 24;
        t.pickupVariable = 0;
        break;
    case 6:
        t.name.set("装饰物");
        t.kind = ObjectKind::Decoration;
        t.behavior = ObjectBehavior::Idle;
        t.gravity = false;
        t.ridesPlatforms = false;
        t.damage = 0;
        t.mask = 0;
        t.faction = 0;
        break;
    case 7:
        t.name.set("玩家");
        t.kind = ObjectKind::Player;
        t.behavior = ObjectBehavior::Idle;
        t.category = 2;
        t.mask = 65535;
        t.faction = 1;
        t.width = playerWidth;
        t.height = playerHeight;
        t.hp = 1;
        t.speed = 180;
        break;
    case 8:
        t.name.set("Boss");
        t.kind = ObjectKind::Boss;
        t.behavior = ObjectBehavior::Idle;
        t.hp = 500;
        t.width = 48;
        t.height = 64;
        break;
    default:
        throw std::runtime_error("Unknown object preset");
    }
    return t;
}
ObjectHandle createObject(WorldState& w, const ObjectTemplate& t, Vec2 p, int map,
                          std::uint64_t placementId) {
    if (map < 0 || map >= 8 || !std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e7f ||
        std::abs(p.y) > 1e7f || !w.objects.nextId || w.objects.nextId >= UINT64_MAX - 1 ||
        !std::isfinite(t.width) || !std::isfinite(t.height) || t.width <= 0 || t.width > 2048 ||
        t.height <= 0 || t.height > 2048 || !std::isfinite(t.speed) || t.speed < 0 || t.speed > 10000 ||
        t.hp < 1 || t.damage < 0 || int(t.kind) > 6 || int(t.behavior) > 5) {
        ++w.objects.rejected;
        return {};
    }
    if (t.kind == ObjectKind::Player || t.kind == ObjectKind::Boss)
        for (const auto& o : w.objects.slots)
            if (o.active && o.alive && o.map == map && o.kind == t.kind) {
                ++w.objects.rejected;
                return {};
            }
    for (std::size_t i = 0; i < w.objects.slots.size(); ++i) {
        auto& o = w.objects.slots[i];
        // Authored tombstones remain reserved: re-entering a map must not resurrect a collected object.
        if (o.active || o.generation == UINT32_MAX)
            continue;
        auto generation = o.generation;
        o = {};
        o.generation = generation ? generation : 1;
        o.id = w.objects.nextId++;
        o.placementId = placementId;
        o.templateId = t.id;
        o.map = map;
        o.kind = t.kind;
        o.behavior = t.behavior;
        o.hp = o.maxHP = t.hp;
        o.damage = t.damage;
        o.x = o.px = o.homeX = p.x;
        o.y = o.py = o.homeY = p.y;
        o.width = t.width;
        o.height = t.height;
        o.speed = t.speed;
        o.patrolRange = t.patrolRange;
        o.detectRange = t.detectRange;
        o.attackInterval = t.attackInterval;
        o.category = t.category;
        o.mask = t.mask;
        o.faction = t.faction;
        o.gravity = t.gravity;
        o.solid = t.solid;
        o.ridesPlatforms = t.ridesPlatforms;
        o.crushKills = t.crushKills;
        o.pickupVariable = t.pickupVariable;
        o.pickupValue = t.pickupValue;
        o.active = true;
        o.alive = true;
        if (map == w.activeMap) {
            if (t.kind == ObjectKind::Player) {
                w.kino.x = p.x;
                w.kino.y = p.y;
                w.kino.hp = t.hp;
                w.kino.width = t.width;
                w.kino.height = t.height;
                w.jump.move_speed = t.speed;
            }
            if (t.kind == ObjectKind::Boss) {
                w.aster.x = p.x;
                w.aster.y = p.y;
                w.aster.hp = w.aster.maxHp = t.hp;
                w.aster.enabled = true;
                w.aster.width = t.width;
                w.aster.height = t.height;
            }
        }
        return {std::uint16_t(i), o.generation};
    }
    ++w.objects.rejected;
    return {};
}
ObjectState* resolveObject(WorldState& w, ObjectHandle h) {
    if (h.slot >= w.objects.slots.size())
        return nullptr;
    auto& o = w.objects.slots[h.slot];
    return o.active && o.alive && o.generation == h.generation ? &o : nullptr;
}
const ObjectState* resolveObject(const WorldState& w, ObjectHandle h) {
    if (h.slot >= w.objects.slots.size())
        return nullptr;
    const auto& o = w.objects.slots[h.slot];
    return o.active && o.alive && o.generation == h.generation ? &o : nullptr;
}
bool destroyObject(WorldState& w, ObjectHandle h) {
    auto* o = resolveObject(w, h);
    if (!o) {
        ++w.objects.invalidReferences;
        return false;
    }
    if (o->map == w.activeMap) {
        if (o->kind == ObjectKind::Player) {
            w.kino.hp = 0;
            w.state = TimeState::Hit;
            w.gameplayClockPaused = true;
        }
        if (o->kind == ObjectKind::Boss)
            w.aster.enabled = false;
    }
    o->alive = false;
    o->ai = ObjectAI::Dead;
    if (o->generation != UINT32_MAX)
        ++o->generation; // Exhausted slots retire; stale handles never alias.
    if (!o->placementId)
        o->active = false;
    return true;
}
void enterObjectMap(WorldState& w, const ObjectLibrary& library) {
    if (w.activeMap < 0 || w.activeMap >= 8)
        return;
    bool visited = w.objects.visited[w.activeMap];
    for (const auto& p : w.level.objects) {
        ObjectState* existing = nullptr;
        for (auto& o : w.objects.slots)
            if (o.active && o.map == w.activeMap && o.placementId == p.id) {
                existing = &o;
                break;
            }
        if (visited && p.persistent && existing)
            continue;
        if (existing) {
            existing->active = false;
            if (existing->generation != UINT32_MAX)
                ++existing->generation;
        }
        auto* t = findObjectTemplate(library, p.templateId);
        if (!t) {
            ++w.objects.invalidReferences;
            continue;
        }
        auto h = createObject(w, *t, {p.x, p.y}, w.activeMap, p.id);
        auto* o = resolveObject(w, h);
        if (!o)
            continue;
        o->persistent = p.persistent;
        o->eventId = p.eventId;
        if (p.overrides & OverrideSpeed)
            o->speed = p.speed;
        if (p.overrides & OverrideHP)
            o->hp = o->maxHP = p.hp;
        if (p.overrides & OverrideSize) {
            o->width = p.width;
            o->height = p.height;
        }
        if (p.overrides & OverrideDamage)
            o->damage = p.damage;
        if (o->kind == ObjectKind::Player) {
            w.kino.hp = o->hp;
            w.kino.width = o->width;
            w.kino.height = o->height;
            w.jump.move_speed = o->speed;
        }
        if (o->kind == ObjectKind::Boss) {
            w.aster.hp = o->hp;
            w.aster.maxHp = o->maxHP;
            w.aster.width = o->width;
            w.aster.height = o->height;
        }
    }
    w.objects.visited[w.activeMap] = true;
}
Rect objectBody(const WorldState& w, const ObjectState& o) {
    if (o.kind == ObjectKind::Player && o.map == w.activeMap)
        return kinoBody(w);
    if (o.kind == ObjectKind::Boss && o.map == w.activeMap)
        return asterBody(w);
    if (o.animation.collision.enabled)
        return animationBox(o.animation.collision, {o.x, o.y}, o.width, o.facing);
    return {o.x, o.y, o.width, o.height};
}
void damageObject(WorldState& w, ObjectHandle h, int amount, Vec2 knockback) {
    auto* o = resolveObject(w, h);
    if (!o ||
        (o->kind == ObjectKind::Player && o->map == w.activeMap ? w.kino.invulnTimer > 0
         : o->kind == ObjectKind::Boss && o->map == w.activeMap ? w.aster.invulnTimer > 0
                                                                : o->invuln > 0) ||
        amount <= 0 || !std::isfinite(knockback.x) || !std::isfinite(knockback.y))
        return;
    o->hp = std::max(0, o->hp - amount);
    o->invuln = .4f;
    o->vx = knockback.x;
    o->vy = knockback.y;
    o->knockback = .35f;
    if (o->kind == ObjectKind::Player && o->map == w.activeMap) {
        w.kino.hp = std::max(0, w.kino.hp - amount);
        w.kino.invulnTimer = .4f;
        w.kino.hitFlash = .2f;
        w.state = TimeState::Hit;
        w.gameplayClockPaused = true;
        o->hp = w.kino.hp;
    }
    if (o->kind == ObjectKind::Boss && o->map == w.activeMap) {
        w.aster.hp = std::max(0, w.aster.hp - amount);
        o->hp = w.aster.hp;
    }
    if (!o->hp)
        destroyObject(w, h);
}
ObjectImpact findFastObjectImpact(const WorldState& w, Rect from, Vec2 motion,
                                  const ObjectWorld* previous) {
    ObjectImpact result;
    for (std::size_t i = 0; i < w.objects.slots.size(); ++i) {
        const auto& o = w.objects.slots[i];
        if (!liveHere(w, o) || !collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o)) ||
            o.kind != ObjectKind::Enemy || o.invuln > 0 || !o.faction || o.faction == 1)
            continue;
        Rect body = objectBody(w, o), old = body;
        if (previous) {
            const auto& p = previous->slots[i];
            if (p.active && p.alive && p.id == o.id && p.generation == o.generation) {
                old = p.animation.collision.enabled
                          ? animationBox(p.animation.collision, {p.x, p.y}, p.width, p.facing)
                          : Rect{p.x, p.y, p.width, p.height};
            }
        }
        float entry;
        if (segmentRect(
                {from.x - old.x, from.y - old.y},
                {from.x + motion.x - body.x, from.y + motion.y - body.y},
                {-from.w, -from.h, std::max(old.w, body.w) + from.w, std::max(old.h, body.h) + from.h},
                &entry) &&
            entry < result.entry)
            result = {{std::uint16_t(i), o.generation}, entry};
    }
    return result;
}
bool fastImpactObjects(WorldState& w, Vec2 from, Vec2 to) {
    float distance = std::hypot(to.x - w.fast.A.x, to.y - w.fast.A.y);
    int damage = int(std::min(160.f, 40 + distance * .18f));
    for (std::size_t i = 0; i < w.objects.slots.size(); ++i) {
        auto& o = w.objects.slots[i];
        if (!liveHere(w, o) || !collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o)) ||
            o.kind != ObjectKind::Enemy || o.invuln > 0)
            continue;
        if (segmentRect(from, to,
                        {o.x - playerWidth, o.y - playerHeight, o.width + playerWidth,
                         o.height + playerHeight})) {
            ObjectHandle h{std::uint16_t(i), o.generation};
            damageObject(w, h, damage, {from.x < o.x ? 545.f : -545.f, -80});
            o.invuln = 6;
            o.knockback = .55f;
            return true;
        }
    }
    return false;
}
void stepObjects(WorldState& w, const ObjectLibrary&, float dt, bool prepared) {
    if (!prepared)
        prepareObjectPlatforms(w);
    Rect player = kinoBody(w);
    for (std::size_t i = 0; i < w.objects.slots.size(); ++i) {
        auto& o = w.objects.slots[i];
        if (!liveHere(w, o))
            continue;
        if (o.kind == ObjectKind::Player || o.kind == ObjectKind::Boss) {
            o.px = o.x;
            o.py = o.y;
            if (o.kind == ObjectKind::Player) {
                o.x = w.kino.x;
                o.y = w.kino.y;
                o.hp = w.kino.hp;
                o.vx = w.kino.vx;
                o.vy = w.kino.vy;
                o.invuln = w.kino.invulnTimer;
                o.grounded = w.kino.grounded;
                o.facing = w.kino.facing;
            } else {
                o.x = w.aster.x;
                o.y = w.aster.y;
                o.hp = w.aster.hp;
                o.invuln = w.aster.invulnTimer;
                o.facing = w.aster.walkDir;
            }
            continue;
        }
        o.animTime += dt;
        o.timer = std::max(0.f, o.timer - dt);
        o.invuln = std::max(0.f, o.invuln - dt);
        o.knockback = std::max(0.f, o.knockback - dt);
        o.springCooldown = std::max(0.f, o.springCooldown - dt);
        float distance = std::hypot(w.kino.x - o.x, w.kino.y - o.y);
        if (o.knockback <= 0) {
            o.vx = 0;
            switch (o.behavior) {
            case ObjectBehavior::Patrol:
                o.ai = ObjectAI::Patrol;
                if (o.x > o.homeX + o.patrolRange)
                    o.facing = -1;
                if (o.x < o.homeX - o.patrolRange)
                    o.facing = 1;
                o.vx = o.facing * o.speed;
                break;
            case ObjectBehavior::Chase:
                if (distance <= o.detectRange) {
                    o.ai = ObjectAI::Chase;
                    o.facing = w.kino.x < o.x ? -1 : 1;
                    o.vx = o.facing * o.speed;
                } else if (std::abs(o.x - o.homeX) > 2) {
                    o.ai = ObjectAI::Return;
                    o.vx = (o.homeX < o.x ? -1 : 1) * o.speed;
                } else
                    o.ai = ObjectAI::Idle;
                break;
            case ObjectBehavior::Turret:
                o.ai = ObjectAI::Attack;
                if (distance < o.detectRange && o.timer == 0) {
                    Bullet b;
                    b.x = b.px = o.x + o.width * .5f;
                    b.y = b.py = o.y + o.height * .5f;
                    float angle = std::atan2(w.kino.y + 32 - b.y, w.kino.x + 14 - b.x);
                    b.angle = angle;
                    b.vx = std::cos(angle) * 180;
                    b.vy = std::sin(angle) * 180;
                    spawnBullet(w, b);
                    o.timer = o.attackInterval;
                }
                break;
            default:
                o.ai = ObjectAI::Idle;
                break;
            }
        }
        float conveyor = 0;
        if (o.grounded && canRide(w, o))
            if (auto* f = supportFeature(w.level, o.standingPlatformId);
                f && f->kind == PlatformKind::Conveyor)
                conveyor = f->speed;
        if (o.gravity)
            o.vy = std::min(700.f, o.vy + w.jump.gravity * dt);
        Rect motionStart = objectBody(w, o);
        detach(o);
        moveAxis(w, o, o.vx * dt + conveyor * dt, false);
        moveAxis(w, o, o.vy * dt, true);
        if (canRide(w, o) && !o.crushed) {
            const PlatformFeature* caught = nullptr;
            float first = 2;
            Rect endBody = objectBody(w, o);
            for (const auto& f : w.level.features) {
                if (f.kind != PlatformKind::Lift || !featureSupports(f) ||
                    o.standingPlatformId == featureSupportId(f))
                    continue;
                float hit;
                Rect previous{f.previous.x, f.previous.y, f.bounds.w, f.bounds.h};
                if (movingSupportContact(motionStart, endBody, previous, f.bounds, hit) &&
                    (hit < first || (hit == first && caught && f.id < caught->id))) {
                    caught = &f;
                    first = hit;
                }
            }
            if (caught) {
                o.grounded = true;
                o.standingPlatformId = featureSupportId(*caught);
                o.vy = 0;
                carryObject(w, o,
                            {(caught->bounds.x - caught->previous.x) * (1 - first),
                             caught->bounds.y - (endBody.y + endBody.h)});
                if (!liveHere(w, o))
                    continue;
                auto body = objectBody(w, o);
                if (body.x + body.w <= caught->bounds.x || body.x >= caught->bounds.x + caught->bounds.w)
                    detach(o);
            }
        }
        if (o.grounded && canRide(w, o) && o.springCooldown <= 0)
            if (auto* f = supportFeature(w.level, o.standingPlatformId);
                f && f->kind == PlatformKind::Spring) {
                o.vy = -f->speed;
                o.springCooldown = .1f;
                detach(o);
            }
        o.x = std::clamp(o.x, 0.f, mapWidth * tileSize - o.width);
        bool inside = collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o)) &&
                      overlap(player, objectBody(w, o));
        if (inside && !o.entered && o.eventId >= 1 && o.eventId <= 32)
            w.events.local[o.eventId - 1].requested = true;
        o.entered = inside;
        if (inside && o.kind == ObjectKind::Pickup) {
            if (o.pickupVariable >= 0) {
                auto& value = w.events.variables[o.pickupVariable];
                long long sum = static_cast<long long>(value) + o.pickupValue;
                value = int(std::clamp(sum, static_cast<long long>(std::numeric_limits<int>::min()),
                                       static_cast<long long>(std::numeric_limits<int>::max())));
            }
            destroyObject(w, {std::uint16_t(i), o.generation});
        } else if (collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o)) &&
                   playerHurtRect(w, objectBody(w, o), hurtContact) && o.kind == ObjectKind::Enemy &&
                   o.faction != 1 && o.damage > 0 && w.kino.invulnTimer == 0 && w.rewindExitGrace == 0) {
            w.kino.hp = std::max(0, w.kino.hp - o.damage);
            w.kino.hitFlash = .2f;
            w.state = TimeState::Hit;
            w.gameplayClockPaused = true;
        }
        if (o.y > mapHeight * tileSize + o.height)
            destroyObject(w, {std::uint16_t(i), o.generation});
    }
}
} // namespace echo
