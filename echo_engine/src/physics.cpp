#include "echo/collision.hpp"
#include "echo/platform_features.hpp"
#include "echo/animations.hpp"
#include "echo/objects.hpp"
#include "echo/simulation.hpp"
#include <algorithm>
#include <cmath>
namespace echo {
bool segmentRect(Vec2 a, Vec2 b, Rect r, float* entry) {
    float near = 0, far = 1;
    for (int axis = 0; axis < 2; ++axis) {
        float p = axis ? a.y : a.x, d = axis ? b.y - a.y : b.x - a.x, min = axis ? r.y : r.x,
              max = min + (axis ? r.h : r.w);
        if (std::abs(d) < 1e-8f) {
            if (p < min || p > max)
                return false;
        } else {
            float t1 = (min - p) / d, t2 = (max - p) / d;
            if (t1 > t2)
                std::swap(t1, t2);
            near = std::max(near, t1);
            far = std::min(far, t2);
            if (near > far)
                return false;
        }
    }
    if (entry)
        *entry = near;
    return true;
}
namespace {
// Work in the current frame's collision-box coordinates, then return the sprite origin.
// Snapshots are only taken after this scope ends; subpixel coordinates remain untouched.
struct BodySpace {
    KinoState& k;
    float x, y, width, height;
    explicit BodySpace(WorldState& w) : k(w.kino) {
        Rect body = kinoBody(w);
        x = body.x - k.x;
        y = body.y - k.y;
        width = body.w;
        height = body.h;
        k.x += x;
        k.y += y;
    }
    ~BodySpace() {
        k.x -= x;
        k.y -= y;
    }
};
} // namespace
static float approach(float current, float target, float delta) {
    return current < target ? std::min(target, current + delta) : std::max(target, current - delta);
}
void refreshGroundContact(WorldState& w) {
    bool terrain = collisionAllowed(w, CollisionClass::Player, CollisionClass::Terrain);
    BodySpace space(w);
    float width = space.width, height = space.height;
    auto& k = w.kino;
    // R keeps historical jump flags and velocity, but its NEW position cannot keep
    // a historical platform attachment or coyote window at a different location.
    k.grounded = false;
    k.standingPlatformId = -1;
    k.coyoteTimer = 0;
    k.climbing = false;
    k.dashRemaining = 0;
    k.wallDirection = 0;
    updateMobilityContact(w, {k.x, k.y, width, height});
    constexpr float probe = .25f;
    if (k.vy >= 0) {
        Rect body{k.x, k.y, width, height};
        float closest = 1;
        auto test = [&](const StaticCollider& collider, int platform) {
            float hit;
            if (sweepCollider(body, {0, probe}, collider, hit) && hit <= closest) {
                closest = hit;
                k.grounded = true;
                k.standingPlatformId = platform;
            }
        };
        int x0 = std::max(0, int(std::floor(k.x / tileSize)));
        int x1 = std::min(mapWidth - 1, int(std::floor((k.x + width - .001f) / tileSize)));
        int y0 = std::max(0, int(std::floor(k.y / tileSize)));
        int y1 = std::min(mapHeight - 1, int(std::floor((k.y + height + probe) / tileSize)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                auto tile = w.level.at(x, y);
                float top = y * tileSize;
                bool oneWay =
                    tile == Tile::OneWay && k.dropThroughTimer == 0 && k.y + height <= top + .1f;
                if (terrain && (tile == Tile::Solid || oneWay)) {
                    StaticCollider collider;
                    collider.bounds = {x * tileSize, top, tileSize, tileSize};
                    test(collider, oneWay ? -(2 + y * mapWidth + x) : -1);
                }
            }
        if (terrain)
            for (const auto& f : w.level.features)
                if (featureSupports(f) && k.dropThroughTimer == 0 && k.y + height <= f.bounds.y + .1f) {
                    StaticCollider c;
                    c.bounds = f.bounds;
                    test(c, featureSupportId(f));
                }
        if (terrain)
            for (const auto& collider : w.level.colliders)
                test(collider, -1);
        for (const auto& o : w.objects.slots)
            if (o.active && o.alive && o.map == w.activeMap && o.solid && o.kind != ObjectKind::Player &&
                (o.mask & 2) && collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o))) {
                StaticCollider c;
                c.bounds = objectBody(w, o);
                test(c, -1);
            }
        for (const auto& e : w.level.events)
            if (e.solid && !e.erased &&
                collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger)) {
                StaticCollider collider;
                collider.bounds = e.region;
                test(collider, -1);
            }
        if (terrain && k.dropThroughTimer == 0)
            for (const auto& platform : w.level.platforms) {
                if (k.y + height > platform.pos.y + .1f)
                    continue;
                StaticCollider collider;
                collider.bounds = {platform.pos.x, platform.pos.y, platform.base.w, platform.base.h};
                test(collider, platform.id);
            }
        if (k.grounded) {
            k.vy = 0;
            k.coyoteTimer = w.jump.coyote_time;
        }
    }
    auto anim = k.grounded ? (std::abs(k.vx) > 1 ? KinoAnimState::Run : KinoAnimState::Idle)
                           : (k.vy < 0 ? KinoAnimState::Jump : KinoAnimState::JumpFall);
    if (anim != k.animState) {
        k.animState = anim;
        k.animTime = 0;
    }
}
void stepPhysics(WorldState& w, const Input& input, float dt) {
    if (std::abs(input.move) > .01f && w.kino.wallLockRemaining <= 0)
        w.kino.facing = input.move > 0 ? 1 : -1;
    prepareObjectPlatforms(w);
    bool terrain = collisionAllowed(w, CollisionClass::Player, CollisionClass::Terrain);
    BodySpace space(w);
    float width = space.width, height = space.height;
    auto& k = w.kino;
    auto& c = w.jump;
    float oldY = k.y;
    bool jumpPressed = input.jump && !k.jumpHeld;
    k.crushed = false;
    k.dashCooldown = std::max(0.f, k.dashCooldown - dt);
    k.wallLockRemaining = std::max(0.f, k.wallLockRemaining - dt);
    if (k.grounded)
        k.airDashAvailable = true;
    k.invulnTimer = std::max(0.0f, k.invulnTimer - dt);
    k.hitFlash = std::max(0.0f, k.hitFlash - dt);
    k.dropThroughTimer = std::max(0.0f, k.dropThroughTimer - dt);
    stepPlatformFeatures(w, dt, {k.x, k.y, width, height});
    if (terrain)
        for (const auto& f : w.level.features) {
            if (f.kind == PlatformKind::Lift || k.standingPlatformId != featureSupportId(f) ||
                !k.grounded)
                continue;
            if (!featureSupports(f)) {
                k.grounded = false;
                k.standingPlatformId = -1;
                continue;
            }
            carryPlatformPlayer(w, {k.x, k.y, width, height},
                                {f.bounds.x - f.previous.x, f.bounds.y - f.previous.y});
        }
    // Moving-platform phase, previous positions and attachment IDs are all WorldState.
    for (auto& p : w.level.platforms) {
        p.previous = p.pos;
        p.phase += dt * p.speed;
        p.pos.x = p.base.x + std::sin(p.phase) * p.amplitude;
        carryPlatformObjects(w, p.id, {p.pos.x - p.previous.x, p.pos.y - p.previous.y});
        if (terrain && k.standingPlatformId == p.id && k.grounded) {
            carryPlatformPlayer(w, {k.x, k.y, width, height},
                                {p.pos.x - p.previous.x, p.pos.y - p.previous.y});
        } else if (terrain && k.y + height > p.pos.y + .1f && k.y < p.pos.y + p.base.h - .1f) {
            float delta = p.pos.x - p.previous.x, push = 0;
            if (delta > 0 && p.previous.x + p.base.w <= k.x + .01f && p.pos.x + p.base.w > k.x)
                push = p.pos.x + p.base.w - k.x;
            if (delta < 0 && p.previous.x >= k.x + width - .01f && p.pos.x < k.x + width)
                push = p.pos.x - k.x - width;
            if (push != 0)
                carryPlatformPlayer(w, {k.x, k.y, width, height}, {push, 0});
        }
    }
    if (w.state == TimeState::Hit)
        return;
    updateMobilityContact(w, {k.x, k.y, width, height});
    auto& m = w.mobility;
    if (k.ladderId >= 0 && (input.up || input.down) && std::abs(input.move) < .01f &&
        k.dashRemaining <= 0)
        k.climbing = true;
    if (std::abs(input.move) > .01f || k.dashRemaining > 0)
        k.climbing = false;
    if (k.climbing) {
        k.grounded = false;
        k.standingPlatformId = -1;
        for (const auto& f : w.level.features)
            if (int(f.id) == k.ladderId) {
                float delta = std::clamp(f.bounds.x + f.bounds.w * .5f - width * .5f - k.x,
                                         -m.climbSpeed * dt, m.climbSpeed * dt);
                k.x += delta * terrainMovementFraction(w, {k.x, k.y, width, height}, {delta, 0});
                break;
            }
    }
    k.wallDirection = 0;
    if (m.wallJumpEnabled && !k.grounded && !k.climbing && !k.inWater && terrain) {
        if (terrainMovementFraction(w, {k.x, k.y, width, height}, {-.5f, 0}) < 1)
            k.wallDirection = -1;
        if (terrainMovementFraction(w, {k.x, k.y, width, height}, {.5f, 0}) < 1)
            k.wallDirection = 1;
    }
    if (k.grounded)
        k.coyoteTimer = c.coyote_time;
    else
        k.coyoteTimer = std::max(0.0f, k.coyoteTimer - dt);
    k.jumpBufferTimer = std::max(0.0f, k.jumpBufferTimer - dt);
    if (input.jump && !k.jumpHeld)
        k.jumpBufferTimer = c.jump_buffer_time;
    if (!input.jump && k.jumpHeld && k.vy < 0) {
        k.vy = std::max(k.vy, c.min_jump_velocity);
        k.jumpWasCut = true;
    }
    k.jumpHeld = input.jump;
    if (input.down && !w.previousDown && k.grounded && k.standingPlatformId != -1) {
        k.dropThroughTimer = .22f;
        k.grounded = false;
        k.standingPlatformId = -1;
        k.coyoteTimer = 0;
        k.y += 2;
    }
    w.previousDown = input.down;
    auto launch = [&](float vy) {
        k.vy = vy;
        k.grounded = false;
        k.standingPlatformId = -1;
        k.jumpBufferTimer = k.coyoteTimer = 0;
        k.jumpWasCut = false;
        k.climbing = false;
        ++w.soundEvent;
        w.soundKind = 1;
    };
    if (jumpPressed && k.climbing)
        launch(c.jump_velocity);
    else if (jumpPressed && k.inWater)
        launch(-m.swimJump);
    else if (m.wallJumpEnabled && k.jumpBufferTimer > 0 && k.wallDirection && k.coyoteTimer <= 0 &&
             !k.grounded && !k.inWater) {
        launch(-m.wallJumpY);
        k.vx = -k.wallDirection * m.wallJumpX;
        k.wallLockRemaining = m.wallLock;
        k.facing = -k.wallDirection;
    }
    if (k.jumpBufferTimer > 0 && k.coyoteTimer > 0 && k.dropThroughTimer == 0 && !k.climbing &&
        !k.inWater) {
        k.vy = c.jump_velocity;
        k.grounded = false;
        k.standingPlatformId = -1;
        k.coyoteTimer = 0;
        k.jumpBufferTimer = 0;
        k.jumpWasCut = false;
        ++w.soundEvent;
        w.soundKind = 1;
    }
    if (input.dash && !k.previousDash && m.dashEnabled && k.dashRemaining <= 0 && k.dashCooldown <= 0 &&
        k.airDashAvailable && !k.climbing) {
        k.dashRemaining = m.dashDuration;
        k.dashCooldown = m.dashCooldown;
        k.dashDirection = k.facing;
        k.airDashAvailable = false;
        k.jumpBufferTimer = 0;
        k.vy = 0;
    }
    k.previousDash = input.dash;
    float dashStep = std::min(dt, k.dashRemaining);
    bool dashing = dashStep > 0;
    float speed = k.inWater ? m.swimSpeed : input.down ? c.move_speed_down : c.move_speed;
    if (k.focusActive) {
        speed = std::min(speed, c.move_speed * w.focus.speedRatio);
        if (w.focus.instant)
            k.vx = std::clamp(k.vx, -speed, speed);
    }
    float accel = std::abs(input.move) > .01f ? (k.grounded ? c.ground_acceleration : c.air_acceleration)
                                              : (k.grounded ? c.ground_friction : c.air_friction);
    if (dashing)
        k.vx = k.dashDirection * m.dashSpeed;
    else if (k.wallLockRemaining <= 0)
        k.vx = approach(k.vx, std::clamp(input.move, -1.0f, 1.0f) * speed, accel * dt);
    if (k.climbing)
        k.vx = 0;
    if (std::abs(input.move) > .01f && k.wallLockRemaining <= 0)
        k.facing = input.move > 0 ? 1 : -1;
    float gravity = c.gravity;
    if (k.vy > 0)
        gravity *= c.fall_gravity_multiplier;
    else if (k.jumpWasCut || !k.jumpHeld)
        gravity *= c.low_jump_gravity_multiplier;
    else if (std::abs(k.vy) < c.apex_velocity)
        gravity *= c.apex_gravity_multiplier;
    if (k.inWater)
        gravity *= m.waterGravity;
    if (k.climbing)
        k.vy = (int(input.down) - int(input.up)) * m.climbSpeed;
    else if (dashing)
        k.vy = 0;
    else
        k.vy = std::min(k.inWater ? std::min(c.max_fall_speed, m.waterFallSpeed) : c.max_fall_speed,
                        k.vy + gravity * dt);
    if (m.wallJumpEnabled && k.wallDirection && input.move * k.wallDirection > 0 && !dashing &&
        !k.climbing && k.wallLockRemaining <= 0 && k.vy > m.wallSlideSpeed)
        k.vy = m.wallSlideSpeed;
    // Tile queries cover ONLY the player's swept AABB. No scanning the whole level.
    float conveyor = 0;
    if (terrain && k.grounded) {
        auto* f = supportFeature(w.level, k.standingPlatformId);
        if (f && f->kind == PlatformKind::Conveyor)
            conveyor = f->speed;
    }
    Rect motionStart{k.x, k.y, width, height};
    float startX = k.x, dx = k.vx * (dashing ? dashStep : dt) + conveyor * dt;
    float xHit = 1;
    int x0 = std::max(0, int(std::floor((std::min(startX, startX + dx)) / tileSize)));
    int x1 = std::min(mapWidth - 1, int((std::max(startX, startX + dx) + width) / tileSize));
    int y0 = std::max(0, int(k.y / tileSize)),
        y1 = std::min(mapHeight - 1, int((k.y + height - .001f) / tileSize));
    Rect body{startX, k.y, width, height};
    auto collideX = [&](const StaticCollider& c) {
        float hit;
        if (sweepCollider(body, {dx, 0}, c, hit))
            xHit = std::min(xHit, hit);
    };
    if (std::abs(dx) > 1e-7f) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (terrain && w.level.at(x, y) == Tile::Solid) {
                    StaticCollider c;
                    c.bounds = {x * tileSize, y * tileSize, tileSize, tileSize};
                    collideX(c);
                }
        if (terrain)
            for (auto& c : w.level.colliders)
                collideX(c);
        for (const auto& o : w.objects.slots)
            if (o.active && o.alive && o.map == w.activeMap && o.solid && o.kind != ObjectKind::Player &&
                (o.mask & 2) && collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o))) {
                StaticCollider c;
                c.bounds = objectBody(w, o);
                collideX(c);
            }
        for (const auto& e : w.level.events)
            if (e.solid && !e.erased &&
                collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger)) {
                StaticCollider c;
                c.bounds = e.region;
                collideX(c);
            }
    }
    k.x = std::clamp(startX + dx * xHit, 0.0f, mapWidth * tileSize - width);
    if (xHit < 1) {
        k.vx = 0;
        if (dashing)
            k.dashRemaining = 0;
    }
    oldY = k.y;
    float dy = k.vy * dt, yHit = 1;
    bool landed = false;
    int standing = -1;
    body = {k.x, oldY, width, height};
    x0 = std::max(0, int(k.x / tileSize));
    x1 = std::min(mapWidth - 1, int((k.x + width - .001f) / tileSize));
    y0 = std::max(0, int(std::min(oldY, oldY + dy) / tileSize));
    y1 = std::min(mapHeight - 1, int((std::max(oldY, oldY + dy) + height) / tileSize));
    auto collideY = [&](const StaticCollider& c, int platform) {
        float hit;
        if (sweepCollider(body, {0, dy == 0 ? .001f : dy}, c, hit) && hit <= yHit) {
            yHit = hit;
            landed = dy >= 0;
            standing = platform;
        }
    };
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            auto t = w.level.at(x, y);
            float top = y * tileSize;
            bool oneWay =
                t == Tile::OneWay && k.dropThroughTimer == 0 && dy >= 0 && oldY + height <= top + .1f;
            if (terrain && (t == Tile::Solid || oneWay)) {
                StaticCollider c;
                c.bounds = {x * tileSize, top, tileSize, tileSize};
                collideY(c, oneWay ? -(2 + y * mapWidth + x) : -1);
            }
        }
    if (terrain)
        for (auto& c : w.level.colliders)
            collideY(c, -1);
    for (const auto& e : w.level.events)
        if (e.solid && !e.erased &&
            collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger)) {
            StaticCollider c;
            c.bounds = e.region;
            collideY(c, -1);
        }
    for (const auto& o : w.objects.slots)
        if (o.active && o.alive && o.map == w.activeMap && o.solid && o.kind != ObjectKind::Player &&
            (o.mask & 2) && collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o))) {
            StaticCollider c;
            c.bounds = objectBody(w, o);
            collideY(c, -1);
        }
    if (terrain && dy >= 0 && k.dropThroughTimer == 0 && !k.climbing)
        for (const auto& f : w.level.features)
            if (featureSupports(f) &&
                (f.kind != PlatformKind::Lift || k.standingPlatformId == featureSupportId(f)) &&
                (oldY + height <= f.previous.y + .5f ||
                 (k.standingPlatformId == featureSupportId(f) && oldY + height <= f.bounds.y + .5f))) {
                StaticCollider feature;
                feature.bounds = f.bounds;
                collideY(feature, featureSupportId(f));
            }
    k.y = oldY + dy * yHit;
    k.grounded = landed;
    k.standingPlatformId = landed ? standing : -1;
    if (yHit < 1 || landed)
        k.vy = 0;
    if (terrain && k.vy >= 0 && k.dropThroughTimer == 0)
        for (auto& p : w.level.platforms) {
            if (oldY + height <= p.previous.y + .5f && k.y + height >= p.pos.y &&
                k.x + width > p.pos.x && k.x < p.pos.x + p.base.w) {
                k.y = p.pos.y - height;
                k.vy = 0;
                k.grounded = true;
                k.standingPlatformId = p.id;
            }
        }
    if (terrain && k.dropThroughTimer == 0 && !k.climbing && !k.crushed) {
        const PlatformFeature* caught = nullptr;
        float first = 2;
        Rect endBody{k.x, k.y, width, height};
        for (const auto& f : w.level.features) {
            if (f.kind != PlatformKind::Lift || !featureSupports(f) ||
                k.standingPlatformId == featureSupportId(f))
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
            k.grounded = true;
            k.standingPlatformId = featureSupportId(*caught);
            k.vy = 0;
            Vec2 remaining{(caught->bounds.x - caught->previous.x) * (1 - first),
                           caught->bounds.y - (k.y + height)};
            carryPlatformPlayer(w, {k.x, k.y, width, height}, remaining);
            if (k.x + width <= caught->bounds.x || k.x >= caught->bounds.x + caught->bounds.w) {
                k.grounded = false;
                k.standingPlatformId = -1;
            }
        }
    }
    if (w.state == TimeState::Hit)
        return;
    if (terrain && k.grounded)
        for (auto& f : w.level.features)
            if (f.kind == PlatformKind::Spring && featureSupportId(f) == k.standingPlatformId &&
                f.enabled && f.cooldown <= 0) {
                f.cooldown = .1f;
                launch(-f.speed);
                break;
            }
    if (k.climbing) {
        k.grounded = false;
        k.standingPlatformId = -1;
        k.coyoteTimer = 0;
    }
    if (dashing && k.dashRemaining > 0) {
        k.dashRemaining = std::max(0.f, k.dashRemaining - dt);
        if (k.dashRemaining == 0)
            k.vx = std::clamp(k.vx, -speed, speed);
    }
    updateMobilityContact(w, {k.x, k.y, width, height});
    if (k.grounded) {
        k.coyoteTimer = c.coyote_time;
        if (k.jumpBufferTimer > 0 && !input.down) {
            k.vy = c.jump_velocity;
            k.grounded = false;
            k.standingPlatformId = -1;
            k.jumpBufferTimer = 0;
            k.coyoteTimer = 0;
            k.jumpWasCut = false;
            ++w.soundEvent;
            w.soundKind = 1;
        }
    }
    KinoAnimState anim = (!k.grounded || k.vy < -1 || k.coyoteTimer <= 0)
                             ? (k.vy < 0 ? KinoAnimState::Jump : KinoAnimState::JumpFall)
                             : (std::abs(k.vx) > 1 ? KinoAnimState::Run : KinoAnimState::Idle);
    if (anim != k.animState) {
        k.animState = anim;
        k.animTime = 0;
    } else
        k.animTime += dt;
}
} // namespace echo
