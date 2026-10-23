#include "echo/focus.hpp"
#include "echo/collision.hpp"
#include "echo/animations.hpp"
#include "echo/audio_state.hpp"
#include "echo/objects.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
namespace echo {
namespace {
void applyStrike(WorldState&, AnimationPlayback&, int);
bool finite(float x) {
    return std::isfinite(x);
}
void boxValid(AnimationBox b) {
    if (!finite(b.x) || !finite(b.y) || !finite(b.w) || !finite(b.h) || std::abs(b.x) > 8192 ||
        std::abs(b.y) > 8192 || b.w < 0 || b.h < 0 || b.w > 8192 || b.h > 8192 ||
        (b.enabled && (b.w <= 0 || b.h <= 0)))
        throw std::runtime_error("Invalid animation box (logical px)");
}
void writeBox(std::ostream& out, AnimationBox b) {
    out << ' ' << b.enabled << ' ' << b.x << ' ' << b.y << ' ' << b.w << ' ' << b.h;
}
void readBox(std::istream& in, AnimationBox& b) {
    int enabled;
    if (!(in >> enabled >> b.x >> b.y >> b.w >> b.h) || (enabled != 0 && enabled != 1))
        throw std::runtime_error("Invalid animation box");
    b.enabled = enabled != 0;
}
void enterFrame(WorldState& w, AnimationPlayback& p, const AnimationClip& c) {
    const auto& f = c.frames[p.frame];
    p.collision = f.collision;
    p.hurt = f.hurt;
    p.attack = f.attack;
    p.damage = f.damage;
    if (!f.attack.enabled)
        p.strike = 0;
    else if (!p.strike) {
        if (!w.animations.nextStrike || w.animations.nextStrike == UINT64_MAX) {
            ++w.animations.capacityRejected;
            p.attack.enabled = false;
        } else
            p.strike = w.animations.nextStrike++;
    }
    // Frame entry is consumed exactly once, including the first frame and each loop.
    if (f.sound >= 0)
        playSound(w, f.sound);
    if (f.variable >= 0) {
        auto& value = w.events.variables[f.variable];
        value = int(std::clamp(static_cast<long long>(value) + f.delta,
                               static_cast<long long>(INT32_MIN), static_cast<long long>(INT32_MAX)));
    }
    p.started = true;
}
bool intersects(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
std::uint64_t selection(const AnimationLibrary& library, std::uint64_t templateId, int state) {
    for (const auto& binding : library.bindings)
        if (binding.templateId == templateId)
            return binding.clips[std::clamp(state, 0, 5)];
    return 0;
}
std::uint64_t mix(std::uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
bool receipt(WorldState& w, std::uint64_t strike, std::uint64_t target) {
    auto slot = std::size_t(mix(strike ^ mix(target))) & 1023;
    for (int probe = 0; probe < 1024; ++probe, slot = (slot + 1) & 1023) {
        auto& hit = w.animations.hits[slot];
        if (hit.strike == strike && hit.target == target)
            return false;
        if (!hit.strike) {
            hit = {strike, target};
            return true;
        }
    }
    // Reject damage before applying it if its receipt cannot be retained.
    ++w.animations.capacityRejected;
    return false;
}
void pruneHits(WorldState& w) {
    std::array<std::uint64_t, 512> active{};
    auto add = [&](const AnimationPlayback& p) {
        if (!p.strike || !p.attack.enabled)
            return;
        auto slot = std::size_t(mix(p.strike)) & 511;
        while (active[slot] && active[slot] != p.strike)
            slot = (slot + 1) & 511;
        active[slot] = p.strike;
    };
    add(w.kino.animation);
    if (w.aster.enabled)
        add(w.aster.animation);
    for (const auto& o : w.objects.slots)
        if (o.active && o.alive && o.map == w.activeMap && o.kind != ObjectKind::Player &&
            o.kind != ObjectKind::Boss)
            add(o.animation);
    auto previous = w.animations.hits;
    w.animations.hits = {};
    for (const auto& hit : previous) {
        if (!hit.strike)
            continue;
        auto slot = std::size_t(mix(hit.strike)) & 511;
        while (active[slot] && active[slot] != hit.strike)
            slot = (slot + 1) & 511;
        if (active[slot])
            receipt(w, hit.strike, hit.target);
    }
}
} // namespace
const AnimationClip* findAnimation(const AnimationLibrary& library, std::uint64_t id) {
    for (const auto& c : library.clips)
        if (c.id == id)
            return &c;
    return nullptr;
}
void validateAnimations(const AnimationLibrary& library) {
    if (!library.nextId || library.clips.size() > 256 || library.bindings.size() > 256)
        throw std::runtime_error("Animation library capacity is 256");
    std::set<std::uint64_t> ids, bindings;
    std::size_t total = 0;
    for (const auto& c : library.clips) {
        if (!c.id || c.id == UINT64_MAX || !ids.insert(c.id).second || c.name.empty() ||
            c.sprite.empty() || c.frameWidth < 1 || c.frameWidth > 4096 || c.frameHeight < 1 ||
            c.frameHeight > 4096 || !finite(c.scale) || c.scale <= 0 || c.scale > 64 ||
            !finite(c.anchorX) || !finite(c.anchorY) || std::abs(c.anchorX) > 4096 ||
            std::abs(c.anchorY) > 4096 || c.frames.empty() || c.frames.size() > 128)
            throw std::runtime_error("Invalid animation clip");
        total += c.frames.size();
        for (const auto& f : c.frames) {
            if (f.column < 0 || f.row < 0 || f.column > 8191 || f.row > 8191 || !finite(f.seconds) ||
                f.seconds < 1.f / 240 || f.seconds > 60 || f.sound < -1 || f.sound >= 32 ||
                f.damage < 0 || f.damage > 1000000 || f.variable < -1 || f.variable >= 64 ||
                std::abs(static_cast<long long>(f.delta)) > 1000000)
                throw std::runtime_error("Invalid animation frame");
            boxValid(f.collision);
            boxValid(f.hurt);
            boxValid(f.attack);
            if (f.collision.w > 2048 || f.collision.h > 2048)
                throw std::runtime_error("Animation collision box exceeds 2048 logical px");
        }
    }
    if (total > 8192)
        throw std::runtime_error("Animation total frame capacity is 8192");
    auto check = [&](std::uint64_t id) {
        if (id && !ids.contains(id))
            throw std::runtime_error("Animation binding references a missing clip");
    };
    for (auto id : library.player)
        check(id);
    for (auto id : library.boss)
        check(id);
    for (const auto& b : library.bindings) {
        if (!b.templateId || !bindings.insert(b.templateId).second)
            throw std::runtime_error("Duplicate/invalid animation template binding");
        for (auto id : b.clips)
            check(id);
    }
}
std::uint64_t allocateAnimationId(AnimationLibrary& library) {
    for (const auto& c : library.clips)
        library.nextId = std::max(library.nextId, c.id + 1);
    if (!library.nextId || library.nextId >= UINT64_MAX - 1)
        throw std::runtime_error("Animation ID space exhausted");
    return library.nextId++;
}
std::string saveAnimations(const AnimationLibrary& library) {
    validateAnimations(library);
    auto next = library.nextId;
    for (const auto& c : library.clips)
        next = std::max(next, c.id + 1);
    std::ostringstream out;
    out << "ECHO_ANIMATIONS_1 " << next << ' ' << library.clips.size() << ' ' << library.bindings.size()
        << '\n'
        << std::setprecision(9);
    for (auto id : library.player)
        out << id << ' ';
    for (auto id : library.boss)
        out << id << ' ';
    out << '\n';
    for (const auto& c : library.clips) {
        out << c.id << ' ' << std::quoted(c.name.c_str()) << ' ' << std::quoted(c.sprite.c_str()) << ' '
            << c.frameWidth << ' ' << c.frameHeight << ' ' << c.scale << ' ' << c.anchorX << ' '
            << c.anchorY << ' ' << c.loop << ' ' << c.flip << ' ' << c.frames.size() << '\n';
        for (const auto& f : c.frames) {
            out << f.column << ' ' << f.row << ' ' << f.seconds;
            writeBox(out, f.collision);
            writeBox(out, f.hurt);
            writeBox(out, f.attack);
            out << ' ' << f.sound << ' ' << f.damage << ' ' << f.variable << ' ' << f.delta << '\n';
        }
    }
    for (const auto& b : library.bindings) {
        out << b.templateId;
        for (auto id : b.clips)
            out << ' ' << id;
        out << '\n';
    }
    return out.str();
}
AnimationLibrary loadAnimations(const std::string& data) {
    if (data.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Animations exceed 4 MiB");
    std::istringstream in(data);
    std::string header;
    AnimationLibrary library;
    int count, bindings;
    if (!(in >> header >> library.nextId >> count >> bindings) || header != "ECHO_ANIMATIONS_1" ||
        !library.nextId || count < 0 || count > 256 || bindings < 0 || bindings > 256)
        throw std::runtime_error("Invalid animation library header");
    for (auto& id : library.player)
        if (!(in >> id))
            throw std::runtime_error("Invalid player binding");
    for (auto& id : library.boss)
        if (!(in >> id))
            throw std::runtime_error("Invalid boss binding");
    std::size_t total = 0;
    for (int i = 0; i < count; ++i) {
        AnimationClip c;
        std::string name, sprite;
        int loop, flip, frames;
        if (!(in >> c.id >> std::quoted(name) >> std::quoted(sprite) >> c.frameWidth >> c.frameHeight >>
              c.scale >> c.anchorX >> c.anchorY >> loop >> flip >> frames) ||
            name.size() >= 64 || sprite.size() >= 256 || (loop != 0 && loop != 1) ||
            (flip != 0 && flip != 1) || frames < 1 || frames > 128 || c.id >= library.nextId)
            throw std::runtime_error("Invalid animation clip row");
        c.name.set(name);
        c.sprite.set(sprite);
        c.loop = loop != 0;
        c.flip = flip != 0;
        total += frames;
        if (total > 8192)
            throw std::runtime_error("Animation total frame capacity is 8192");
        for (int j = 0; j < frames; ++j) {
            AnimationFrame f;
            if (!(in >> f.column >> f.row >> f.seconds))
                throw std::runtime_error("Invalid frame row");
            readBox(in, f.collision);
            readBox(in, f.hurt);
            readBox(in, f.attack);
            if (!(in >> f.sound >> f.damage >> f.variable >> f.delta))
                throw std::runtime_error("Invalid frame marker");
            c.frames.push_back(f);
        }
        library.clips.push_back(std::move(c));
    }
    for (int i = 0; i < bindings; ++i) {
        AnimationBinding b;
        if (!(in >> b.templateId))
            throw std::runtime_error("Invalid template animation binding");
        for (auto& id : b.clips)
            if (!(in >> id))
                throw std::runtime_error("Invalid state binding");
        library.bindings.push_back(b);
    }
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Trailing animation data");
    validateAnimations(library);
    return library;
}
Rect animationBox(AnimationBox b, Vec2 p, float width, int facing) {
    return {p.x + (facing < 0 ? width - b.x - b.w : b.x), p.y + b.y, b.w, b.h};
}
Rect kinoBody(const WorldState& w) {
    if (w.kino.animation.collision.enabled)
        return animationBox(w.kino.animation.collision, {w.kino.x, w.kino.y}, w.kino.width,
                            w.kino.facing);
    return {w.kino.x, w.kino.y, w.kino.width, w.kino.height};
}
Rect kinoHurt(const WorldState& w) {
    Rect result{};
    bool any = false;
    auto count = w.hurt.source == HurtSource::Custom ? w.hurt.count : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto region = effectiveHurt(w, i);
        if (!region.enabled || !region.mask || (region.focusOnly && !w.kino.focusActive))
            continue;
        auto r = hurtBounds(w.kino, w.hurt, region);
        if (!any) {
            result = r;
            any = true;
        } else {
            float right = std::max(result.x + result.w, r.x + r.w),
                  bottom = std::max(result.y + result.h, r.y + r.h);
            result.x = std::min(result.x, r.x);
            result.y = std::min(result.y, r.y);
            result.w = right - result.x;
            result.h = bottom - result.y;
        }
    }
    return result;
}
Rect asterBody(const WorldState& w) {
    if (w.aster.animation.collision.enabled)
        return animationBox(w.aster.animation.collision, {w.aster.x, w.aster.y}, w.aster.width,
                            w.aster.walkDir);
    return {w.aster.x, w.aster.y, w.aster.width, w.aster.height};
}
void advanceAnimation(WorldState& w, AnimationPlayback& p, const AnimationLibrary& library,
                      std::uint64_t automatic, float dt, int actor) {
    if (!finite(dt) || dt < 0 || dt > 1)
        throw std::runtime_error("Animation dt must be 0..1 seconds");
    if (p.finished && p.requested == p.clip)
        p.requested = 0;
    auto selected = p.requested ? p.requested : automatic;
    auto* c = selected ? findAnimation(library, selected) : nullptr;
    if (!c) {
        if (selected)
            ++w.animations.invalidClips;
        p = {};
        return;
    }
    if (p.clip != selected || p.frame >= c->frames.size()) {
        auto requested = p.requested;
        p = {};
        p.clip = selected;
        p.requested = requested;
    }
    if (!p.started)
        enterFrame(w, p, *c);
    if (actor != -1)
        applyStrike(w, p, actor);
    if (p.finished || dt <= 0)
        return;
    p.elapsed += dt;
    for (int budget = 0; budget < 256 && p.elapsed + 1e-7f >= c->frames[p.frame].seconds; ++budget) {
        p.elapsed = std::max(0.f, p.elapsed - c->frames[p.frame].seconds);
        if (p.frame + 1 == c->frames.size()) {
            if (!c->loop || p.loops == UINT32_MAX) {
                p.finished = true;
                p.elapsed = 0;
                p.attack.enabled = false;
                p.strike = 0;
                return;
            }
            ++p.loops;
            p.frame = 0;
            p.strike = 0;
        } else
            ++p.frame;
        enterFrame(w, p, *c);
        if (actor != -1)
            applyStrike(w, p, actor);
    }
}
bool requestAnimation(WorldState& w, const AnimationLibrary& library, ObjectHandle handle,
                      std::uint64_t clip) {
    auto* o = resolveObject(w, handle);
    if (!o || (clip && !findAnimation(library, clip))) {
        ++w.animations.invalidClips;
        return false;
    }
    AnimationPlayback* p = &o->animation;
    if (o->map == w.activeMap && o->kind == ObjectKind::Player)
        p = &w.kino.animation;
    if (o->map == w.activeMap && o->kind == ObjectKind::Boss)
        p = &w.aster.animation;
    *p = {};
    p->requested = clip;
    return true;
}
namespace {
void applyStrike(WorldState& w, AnimationPlayback& p, int actor) {
    if (!p.attack.enabled || !p.strike || p.damage <= 0 || w.state == TimeState::Hit)
        return;
    Vec2 position;
    float width;
    int facing;
    std::uint16_t faction;
    if (actor == -2) {
        position = {w.kino.x, w.kino.y};
        width = w.kino.width;
        facing = w.kino.facing;
        faction = 1;
    } else if (actor == -3) {
        if (!w.aster.enabled || w.aster.hp <= 0)
            return;
        position = {w.aster.x, w.aster.y};
        width = w.aster.width;
        facing = w.aster.walkDir;
        faction = 2;
    } else if (actor >= 0 && actor < maxObjects) {
        const auto& o = w.objects.slots[actor];
        if (!o.active || !o.alive || o.map != w.activeMap)
            return;
        position = {o.x, o.y};
        width = o.width;
        facing = o.facing;
        faction = o.faction;
    } else
        return;
    if (!faction)
        return;
    Rect attack = animationBox(p.attack, position, width, facing);
    if (collisionAllowed(w, CollisionClass::Attack, CollisionClass::Player) && faction != 1 &&
        w.kino.hp > 0 && w.kino.invulnTimer <= 0 && w.rewindExitGrace <= 0 &&
        playerHurtRect(w, attack, hurtMelee) && receipt(w, p.strike, UINT64_MAX)) {
        w.kino.hp = std::max(0, w.kino.hp - p.damage);
        w.kino.invulnTimer = .4f;
        w.kino.hitFlash = .2f;
        w.state = TimeState::Hit;
        w.gameplayClockPaused = true;
    }
    Rect bossHurt = w.aster.animation.hurt.enabled
                        ? animationBox(w.aster.animation.hurt, {w.aster.x, w.aster.y}, w.aster.width,
                                       w.aster.walkDir)
                        : asterBody(w);
    if (collisionAllowed(w, CollisionClass::Attack, CollisionClass::Enemy) && faction != 2 &&
        w.aster.enabled && w.aster.hp > 0 && w.aster.invulnTimer <= 0 && intersects(attack, bossHurt) &&
        receipt(w, p.strike, UINT64_MAX - 1)) {
        w.aster.hp = std::max(0, w.aster.hp - p.damage);
        w.aster.invulnTimer = .4f;
    }
    for (int i = 0; i < maxObjects; ++i) {
        auto& o = w.objects.slots[i];
        if (i == actor || !o.active || !o.alive || o.map != w.activeMap ||
            o.kind == ObjectKind::Player || o.kind == ObjectKind::Boss || !o.faction ||
            o.faction == faction || o.invuln > 0 ||
            !collisionAllowed(w, CollisionClass::Attack, objectCollisionClass(o)))
            continue;
        Rect hurt = o.animation.hurt.enabled
                        ? animationBox(o.animation.hurt, {o.x, o.y}, o.width, o.facing)
                        : objectBody(w, o);
        if (intersects(attack, hurt) && receipt(w, p.strike, o.id))
            damageObject(w, {std::uint16_t(i), o.generation}, p.damage, {facing * 140.f, -60});
    }
}
} // namespace
void stepAnimations(WorldState& w, const AnimationLibrary& library, float dt, bool combat) {
    std::uint64_t player = library.player[int(w.kino.animState)],
                  boss = library.boss[int(w.aster.animState)];
    for (const auto& o : w.objects.slots) {
        if (!o.active || !o.alive || o.map != w.activeMap)
            continue;
        if (o.kind == ObjectKind::Player) {
            auto id = selection(library, o.templateId, int(w.kino.animState));
            if (id)
                player = id;
        } else if (o.kind == ObjectKind::Boss) {
            auto id = selection(library, o.templateId, int(w.aster.animState));
            if (id)
                boss = id;
        }
    }
    advanceAnimation(w, w.kino.animation, library, player, dt, combat ? -2 : -1);
    if (w.aster.enabled)
        advanceAnimation(w, w.aster.animation, library, boss, dt, combat ? -3 : -1);
    for (int i = 0; i < maxObjects; ++i) {
        auto& o = w.objects.slots[i];
        if (!o.active || !o.alive || o.map != w.activeMap || o.kind == ObjectKind::Player ||
            o.kind == ObjectKind::Boss)
            continue;
        advanceAnimation(w, o.animation, library, selection(library, o.templateId, int(o.ai)), dt,
                         combat ? i : -1);
    }
    pruneHits(w);
}
} // namespace echo
