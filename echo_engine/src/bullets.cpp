#include "echo/focus.hpp"
#include "echo/platform_features.hpp"
#include "echo/presets.hpp"
#include "echo/animations.hpp"
#include "echo/collision.hpp"
#include "echo/simulation.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace echo {
bool spawnBullet(WorldState& w, Bullet b) {
    // Stable allocation and deterministic overflow are gameplay decisions, so counters and policy are
    // snapshotted.
    if (w.nextBulletId == UINT32_MAX || !std::isfinite(b.x) || !std::isfinite(b.y) ||
        !std::isfinite(b.vx) || !std::isfinite(b.vy) || !std::isfinite(b.hitR) || b.hitR < 0 ||
        b.damage < 0 || !std::isfinite(b.maxSpeed) || b.maxSpeed < 0 || !std::isfinite(b.expireAge) ||
        b.expireAge <= 0 || !std::isfinite(b.acceleration)) {
        ++w.bulletsDropped;
        return false;
    }
    b.id = w.nextBulletId++;
    b.px = b.x;
    b.py = b.y;
    if (w.bullets.count == maxBullets) {
        if (w.bulletOverflow == BulletOverflowPolicy::RejectNewest) {
            ++w.bulletsDropped;
            return false;
        }
        int oldest = -1;
        for (int i = 0; i < int(w.bullets.count); ++i) {
            const auto& old = w.bullets[i];
            if (w.bulletOverflow == BulletOverflowPolicy::RecycleGroup && old.group != b.group)
                continue;
            if (oldest < 0 || old.age > w.bullets[oldest].age ||
                (old.age == w.bullets[oldest].age && old.id < w.bullets[oldest].id))
                oldest = i;
        }
        if (oldest < 0) {
            ++w.bulletsDropped;
            return false;
        }
        w.bullets[oldest] = b;
        ++w.bulletsRecycled;
        return true;
    }
    w.bullets.push(b);
    return true;
}
void firePattern(WorldState& w, const Instruction& op, Vec2 pos, float direction, bool aim) {
    if (aim)
        direction = std::atan2(w.kino.y + 32 - pos.y, w.kino.x + 14 - pos.x);
    int count = std::clamp(op.count, 1, maxBullets);
    for (int i = 0; i < count; ++i) {
        float a = direction + (op.pattern == 0 ? float(i) * 2 * std::numbers::pi_v<float> / count
                                               : (count > 1 ? (float(i) / (count - 1) - .5f) * .8f : 0));
        Bullet b;
        b.x = pos.x;
        b.y = pos.y;
        b.angle = a;
        b.vx = std::cos(a) * op.speed;
        b.vy = std::sin(a) * op.speed;
        b.palette = std::uint8_t(w.aster.phaseId % 3);
        if (op.pattern == 2) {
            b.turnRate = .8f;
            b.turnTime = 2;
        }
        if (op.pattern == 3) {
            b.motion = {float(i) * .5f, 24, 5, 1};
        }
        if (op.pattern == 4) {
            b.formation.active = true;
            b.formation.cx = pos.x;
            b.formation.cy = pos.y;
            b.formation.lx = std::cos(a) * 32;
            b.formation.ly = std::sin(a) * 32;
            b.formation.growth = 2;
            b.vx = b.vy = 0;
        }
        spawnBullet(w, b);
    }
}
static float terrainEntry(const WorldState& w, const Bullet& b, SimulationStats& stats) {
    if (!b.terrain)
        return 2;
    float best = 2, radius = b.hitR;
    Vec2 a{b.px, b.py}, c{b.x, b.y}, delta{c.x - a.x, c.y - a.y};
    auto box = [&](Rect r, Vec2 motion = Vec2{}) {
        float entry;
        if (segmentRect(a, {c.x - motion.x, c.y - motion.y},
                        {r.x - radius, r.y - radius, r.w + radius * 2, r.h + radius * 2}, &entry))
            best = std::min(best, entry);
        ++stats.terrainCandidates;
    };
    int left = std::clamp(int(std::floor((std::min(a.x, c.x) - radius) / tileSize)), 0, mapWidth - 1);
    int right = std::clamp(int(std::floor((std::max(a.x, c.x) + radius) / tileSize)), 0, mapWidth - 1);
    int top = std::clamp(int(std::floor((std::min(a.y, c.y) - radius) / tileSize)), 0, mapHeight - 1);
    int bottom = std::clamp(int(std::floor((std::max(a.y, c.y) + radius) / tileSize)), 0, mapHeight - 1);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            auto tile = w.level.at(x, y);
            if (collisionAllowed(w, CollisionClass::Projectile, CollisionClass::Terrain) &&
                (tile == Tile::Solid || tile == Tile::Spike ||
                 (b.terrain == 2 && tile == Tile::OneWay && delta.y > 0 &&
                  a.y + radius <= y * tileSize + .01f)))
                box({x * tileSize, y * tileSize, tileSize, tileSize});
        }
    if (collisionAllowed(w, CollisionClass::Projectile, CollisionClass::Terrain))
        for (const auto& collider : w.level.colliders) {
            float entry;
            if (sweepCollider({a.x - radius, a.y - radius, radius * 2, radius * 2}, delta, collider,
                              entry))
                best = std::min(best, entry);
            ++stats.terrainCandidates;
        }
    for (const auto& f : w.level.features)
        if (featureSupports(f) &&
            collisionAllowed(w, CollisionClass::Projectile, CollisionClass::Terrain) && b.terrain == 2 &&
            delta.y > f.bounds.y - f.previous.y && a.y + radius <= f.previous.y + .01f)
            box({f.previous.x, f.previous.y, f.bounds.w, f.bounds.h},
                {f.bounds.x - f.previous.x, f.bounds.y - f.previous.y});
    for (const auto& platform : w.level.platforms)
        if (collisionAllowed(w, CollisionClass::Projectile, CollisionClass::Terrain) && b.terrain == 2 &&
            delta.y > platform.pos.y - platform.previous.y && a.y + radius <= platform.previous.y + .01f)
            box({platform.previous.x, platform.previous.y, platform.base.w, platform.base.h},
                {platform.pos.x - platform.previous.x, platform.pos.y - platform.previous.y});
    for (const auto& e : w.level.events)
        if (!e.erased && e.solid &&
            collisionAllowed(w, CollisionClass::Projectile, CollisionClass::Trigger))
            box(e.region);
    for (const auto& o : w.objects.slots) {
        if (!o.active || !o.alive || !o.solid || o.map != w.activeMap || o.kind == ObjectKind::Player ||
            !collisionAllowed(w, CollisionClass::Projectile, objectCollisionClass(o)))
            continue;
        auto r = objectBody(w, o);
        r.x += o.px - o.x;
        r.y += o.py - o.y;
        box(r, {o.x - o.px, o.y - o.py});
    }
    return best;
}
void stepBullets(WorldState& w, float dt, Vec2 oldPlayer, SimulationStats& stats, Hooks* hooks,
                 const Rect* previousHurt, bool previousBox, const HurtConfig* previousConfig,
                 const KinoState* previousKino) {
    auto old = previousKino ? *previousKino : w.kino;
    old.x = oldPlayer.x;
    old.y = oldPlayer.y;
    if (!previousKino && previousHurt && previousBox) {
        auto r = *previousHurt;
        old.animation.hurt = {r.x - old.x, r.y - old.y, r.w, r.h, true};
    }
    const auto& oldConfig = previousConfig ? *previousConfig : w.hurt;
    stats.updatedBullets = w.bullets.count;
    for (std::size_t i = 0; i < w.bullets.count;) {
        auto& b = w.bullets[i];
        b.px = b.x;
        b.py = b.y;
        float elapsed = std::min(dt, std::max(0.f, b.expireAge - b.age));
        b.age += elapsed;
        float turning = std::min(elapsed, std::max(0.f, b.turnTime));
        b.turnTime -= turning;
        b.angle += b.turnRate * turning;
        float oldSpeed = std::hypot(b.vx, b.vy);
        float speed = std::clamp(oldSpeed + b.acceleration * elapsed, 0.f, b.maxSpeed);
        if (turning > 0 || (oldSpeed == 0 && speed > 0)) {
            b.vx = std::cos(b.angle) * speed;
            b.vy = std::sin(b.angle) * speed;
        } else if (oldSpeed > 0 && speed != oldSpeed) {
            b.vx *= speed / oldSpeed;
            b.vy *= speed / oldSpeed;
            b.angle = std::atan2(b.vy, b.vx);
        }
        if (b.formation.active) {
            auto& f = b.formation;
            f.age += elapsed;
            f.cx += f.driftX * elapsed;
            f.cy += f.driftY * elapsed;
            f.scale = std::max(0.f, f.scale + f.growth * elapsed);
            f.rotation += b.rotationRate * elapsed;
            float c = std::cos(f.rotation), s = std::sin(f.rotation);
            b.x = f.cx + (f.lx * c - f.ly * s) * f.scale;
            b.y = f.cy + (f.lx * s + f.ly * c) * f.scale;
            if (b.releaseAge > 0 && f.age >= b.releaseAge) {
                f.active = false;
                b.vx = f.driftX + f.growth * (f.lx * c - f.ly * s) - b.rotationRate * (b.y - f.cy);
                b.vy = f.driftY + f.growth * (f.lx * s + f.ly * c) + b.rotationRate * (b.x - f.cx);
                b.angle = std::atan2(b.vy, b.vx);
            }
        } else {
            b.x += b.vx * elapsed;
            b.y += b.vy * elapsed;
            if (b.motion.type == 1) {
                auto& m = b.motion;
                float d = (std::sin(b.age * m.frequency + m.phase) -
                           std::sin((b.age - elapsed) * m.frequency + m.phase)) *
                          m.amplitude;
                b.x -= std::sin(b.angle) * d;
                b.y += std::cos(b.angle) * d;
            }
        }
        float wall = terrainEntry(w, b, stats);
        if (collisionAllowed(w, CollisionClass::Projectile, CollisionClass::Player) && !b.brake &&
            b.damage > 0 && w.kino.invulnTimer <= 0 && w.rewindExitGrace <= 0 &&
            w.state != TimeState::Hit) {
            float entry{};
            bool hit =
                playerHurtSweep(w, old, oldConfig, {b.px, b.py}, {b.x, b.y}, b.hitR,
                                w.hurt.projectileBox, hurtBullets, entry, &stats.collisionCandidates);
            if (hit && entry <= wall) {
                w.state = TimeState::Hit;
                w.gameplayClockPaused = true;
                w.kino.hitFlash = .2f;
                w.kino.hp = std::max(0, w.kino.hp - b.damage);
                ++w.soundEvent;
                w.soundKind = 2;
                if (hooks)
                    hooks->collision(w, b.id);
            }
        }
        if (wall <= 1 || b.age >= b.expireAge || b.x < -256 || b.x > mapWidth * tileSize + 256 ||
            b.y < -256 || b.y > mapHeight * tileSize + 256) {
            w.bullets.erase(i);
            continue;
        }
        ++i;
    }
}
void stepProgram(WorldState& w, float dt) {
    auto& s = w.script;
    if (!s.program.count)
        return;
    s.wait -= dt;
    if (s.wait > 0)
        return;
    // A zero-wait or malformed script is bounded to 64 instructions per simulation tick.
    for (int budget = 0; budget < 64 && s.wait <= 0; ++budget) {
        if (s.pc >= int(s.program.count)) {
            if (!s.loop)
                return;
            s.pc = 0;
        }
        auto op = s.program[s.pc++];
        switch (op.op) {
        case OpCode::Fire:
            firePattern(w, op, {w.aster.x + 24, w.aster.y + 24}, s.direction, s.aimPlayer);
            break;
        case OpCode::Wait:
            s.wait += std::max(fixedDt, op.value);
            break;
        case OpCode::Aim:
            s.aimPlayer = true;
            break;
        case OpCode::Incremental:
            s.direction += op.value;
            break;
        case OpCode::SetDirection:
            s.direction = op.value;
            s.aimPlayer = false;
            break;
        }
    }
}
void stepBoss(WorldState& w, float dt, Hooks* hooks) {
    auto& a = w.aster;
    if (!a.enabled || a.hp <= 0 || w.bulletPreset == 0)
        return;
    a.invulnTimer = std::max(0.0f, a.invulnTimer - dt);
    a.specialCooldown -= dt;
    a.specialCastElapsed += dt;
    if (a.knockbackActive) {
        a.knockbackTime += dt;
        float u = std::clamp(a.knockbackTime / std::max(.001f, a.knockbackDuration), 0.0f, 1.0f);
        a.x = a.knockbackStartX + (a.knockbackTargetX - a.knockbackStartX) * (1 - std::pow(1 - u, 3));
        a.knockbackActive = u < 1;
    } else if (a.phaseId == 4) {
        a.x += a.vx * a.walkDir * dt;
        if (a.x > 1080 || a.x < 400)
            a.walkDir = -a.walkDir;
    }
    if (a.resting) {
        a.attackTimer -= dt;
        if (a.attackTimer <= 0) {
            a.attack = {};
            a.attack.phase = a.phaseId;
            a.attack.variant = a.attackCycle++ % 5;
            if (w.bulletPreset > 0)
                planBulletPreset(w);
            else if (hooks)
                hooks->planAttack(w);
            a.resting = false;
        }
    } else {
        auto& p = a.attack;
        p.t += dt;
        for (auto& v : p.volleys) {
            if (v.aimed && !v.locked && p.t >= v.at - .85f) {
                v.angle = std::atan2(w.kino.y + 32 - v.origin.y, w.kino.x + 14 - v.origin.x);
                v.locked = true;
            }
            if (!v.fired && p.t >= v.at) {
                v.fired = true;
                Instruction op;
                op.count = v.count;
                op.speed = v.speed;
                op.pattern = v.pattern;
                firePattern(w, op, v.origin, v.angle, false);
            }
        }
        for (std::size_t i = 0; i < p.zones.count; ++i) {
            auto& z = p.zones[i];
            z.fired = p.t >= z.start;
            bool active = z.fired && p.t <= z.end;
            bool inside = active && w.kino.x + 14 >= z.x && w.kino.x + 14 <= z.x + z.w &&
                          w.kino.y + 32 >= z.y && w.kino.y + 32 <= z.y + z.h;
            if (inside && !z.entered && hooks)
                hooks->region(w, int(i));
            z.entered = inside;
            // Region callbacks use their own entry rule; damage respects the author's hurt profile.
            bool damaging = collisionAllowed(w, CollisionClass::Projectile, CollisionClass::Player) &&
                            active && !z.brake && playerHurtRect(w, {z.x, z.y, z.w, z.h}, hurtBullets);
            if (damaging && w.rewindExitGrace <= 0 && w.kino.invulnTimer <= 0) {
                w.state = TimeState::Hit;
                w.gameplayClockPaused = true;
                w.kino.hp = 0;
            }
        }
        if (p.t >= p.end) {
            a.resting = true;
            a.attackTimer = .8f;
        }
    }
    BossAnimState anim =
        a.resting ? BossAnimState::Idle : (a.phaseId == 4 ? BossAnimState::Walk : BossAnimState::Cast);
    if (anim != a.animState) {
        a.animState = anim;
        a.animTime = 0;
    } else
        a.animTime += dt;
}
} // namespace echo
