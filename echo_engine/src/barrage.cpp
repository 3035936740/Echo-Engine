#include "echo/barrage.hpp"
#include "echo/animations.hpp"
#include "echo/objects.hpp"
#include "echo/simulation.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
namespace echo {
namespace {
constexpr float radians = std::numbers::pi_v<float> / 180;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool range(float v, float lo, float hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}
std::uint32_t random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
Vec2 playerTarget(const WorldState& w) {
    auto r = kinoHurt(w);
    return {r.x + r.w * .5f, r.y + r.h * .5f};
}
bool origin(const WorldState& w, const BarrageEmitter& e, Vec2& p) {
    p = e.position;
    if (e.origin == BarrageOrigin::Player) {
        p.x += w.kino.x;
        p.y += w.kino.y;
    } else if (e.origin == BarrageOrigin::Boss) {
        if (!w.aster.enabled || w.aster.hp <= 0)
            return false;
        p.x += w.aster.x;
        p.y += w.aster.y;
    } else if (e.origin == BarrageOrigin::Object) {
        for (const auto& o : w.objects.slots) {
            if (o.active && o.alive && o.map == w.activeMap && o.placementId == e.object) {
                p.x += o.x;
                p.y += o.y;
                return true;
            }
        }
        return false;
    }
    return true;
}
} // namespace
bool barrageEditorOrigin(const LevelState& level, const BarrageEmitter& e, Vec2& p) {
    p = e.position;
    Vec2 anchor{};
    if (e.origin == BarrageOrigin::Player)
        anchor = level.spawn;
    else if (e.origin == BarrageOrigin::Boss)
        anchor = level.bossSpawn;
    else if (e.origin == BarrageOrigin::Object) {
        auto object = std::find_if(level.objects.begin(), level.objects.end(),
                                   [&](const auto& o) { return o.id == e.object; });
        if (object == level.objects.end())
            return false;
        anchor = {object->x, object->y};
    }
    p.x += anchor.x;
    p.y += anchor.y;
    return true;
}
bool queryBarrageTarget(const WorldState& w, const BarrageEmitter& e, Vec2 from,
                        BarrageTargetResult& result, std::uint32_t* candidates) {
    result = {};
    if (e.target == BarrageTarget::Point) {
        result.position = e.targetPoint;
        return true;
    }
    if (e.target == BarrageTarget::Player) {
        if (w.kino.hp <= 0)
            return false;
        result.position = playerTarget(w);
        return true;
    }
    if (e.target == BarrageTarget::Boss) {
        if (!w.aster.enabled || w.aster.hp <= 0)
            return false;
        auto body = asterBody(w);
        result.position = {body.x + body.w * .5f, body.y + body.h * .5f};
        return true;
    }
    if (e.target != BarrageTarget::Object && e.target != BarrageTarget::NearestObject)
        return false;
    double best = e.targetRange > 0 ? double(e.targetRange) * e.targetRange
                                    : std::numeric_limits<double>::infinity();
    bool found = false;
    for (std::size_t i = 0; i < w.objects.slots.size(); ++i) {
        const auto& o = w.objects.slots[i];
        if (candidates)
            ++*candidates;
        if (!o.active || !o.alive || o.map != w.activeMap ||
            (o.kind == ObjectKind::Player && w.kino.hp <= 0) ||
            (o.kind == ObjectKind::Boss && (!w.aster.enabled || w.aster.hp <= 0)) ||
            (e.excludeParent && e.origin == BarrageOrigin::Object && o.placementId == e.object) ||
            (e.factionFilter == BarrageFactionFilter::Only && o.faction != e.targetFaction) ||
            (e.factionFilter == BarrageFactionFilter::Except && o.faction == e.targetFaction))
            continue;
        if (e.target == BarrageTarget::Object && o.placementId != e.targetObject)
            continue;
        auto body = objectBody(w, o);
        Vec2 point{body.x + body.w * .5f, body.y + body.h * .5f};
        double dx = double(point.x) - from.x, dy = double(point.y) - from.y;
        double distance = dx * dx + dy * dy;
        if (e.target == BarrageTarget::NearestObject &&
            (distance > best || (found && distance == best && o.id >= result.id)))
            continue;
        found = true;
        best = distance;
        result = {point, {std::uint16_t(i), o.generation}, o.id};
        if (e.target == BarrageTarget::Object)
            break;
    }
    return found;
}
const BarragePattern* findBarragePattern(const BarrageLibrary& library, std::uint64_t id) {
    auto it = std::find_if(library.patterns.begin(), library.patterns.end(),
                           [&](const auto& p) { return p.id == id; });
    return it == library.patterns.end() ? nullptr : &*it;
}
void validateBarrage(const BarrageLibrary& l, int maps) {
    require(l.patterns.size() <= 128 && l.emitters.size() <= maxBarrageEmitters,
            "Barrage library capacity exceeded");
    require(l.nextId && int(l.overflow) <= 2 && maps >= 1 && maps <= 9, "Invalid barrage configuration");
    std::unordered_set<std::uint64_t> ids;
    auto identity = [&](std::uint64_t id, const Text<64>& name) {
        require(id && id < l.nextId && ids.insert(id).second && !name.empty() && name.data.back() == 0,
                "Invalid / duplicate barrage ID or name");
    };
    for (const auto& p : l.patterns) {
        identity(p.id, p.name);
        require(int(p.shape) <= 3 && int(p.aim) <= 2 && p.count >= 1 && p.count <= 512 &&
                    p.damage >= 0 && p.damage <= 1000000 && p.palette <= 7 && p.terrain <= 2,
                "Invalid barrage shape / count / damage / terrain");
        require(range(p.speed, 0, 4000) && range(p.acceleration, -4000, 4000) &&
                    range(p.maxSpeed, 0, 4000) && range(p.lifetime, fixedDt, 60) &&
                    range(p.direction, -36000, 36000) && range(p.spread, 0, 360) &&
                    range(p.turnRate, -3600, 3600) && range(p.turnSeconds, 0, 60) &&
                    range(p.visualRadius, .25f, 128) && range(p.hitRadius, 0, 128) &&
                    range(p.wobble, 0, 512) && range(p.frequency, 0, 120) && range(p.growth, -10, 10) &&
                    range(p.rotation, -3600, 3600) && range(p.releaseSeconds, 0, 60) &&
                    range(p.jitter, 0, 180),
                "Barrage parameter outside finite range");
    }
    for (const auto& e : l.emitters) {
        identity(e.id, e.name);
        require(e.map >= 0 && e.map < maps && e.gateSwitch >= -1 && e.gateSwitch < 128 &&
                    int(e.origin) <= 3 && (e.origin != BarrageOrigin::Object || e.object) &&
                    int(e.target) <= 4 && int(e.factionFilter) <= 2 &&
                    (e.target != BarrageTarget::Object || e.targetObject) &&
                    range(e.targetPoint.x, -8192, 8192) && range(e.targetPoint.y, -8192, 8192) &&
                    range(e.targetRange, 0, 16384) && range(e.position.x, -8192, 8192) &&
                    range(e.position.y, -8192, 8192) && range(e.startDelay, 0, 3600) &&
                    range(e.loopDelay, fixedDt, 3600) && e.seed && !e.waves.empty() &&
                    e.waves.size() <= 32,
                "Invalid barrage emitter / wave capacity");
        for (const auto& wave : e.waves) {
            auto p = findBarragePattern(l, wave.pattern);
            require(p && wave.repeat >= 1 && wave.repeat <= 256 && range(wave.delay, 0, 3600) &&
                        range(wave.warning, p->damage > 0 ? .85f : 0.f, 60) &&
                        range(wave.interval, fixedDt, 60) && range(wave.angleStep, -3600, 3600),
                    "Invalid wave / missing pattern / warning must be at least 0.85 seconds");
        }
    }
}
std::uint64_t allocateBarrageId(BarrageLibrary& l) {
    for (const auto& p : l.patterns)
        l.nextId = std::max(l.nextId, p.id + 1);
    for (const auto& e : l.emitters)
        l.nextId = std::max(l.nextId, e.id + 1);
    if (!l.nextId || l.nextId == UINT64_MAX)
        throw std::runtime_error("Barrage ID exhausted");
    return l.nextId++;
}
BarragePattern barragePreset(std::uint64_t id, int preset) {
    BarragePattern p;
    p.id = id;
    const char* names[] = {"环形散射", "瞄准扇形", "旋转弧线", "波浪弹", "扩散阵型"};
    p.name.set(names[std::clamp(preset, 0, 4)]);
    if (preset == 1) {
        p.shape = BarrageShape::Fan;
        p.aim = BarrageAim::AtWarning;
        p.count = 7;
        p.spread = 60;
    }
    if (preset == 2) {
        p.turnRate = 60;
        p.turnSeconds = 2;
    }
    if (preset == 3) {
        p.shape = BarrageShape::Fan;
        p.spread = 90;
        p.wobble = 24;
        p.frequency = 5;
    }
    if (preset == 4) {
        p.shape = BarrageShape::Formation;
        p.rotation = 30;
        p.releaseSeconds = 2;
    }
    return p;
}
std::string saveBarrage(const BarrageLibrary& l) {
    validateBarrage(l);
    std::ostringstream s;
    s << std::setprecision(std::numeric_limits<float>::max_digits10);
    s << "ECHO_BARRAGE_2 " << l.nextId << ' ' << int(l.overflow) << ' ' << l.patterns.size() << ' '
      << l.emitters.size() << '\n';
    for (const auto& p : l.patterns)
        s << p.id << ' ' << std::quoted(p.name.c_str()) << ' ' << int(p.shape) << ' ' << int(p.aim)
          << ' ' << p.count << ' ' << p.damage << ' ' << p.group << ' ' << int(p.palette) << ' '
          << int(p.terrain) << ' ' << p.speed << ' ' << p.acceleration << ' ' << p.maxSpeed << ' '
          << p.lifetime << ' ' << p.direction << ' ' << p.spread << ' ' << p.turnRate << ' '
          << p.turnSeconds << ' ' << p.visualRadius << ' ' << p.hitRadius << ' ' << p.wobble << ' '
          << p.frequency << ' ' << p.growth << ' ' << p.rotation << ' ' << p.releaseSeconds << ' '
          << p.jitter << '\n';
    for (const auto& e : l.emitters) {
        s << e.id << ' ' << std::quoted(e.name.c_str()) << ' ' << e.map << ' ' << int(e.origin) << ' '
          << e.object << ' ' << e.gateSwitch << ' ' << e.position.x << ' ' << e.position.y << ' '
          << e.startDelay << ' ' << e.loopDelay << ' ' << e.seed << ' ' << e.enabled << ' ' << e.loop
          << ' ' << e.persistent << ' ' << e.waves.size() << ' ' << int(e.target) << ' '
          << int(e.factionFilter) << ' ' << e.targetObject << ' ' << e.targetFaction << ' '
          << e.targetPoint.x << ' ' << e.targetPoint.y << ' ' << e.targetRange << ' ' << e.excludeParent
          << '\n';
        for (const auto& wave : e.waves)
            s << wave.pattern << ' ' << wave.repeat << ' ' << wave.delay << ' ' << wave.warning << ' '
              << wave.interval << ' ' << wave.angleStep << '\n';
    }
    return s.str();
}
BarrageLibrary loadBarrage(const std::string& text) {
    require(text.size() <= 4 * 1024 * 1024, "Barrage file exceeds 4 MiB");
    std::istringstream s(text);
    std::string magic, name;
    std::size_t patterns{}, emitters{};
    int policy{};
    BarrageLibrary l;
    require(bool(s >> magic >> l.nextId >> policy >> patterns >> emitters) &&
                (magic == "ECHO_BARRAGE_1" || magic == "ECHO_BARRAGE_2") && policy >= 0 && policy <= 2 &&
                patterns <= 128 && emitters <= maxBarrageEmitters,
            "Invalid barrage header");
    l.overflow = BulletOverflowPolicy(policy);
    for (std::size_t i = 0; i < patterns; ++i) {
        BarragePattern p;
        int shape{}, aim{}, group{}, palette{}, terrain{};
        s >> p.id >> std::quoted(name) >> shape >> aim >> p.count >> p.damage >> group >> palette >>
            terrain >> p.speed >> p.acceleration >> p.maxSpeed >> p.lifetime >> p.direction >>
            p.spread >> p.turnRate >> p.turnSeconds >> p.visualRadius >> p.hitRadius >> p.wobble >>
            p.frequency >> p.growth >> p.rotation >> p.releaseSeconds >> p.jitter;
        require(bool(s) && name.size() < 64 && shape >= 0 && shape <= 3 && aim >= 0 && aim <= 2 &&
                    group >= 0 && group <= 65535 && palette >= 0 && palette <= 7 && terrain >= 0 &&
                    terrain <= 2,
                "Invalid barrage pattern row");
        p.name.set(name);
        p.shape = BarrageShape(shape);
        p.aim = BarrageAim(aim);
        p.group = std::uint16_t(group);
        p.palette = std::uint8_t(palette);
        p.terrain = std::uint8_t(terrain);
        l.patterns.push_back(p);
    }
    for (std::size_t i = 0; i < emitters; ++i) {
        BarrageEmitter e;
        int type{}, enabled{}, loop{}, persistent{};
        std::size_t waves{};
        s >> e.id >> std::quoted(name) >> e.map >> type >> e.object >> e.gateSwitch >> e.position.x >>
            e.position.y >> e.startDelay >> e.loopDelay >> e.seed >> enabled >> loop >> persistent >>
            waves;
        require(bool(s) && name.size() < 64 && type >= 0 && type <= 3 && enabled >= 0 && enabled <= 1 &&
                    loop >= 0 && loop <= 1 && persistent >= 0 && persistent <= 1 && waves >= 1 &&
                    waves <= 32,
                "Invalid barrage emitter row");
        if (magic == "ECHO_BARRAGE_2") {
            int target{}, filter{}, faction{}, exclude{};
            require(bool(s >> target >> filter >> e.targetObject >> faction >> e.targetPoint.x >>
                         e.targetPoint.y >> e.targetRange >> exclude) &&
                        target >= 0 && target <= 4 && filter >= 0 && filter <= 2 && faction >= 0 &&
                        faction <= 65535 && exclude >= 0 && exclude <= 1,
                    "Invalid barrage target row");
            e.target = BarrageTarget(target);
            e.factionFilter = BarrageFactionFilter(filter);
            e.targetFaction = std::uint16_t(faction);
            e.excludeParent = exclude != 0;
        }
        e.name.set(name);
        e.origin = BarrageOrigin(type);
        e.enabled = enabled;
        e.loop = loop;
        e.persistent = persistent;
        for (std::size_t j = 0; j < waves; ++j) {
            BarrageWave wave;
            require(bool(s >> wave.pattern >> wave.repeat >> wave.delay >> wave.warning >>
                         wave.interval >> wave.angleStep),
                    "Truncated barrage wave");
            e.waves.push_back(wave);
        }
        l.emitters.push_back(std::move(e));
    }
    s >> std::ws;
    require(s.eof(), "Trailing barrage file content");
    validateBarrage(l);
    return l;
}
void fireBarrage(WorldState& w, const BarragePattern& p, Vec2 pos, float direction, std::uint32_t& seed,
                 std::uint64_t source) {
    for (int i = 0; i < p.count; ++i) {
        if (!w.barrage.attemptBudget) {
            w.barrage.budgetRejected += std::uint32_t(p.count - i);
            return;
        }
        --w.barrage.attemptBudget;
        float fraction = p.shape == BarrageShape::Stream ? 0.f
                         : p.shape == BarrageShape::Ring || p.shape == BarrageShape::Formation
                             ? float(i) / p.count
                             : (p.count > 1 ? float(i) / (p.count - 1) - .5f : 0);
        float jitter = (float(random(seed)) / float(UINT32_MAX) * 2 - 1) * p.jitter;
        float a = direction + (fraction * p.spread + jitter) * radians;
        Bullet b;
        b.x = pos.x;
        b.y = pos.y;
        b.angle = a;
        b.vx = std::cos(a) * p.speed;
        b.vy = std::sin(a) * p.speed;
        b.damage = p.damage;
        b.group = p.group;
        b.source = source;
        b.palette = p.palette;
        b.terrain = p.terrain;
        b.acceleration = p.acceleration;
        b.maxSpeed = p.maxSpeed;
        b.expireAge = p.lifetime;
        b.visualR = p.visualRadius;
        b.hitR = p.hitRadius;
        b.turnRate = p.turnRate * radians;
        b.turnTime = p.turnSeconds;
        if (p.wobble > 0)
            b.motion = {float(i) * .5f, p.wobble, p.frequency, 1};
        if (p.shape == BarrageShape::Formation) {
            b.formation = {pos.x, pos.y, std::cos(a) * 32, std::sin(a) * 32, 1, p.growth, 0, 0, 0, 0,
                           0,     true};
            b.rotationRate = p.rotation * radians;
            b.releaseAge = p.releaseSeconds;
        }
        spawnBullet(w, b);
    }
}
std::uint32_t clearBulletGroup(WorldState& w, std::uint16_t group) {
    std::uint32_t removed = 0;
    for (std::size_t i = 0; i < w.bullets.count;) {
        if (w.bullets[i].group == group) {
            w.bullets.erase(i);
            ++removed;
        } else
            ++i;
    }
    return removed;
}
void resetBarrage(WorldState& w, const BarrageLibrary& l) {
    w.barrage = {};
    w.barrage.map = w.activeMap;
    w.bulletOverflow = l.overflow;
    for (std::size_t i = 0; i < l.emitters.size(); ++i) {
        auto& s = w.barrage.emitters[i];
        const auto& e = l.emitters[i];
        s.id = e.id;
        s.enabled = e.enabled;
        s.seed = s.initialSeed = e.seed;
        s.initialDelay = e.startDelay;
        s.elapsed = -e.startDelay;
    }
}
bool setBarrageEmitter(WorldState& w, std::uint64_t id, bool enabled, bool restart) {
    for (auto& e : w.barrage.emitters)
        if (id && e.id == id) {
            e.enabled = enabled;
            if (restart) {
                e.elapsed = -e.initialDelay;
                e.seed = e.initialSeed;
                e.wave = 0;
                e.shot = 0;
                e.cycles = 0;
                e.locked = false;
                e.finished = false;
                e.warning = false;
                e.targetHandle = {};
                e.targetId = 0;
                e.target = {};
                e.targetValid = e.waitingForTarget = false;
            }
            return true;
        }
    return false;
}
void enterBarrageMap(WorldState& w, const BarrageLibrary& l) {
    auto& system = w.barrage;
    if (system.map == w.activeMap)
        return;
    system.map = w.activeMap;
    for (std::size_t i = 0; i < l.emitters.size(); ++i) {
        const auto& e = l.emitters[i];
        auto& s = system.emitters[i];
        s.warning = false;
        if (e.map == w.activeMap && !e.persistent) {
            s = {};
            s.id = e.id;
            s.enabled = e.enabled;
            s.seed = s.initialSeed = e.seed;
            s.initialDelay = e.startDelay;
            s.elapsed = -e.startDelay;
        }
    }
}
void stepBarrage(WorldState& w, const BarrageLibrary& l, float dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1)
        return;
    auto& system = w.barrage;
    system.attemptBudget = 4096;
    system.targetQueries = system.targetCandidates = 0;
    enterBarrageMap(w, l);
    for (std::size_t i = 0; i < l.emitters.size(); ++i) {
        const auto& e = l.emitters[i];
        auto& state = system.emitters[i];
        state.warning = false;
        if (e.map != w.activeMap || !state.enabled || state.finished ||
            (e.gateSwitch >= 0 && !w.events.switches[e.gateSwitch]))
            continue;
        Vec2 pos{};
        if (!origin(w, e, pos)) {
            state.warning = false;
            ++system.missingParents;
            continue;
        }
        state.elapsed += dt;
        state.warning = false;
        // All waves share this emitter's target and world during one fixed step.
        // Cache once to cap candidate scans at 64 * 256 even during a catch-up burst.
        BarrageTargetResult targetResult;
        bool targetQueried = false, targetAvailable = false;
        // Bounded work independently of frame rate. Any partial wave remains in the snapshot.
        for (int work = 0; work < 64; ++work) {
            if (state.wave >= e.waves.size()) {
                if (!e.loop) {
                    state.finished = true;
                    break;
                }
                state.wave = 0;
                state.shot = 0;
                state.locked = false;
                state.elapsed -= e.loopDelay;
                ++state.cycles;
            }
            const auto& wave = e.waves[state.wave];
            const auto* pattern = findBarragePattern(l, wave.pattern);
            if (!pattern) {
                state.finished = true;
                ++system.invalidDefinitions;
                break;
            }
            float fireAt = wave.delay + wave.warning + state.shot * wave.interval;
            float warningStart = fireAt - wave.warning;
            state.warning = state.elapsed >= warningStart && state.elapsed < fireAt;
            if (pattern->aim != BarrageAim::Fixed && state.elapsed >= warningStart &&
                (pattern->aim == BarrageAim::AtFire || !state.locked)) {
                if (!targetQueried) {
                    targetQueried = true;
                    ++system.targetQueries;
                    targetAvailable =
                        queryBarrageTarget(w, e, pos, targetResult, &system.targetCandidates);
                }
                if (!targetAvailable) {
                    // HARD CONTRACT: lost targets re-arm a full warning. Never fire at stale
                    // coordinates, or hide this cross-frame wait outside the snapshot.
                    state.elapsed = warningStart;
                    state.locked = state.warning = state.targetValid = false;
                    state.waitingForTarget = true;
                    state.targetHandle = {};
                    state.targetId = 0;
                    ++system.missingTargets;
                    break;
                }
                state.target = targetResult.position;
                state.targetHandle = targetResult.handle;
                state.targetId = targetResult.id;
                state.targetValid = true;
                state.waitingForTarget = false;
            }
            if (!state.locked && state.elapsed >= warningStart) {
                state.locked = true;
                state.origin = pos;
                state.angle = pattern->direction * radians + wave.angleStep * state.shot * radians;
                if (pattern->aim == BarrageAim::AtWarning)
                    state.angle += std::atan2(state.target.y - pos.y, state.target.x - pos.x);
            }
            state.origin = pos;
            state.warningAngle = state.angle;
            if (pattern->aim == BarrageAim::AtFire && state.targetValid)
                state.warningAngle += std::atan2(state.target.y - pos.y, state.target.x - pos.x);
            state.warningSpread = pattern->spread;
            state.warningCount = std::uint16_t(pattern->count);
            state.warningShape = std::uint8_t(pattern->shape);
            if (state.elapsed + 1e-6f < fireAt)
                break;
            float angle = state.angle;
            if (pattern->aim == BarrageAim::AtFire) {
                angle += std::atan2(state.target.y - pos.y, state.target.x - pos.x);
            }
            fireBarrage(w, *pattern, pos, angle, state.seed, e.id);
            ++state.shot;
            ++system.volleys;
            state.locked = false;
            state.warning = false;
            if (state.shot >= std::uint32_t(wave.repeat)) {
                state.elapsed -= fireAt;
                state.shot = 0;
                ++state.wave;
            }
        }
    }
}
} // namespace echo
