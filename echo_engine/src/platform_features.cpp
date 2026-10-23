#include "echo/platform_features.hpp"
#include "echo/collision.hpp"
#include "echo/objects.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace echo {
void validateMobility(const MobilityConfig& c) {
    const float values[] = {c.climbSpeed, c.swimSpeed,    c.waterGravity, c.swimJump,
                            c.dashSpeed,  c.dashDuration, c.dashCooldown, c.wallSlideSpeed,
                            c.wallJumpX,  c.wallJumpY,    c.wallLock,     c.waterFallSpeed};
    for (float v : values)
        if (!std::isfinite(v) || v < 0 || v > 4000)
            throw std::runtime_error("Invalid mobility parameter");
    if (c.climbSpeed < 1 || c.swimSpeed < 1 || c.waterGravity > 2 || c.dashDuration < .01f ||
        c.dashDuration > 2 || c.dashCooldown > 30 || c.wallLock > 2 || c.dashKey < 8 ||
        c.dashKey > 254 || c.climbKey < 8 || c.climbKey > 254 || c.dashKey == c.climbKey)
        throw std::runtime_error("Mobility range / keyboard conflict");
}
std::string writeMobility(const MobilityConfig& c) {
    validateMobility(c);
    std::ostringstream out;
    out.precision(9);
    out << int(c.dashEnabled) << ' ' << int(c.wallJumpEnabled) << ' ' << int(c.crushKills) << ' '
        << c.climbSpeed << ' ' << c.swimSpeed << ' ' << c.waterGravity << ' ' << c.swimJump << ' '
        << c.dashSpeed << ' ' << c.dashDuration << ' ' << c.dashCooldown << ' ' << c.wallSlideSpeed
        << ' ' << c.wallJumpX << ' ' << c.wallJumpY << ' ' << c.wallLock << ' ' << c.dashKey << ' '
        << c.climbKey << ' ' << c.waterFallSpeed;
    return out.str();
}
MobilityConfig readMobility(const std::string& text) {
    MobilityConfig c;
    std::istringstream in(text);
    int dash = -1, wall = -1, crush = -1;
    in >> dash >> wall >> crush >> c.climbSpeed >> c.swimSpeed >> c.waterGravity >> c.swimJump >>
        c.dashSpeed >> c.dashDuration >> c.dashCooldown >> c.wallSlideSpeed >> c.wallJumpX >>
        c.wallJumpY >> c.wallLock >> c.dashKey >> c.climbKey >> c.waterFallSpeed;
    if (!in || dash < 0 || dash > 1 || wall < 0 || wall > 1 || crush < 0 || crush > 1)
        throw std::runtime_error("Invalid mobility configuration");
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Trailing mobility configuration");
    c.dashEnabled = dash;
    c.wallJumpEnabled = wall;
    c.crushKills = crush;
    validateMobility(c);
    return c;
}
void validateFeature(const PlatformFeature& f) {
    if (!f.id || f.id > 1000000 || int(f.kind) > int(PlatformKind::Lift) || !std::isfinite(f.base.x) ||
        !std::isfinite(f.base.y) || !std::isfinite(f.base.w) || !std::isfinite(f.base.h) ||
        std::abs(f.base.x) > 8192 || std::abs(f.base.y) > 8192 || f.base.w < 1 || f.base.w > 4096 ||
        f.base.h < 1 || f.base.h > 4096 || !std::isfinite(f.speed) || std::abs(f.speed) > 4000 ||
        !std::isfinite(f.delay) || f.delay < 0 || f.delay > 30 || !std::isfinite(f.resetSeconds) ||
        f.resetSeconds < .1f || f.resetSeconds > 60 ||
        ((f.kind == PlatformKind::Spring || f.kind == PlatformKind::Falling) && f.speed < 1) ||
        !std::isfinite(f.travel.x) || !std::isfinite(f.travel.y) || std::abs(f.travel.x) > 8192 ||
        std::abs(f.travel.y) > 8192 ||
        (f.kind == PlatformKind::Lift &&
         (f.speed < 0 || std::hypot(f.travel.x, f.travel.y) < 1 ||
          std::abs(f.base.x + f.travel.x) > 8192 || std::abs(f.base.y + f.travel.y) > 8192)))
        throw std::runtime_error("Invalid platform feature / route");
}
void validateFeatures(const LevelState& level) {
    if (level.features.count > 64 || level.nextFeatureId < 1 || level.nextFeatureId > 1000001)
        throw std::runtime_error("Platform feature capacity / ID exhausted");
    for (std::size_t i = 0; i < level.features.count; ++i) {
        const auto& f = level.features[i];
        validateFeature(f);
        if (f.id >= level.nextFeatureId)
            throw std::runtime_error("Invalid platform feature ID allocator");
        for (std::size_t j = 0; j < i; ++j)
            if (level.features[j].id == f.id)
                throw std::runtime_error("Duplicate platform feature ID");
    }
}
PlatformFeature platformPreset(PlatformKind kind, Rect region, std::uint32_t id) {
    PlatformFeature f;
    f.id = id;
    f.kind = kind;
    f.base = f.bounds = region;
    f.previous = {region.x, region.y};
    f.speed = kind == PlatformKind::Spring ? 560 : kind == PlatformKind::Falling ? 980 : 100;
    return f;
}
int featureSupportId(const PlatformFeature& f) {
    return -100000 - int(f.id);
}
bool featureSupports(const PlatformFeature& f) {
    return f.enabled && (f.kind != PlatformKind::Falling || f.stage != 2) &&
           (f.kind == PlatformKind::Conveyor || f.kind == PlatformKind::Spring ||
            f.kind == PlatformKind::Falling || f.kind == PlatformKind::Lift);
}
const PlatformFeature* supportFeature(const LevelState& l, int id) {
    for (const auto& f : l.features)
        if (featureSupportId(f) == id && featureSupports(f))
            return &f;
    return nullptr;
}
namespace {
void stepLift(PlatformFeature& f, float dt, WorldState& world, Rect playerBody) {
    if (f.speed <= 0 || f.stage == 2)
        return; // speed 0 freezes the route, including dwell; completed one-shots remain supporting.
    const float travelTime = std::hypot(f.travel.x, f.travel.y) / f.speed;
    float remaining = dt;
    bool cycleChecked = false;
    auto rider = [&] {
        return world.kino.grounded && world.kino.standingPlatformId == featureSupportId(f) &&
               collisionAllowed(world, CollisionClass::Player, CollisionClass::Terrain);
    };
    auto move = [&] {
        Vec2 delta{f.base.x + f.travel.x * f.progress - f.bounds.x,
                   f.base.y + f.travel.y * f.progress - f.bounds.y};
        f.bounds.x += delta.x;
        f.bounds.y += delta.y;
        // Sweep each leg, even if a route reverses inside this physics substep.
        // The trajectory is derived here; all persistent traversal data stays in WorldState.
        if (rider())
            carryPlatformPlayer(world, {world.kino.x, world.kino.y, playerBody.w, playerBody.h}, delta);
        carryPlatformObjects(world, featureSupportId(f), delta);
    };
    while (remaining > 1e-7f && f.stage != 2) {
        // At the cycle boundary skip whole cycles arithmetically. Even a one-pixel route
        // at maximum speed has bounded work; this changes neither RNG nor gameplay ordering.
        if (f.repeat && f.stage == 0 && f.progress == 0 && f.direction == 1) {
            float cycle = 2 * (travelTime + f.delay);
            if (remaining >= cycle &&
                ((!rider() && !objectPlatformRider(world, featureSupportId(f))) || cycleChecked))
                remaining = std::fmod(remaining, cycle);
        }
        if (f.stage == 1) {
            float wait = std::max(0.f, f.delay - f.elapsed);
            float used = std::min(remaining, wait);
            f.elapsed += used;
            remaining -= used;
            if (f.elapsed + 1e-7f < f.delay)
                break;
            f.elapsed = 0;
            f.stage = 0;
            f.direction = -f.direction;
            if (f.progress == 0 && f.direction == 1)
                cycleChecked = true; // one full rider cycle already tested both swept legs
            continue;
        }
        float distance = f.direction > 0 ? 1 - f.progress : f.progress;
        float needed = distance * travelTime;
        float used = std::min(remaining, needed);
        f.progress = std::clamp(f.progress + f.direction * used / travelTime, 0.f, 1.f);
        remaining -= used;
        if (used + 1e-6f < needed) {
            move();
            break;
        }
        f.progress = f.direction > 0 ? 1.f : 0.f;
        f.stage = f.repeat ? 1 : 2;
        f.elapsed = 0;
        move();
        if (world.state == TimeState::Hit)
            break;
    }
    f.bounds.x = f.base.x + f.travel.x * f.progress;
    f.bounds.y = f.base.y + f.travel.y * f.progress;
}
} // namespace
void stepPlatformFeatures(WorldState& w, float dt, Rect playerBody) {
    for (auto& f : w.level.features) {
        f.previous = {f.bounds.x, f.bounds.y};
        f.cooldown = std::max(0.f, f.cooldown - dt);
        if (!f.enabled)
            continue;
        if (f.kind == PlatformKind::Lift) {
            stepLift(f, dt, w, playerBody);
            continue;
        }
        if (f.kind != PlatformKind::Falling)
            continue;
        if (f.stage == 0) {
            if ((w.kino.grounded && w.kino.standingPlatformId == featureSupportId(f)) ||
                objectPlatformRider(w, featureSupportId(f))) {
                f.elapsed += dt;
                if (f.elapsed >= f.delay) {
                    f.stage = 1;
                    f.elapsed = 0;
                }
            } else
                f.elapsed = 0;
        }
        if (f.stage == 1) {
            f.velocity = std::min(3000.f, f.velocity + f.speed * dt);
            f.bounds.y += f.velocity * dt;
            if (f.bounds.y > mapHeight * tileSize + 128) {
                f.stage = 2;
                f.elapsed = 0;
            }
        } else if (f.stage == 2) {
            f.elapsed += dt;
            if (f.elapsed >= f.resetSeconds) {
                f.stage = 0;
                f.elapsed = f.velocity = 0;
                f.bounds = f.base;
                f.previous = {f.base.x, f.base.y}; // reset teleports; never carry through the map
            }
        }
        if (featureSupports(f))
            carryPlatformObjects(w, featureSupportId(f),
                                 {f.bounds.x - f.previous.x, f.bounds.y - f.previous.y});
    }
}
void updateMobilityContact(WorldState& w, Rect body) {
    auto& k = w.kino;
    k.inWater = false;
    k.waterId = k.ladderId = -1;
    bool triggers = collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger);
    for (const auto& f : w.level.features) {
        if (!triggers || !f.enabled || body.x + body.w <= f.bounds.x ||
            body.x >= f.bounds.x + f.bounds.w || body.y + body.h <= f.bounds.y ||
            body.y >= f.bounds.y + f.bounds.h)
            continue;
        if (f.kind == PlatformKind::Water) {
            k.inWater = true;
            if (k.waterId < 0 || int(f.id) < k.waterId)
                k.waterId = int(f.id);
        }
        if (f.kind == PlatformKind::Ladder && (k.ladderId < 0 || int(f.id) < k.ladderId))
            k.ladderId = int(f.id);
    }
    if (k.ladderId < 0)
        k.climbing = false;
}
float terrainMovementFraction(const WorldState& w, Rect body, Vec2 delta) {
    float result = 1;
    if (delta.x > 0)
        result = std::min(result, std::max(0.f, (mapWidth * tileSize - body.x - body.w) / delta.x));
    if (delta.x < 0)
        result = std::min(result, std::max(0.f, body.x / -delta.x));
    auto test = [&](const StaticCollider& c) {
        // Horizontal carry excludes supporting surfaces; vertical carry excludes side tangencies.
        if (std::abs(delta.x) > 0 && std::abs(delta.y) < 1e-8f &&
            (c.bounds.y >= body.y + body.h - .001f || c.bounds.y + c.bounds.h <= body.y + .001f))
            return;
        if (std::abs(delta.y) > 0 && std::abs(delta.x) < 1e-8f &&
            (c.bounds.x >= body.x + body.w - .001f || c.bounds.x + c.bounds.w <= body.x + .001f))
            return;
        float t;
        if (sweepCollider(body, delta, c, t))
            result = std::min(result, t);
    };
    bool terrain = collisionAllowed(w, CollisionClass::Player, CollisionClass::Terrain);
    if (terrain) {
        int x0 = std::max(0, int(std::floor(std::min(body.x, body.x + delta.x) / tileSize)));
        int x1 =
            std::min(mapWidth - 1,
                     int(std::floor((std::max(body.x, body.x + delta.x) + body.w - .001f) / tileSize)));
        int y0 = std::max(0, int(std::floor(std::min(body.y, body.y + delta.y) / tileSize)));
        int y1 =
            std::min(mapHeight - 1,
                     int(std::floor((std::max(body.y, body.y + delta.y) + body.h - .001f) / tileSize)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                Tile tile = w.level.at(x, y);
                if (tile != Tile::Solid &&
                    !(tile == Tile::OneWay && delta.y > 0 && body.y + body.h <= y * tileSize + .1f &&
                      w.kino.dropThroughTimer == 0))
                    continue;
                StaticCollider c;
                c.bounds = {x * tileSize, y * tileSize, tileSize, tileSize};
                test(c);
            }
        for (const auto& c : w.level.colliders)
            test(c);
    }
    for (const auto& o : w.objects.slots)
        if (o.active && o.alive && o.map == w.activeMap && o.solid && o.kind != ObjectKind::Player &&
            (o.mask & 2) && collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o))) {
            StaticCollider c;
            c.bounds = objectBody(w, o);
            test(c);
        }
    for (const auto& e : w.level.events)
        if (e.solid && !e.erased &&
            collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger)) {
            StaticCollider c;
            c.bounds = e.region;
            test(c);
        }
    return result;
}
bool movingSupportContact(Rect a, Rect b, Rect previous, Rect current, float& time) {
    // A one-way moving top catches only a crossing from above, in RELATIVE motion.
    // A fast ascending platform may cross a stationary or ascending player's feet.
    float gap = previous.y - (a.y + a.h);
    float relative = b.y - a.y - (current.y - previous.y);
    if (gap < -.1f || relative <= 1e-7f || gap > relative + .1f)
        return false;
    time = std::clamp(gap / relative, 0.f, 1.f);
    float playerX = a.x + (b.x - a.x) * time;
    float platformX = previous.x + (current.x - previous.x) * time;
    return playerX + a.w > platformX + .001f && playerX < platformX + current.w - .001f;
}
void carryPlatformPlayer(WorldState& w, Rect body, Vec2 delta) {
    auto& k = w.kino;
    float t = terrainMovementFraction(w, body, delta);
    k.x += delta.x * t;
    k.y += delta.y * t;
    if (t < 1 - 1e-5f) {
        if (delta.y > 0) {
            float horizontal = terrainMovementFraction(w, body, {delta.x, 0});
            if (horizontal >= 1 - 1e-5f) {
                k.x += delta.x * (horizontal - t);
                k.grounded = false;
                k.standingPlatformId = -1;
                k.vy = 0;
                return;
            }
        }
        k.crushed = true;
        if (w.mobility.crushKills) {
            k.hp = 0;
            w.state = TimeState::Hit;
            w.gameplayClockPaused = true;
        } else {
            k.grounded = false;
            k.standingPlatformId = -1;
            k.vx = k.vy = 0;
        }
    }
}
} // namespace echo
