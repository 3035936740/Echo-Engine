#include "echo/focus.hpp"
#include "echo/control.hpp"
#include "echo/simulation.hpp"
#include "echo/audio_state.hpp"
#include "echo/collision.hpp"
#include "echo/presets.hpp"
#include "echo/transition.hpp"
#include <algorithm>
#include <cmath>
namespace echo {
namespace {
std::uint16_t heldControls(const Input& i) {
    return (i.jump ? 2 : 0) | (i.down ? 4 : 0) | (i.focus ? 8 : 0) | (i.confirm ? 16 | 256 : 0) |
           (i.fast ? 32 : 0) | (i.trace ? 64 : 0) | (i.restart ? 128 : 0);
}
Input permittedInput(WorldState& w, const Input& raw) {
    auto p = resolveControl(w);
    updateControlEdges(w, heldControls(raw), p);
    if (!p.permits(PlayerControl::Jump) ||
        (w.controlInput.heldBlocked & controlBit(PlayerControl::Jump)))
        w.kino.jumpBufferTimer = 0; // denied input must not survive as a future buffered jump
    w.controlInput.dashBlocked =
        (w.controlInput.dashBlocked || !p.permits(PlayerControl::Jump)) && raw.dash;
    w.controlInput.climbBlocked =
        (w.controlInput.climbBlocked || !p.permits(PlayerControl::Move)) && raw.up;
    Input result = raw;
    result.dash = raw.dash && p.permits(PlayerControl::Jump) && !w.controlInput.dashBlocked;
    result.up = raw.up && p.permits(PlayerControl::Move) && !w.controlInput.climbBlocked;
    if (!p.permits(PlayerControl::Move))
        result.move = 0;
    result.jump = controlHeld(w, p, PlayerControl::Jump, raw.jump);
    result.down = controlHeld(w, p, PlayerControl::Down, raw.down);
    result.focus = controlHeld(w, p, PlayerControl::Focus, raw.focus);
    result.fast = controlHeld(w, p, PlayerControl::Fast, raw.fast);
    result.trace = controlHeld(w, p, PlayerControl::Trace, raw.trace);
    result.restart = controlHeld(w, p, PlayerControl::Restart, raw.restart);
    return result;
}
} // namespace
Simulation::Simulation(std::size_t budget)
    : history_(budget), world_(std::make_unique<WorldState>()),
      initial_(std::make_unique<WorldState>()) {}
void Simulation::reset(const WorldState& initial, HistoryClearReason reason) {
    *initial_ = initial;
    *world_ = initial;
    enterObjectMap(*world_, objects_);
    resetBarrage(*world_, barrage_);
    stepAnimations(*world_, animations_, 0, false);
    world_->recordingEnabled = initial.rewind.enabled;
    history_.clear(reason);
    history_.prepare(initial.rewind.enabled);
    if (hooks_)
        hooks_->afterRestore(*world_);
    record();
}
void Simulation::seekHistory(int cursor) {
    if (!world_->rewind.enabled)
        throw std::runtime_error("Rewind is disabled");
    auto generation = world_->timelineGenerationId;
    restore(history_.at(cursor));
    world_->timelineCursor = cursor;
    world_->timelineGenerationId = generation + 1;
    world_->heldRewind = {};
    world_->fast.hold = false;
    world_->rewindInputLatched = true;
    history_.truncateToCursor(cursor);
    if (hooks_)
        hooks_->beforeSnapshot(*world_);
    history_.replace(cursor, *world_);
}
void Simulation::commitRuntimeEdit() {
    if (world_->state == TimeState::Fast || world_->state == TimeState::Trace)
        throw std::runtime_error("Finish rewinding before changing debug values");
    ++world_->timelineGenerationId;
    if (hooks_)
        hooks_->beforeSnapshot(*world_);
    if (history_.size() > 0) {
        history_.truncateToCursor(world_->timelineCursor);
        if (world_->clock > history_.at(world_->timelineCursor).clock + 1e-7)
            record(); // A 30 Hz capture may lag a debugger's single 60 Hz step.
        else
            history_.replace(world_->timelineCursor, *world_);
    }
}
void Simulation::configureRewind(RewindConfig config) {
    if (!std::isfinite(config.fast_bullet_margin) || config.fast_bullet_margin < 0 ||
        config.fast_bullet_margin > 64)
        throw std::runtime_error("fast_bullet_margin must be 0..64 logical pixels");
    if (world_->state == TimeState::Fast || world_->state == TimeState::Trace)
        finish();
    config.history_seconds = std::clamp(config.history_seconds, 1.0f, 30.0f);
    if (config.record_hz != 30 && config.record_hz != 60 && config.record_hz != 120)
        config.record_hz = 60;
    config.fast_rate = std::clamp(config.fast_rate, .1f, 20.0f);
    config.trace_rate = std::clamp(config.trace_rate, .1f, 10.0f);
    bool changed = world_->rewind.enabled != config.enabled;
    world_->rewind = config;
    world_->recordingEnabled = config.enabled;
    if (changed) {
        history_.clear(config.enabled ? HistoryClearReason::Enabled : HistoryClearReason::Disabled);
        history_.prepare(config.enabled);
        world_->recordAccumulator = 0;
        if (config.enabled)
            record();
    }
}
void Simulation::record() {
    if (world_->rewind.enabled && history_.capacity() > 0) {
        if (hooks_)
            hooks_->beforeSnapshot(*world_);
        history_.record(*world_);
        world_->timelineCursor = history_.size() - 1;
    }
}
void Simulation::change(TimeState to) {
    auto from = world_->state;
    world_->state = to;
    if (hooks_ && from != to)
        hooks_->stateChange(*world_, from, to);
}
void Simulation::begin(TimeState mode) {
    auto& w = *world_;
    if (!w.rewind.enabled || history_.size() < 2)
        return;
    if (w.state == TimeState::Hit && mode == TimeState::Fast)
        return;
    if (w.state == TimeState::Escape || w.state == TimeState::Suppressed)
        return;
    w.timelineCursor = history_.size() - 1;
    double target = std::max(history_.at(0).clock, w.clock - w.rewind.history_seconds);
    int end = history_.cursorAtTime(target);
    w.heldRewind = {mode, w.state, w.timelineCursor, 0, w.clock, history_.at(end).clock};
    w.fast = {{w.kino.x, w.kino.y},
              {history_.at(end).kino.x, history_.at(end).kino.y},
              end,
              w.timelineCursor,
              float(w.clock - history_.at(end).clock),
              true,
              0,
              0};
    w.traceStartCursor = w.timelineCursor;
    w.traceTarget = end;
    w.resumeAfterRewind = TimeState::Free;
    w.gameplayClockPaused = false;
    change(mode);
}
void Simulation::finish(bool) {
    auto& w = *world_;
    auto mode = w.state;
    bool changed = w.timelineCursor < w.heldRewind.startCursor ||
                   (mode == TimeState::Fast && (w.kino.x != w.fast.A.x || w.kino.y != w.fast.A.y ||
                                                std::abs(w.clock - w.heldRewind.startTime) > 1e-7));
    if (!changed) {
        auto previous = w.heldRewind.previousState;
        change(previous);
        w.gameplayClockPaused = previous == TimeState::Hit || w.dialogue.active >= 0;
        w.heldRewind = {};
        w.fast.hold = false;
        w.rewindInputLatched = true;
        return;
    }
    history_.truncateToCursor(w.timelineCursor);
    ++w.timelineGenerationId;
    w.recordAccumulator = 0;
    if (mode == TimeState::Fast) {
        refreshGroundContact(w);
        // User policy: rewind exit never erases nearby enemy bullets.
        // Restored bullets (including px/py) remain intact on the new timeline.
        w.rewindExitGrace = .45f;
        w.kino.invulnTimer = std::max(w.kino.invulnTimer, .45f);
    }
    w.gameplayClockPaused = w.kino.hp <= 0 || w.dialogue.active >= 0;
    w.heldRewind = {};
    w.fast.hold = false;
    w.rewindInputLatched = true;
    change(w.kino.hp <= 0 ? TimeState::Hit : w.aster.hp <= 0 ? TimeState::Suppressed : TimeState::Free);
    if (hooks_)
        hooks_->beforeSnapshot(w);
    history_.replace(w.timelineCursor, w);
}
void Simulation::rewindTick(const Input& input, float dt) {
    auto& w = *world_;
    auto mode = w.state;
    float rate = mode == TimeState::Fast ? w.rewind.fast_rate : w.rewind.trace_rate;
    float elapsed = w.heldRewind.heldSeconds + dt * rate;
    double time = std::max(w.heldRewind.targetTime, w.heldRewind.startTime - elapsed);
    int cursor = history_.cursorAtTime(time);
    if (mode == TimeState::Fast && input.fast) {
        // Inspect EVERY crossed snapshot: a short-lived bullet in a skipped frame matters.
        // All transient query buffers are derived in rewindStep; no hidden persistent state.
        int previousCursor = w.timelineCursor;
        for (int frame = previousCursor - 1; frame > cursor; --frame) {
            float at = float(w.heldRewind.startTime - history_.at(frame).clock);
            rewindStep(input, at, frame);
            if (w.state != TimeState::Fast)
                return;
        }
    }
    rewindStep(input, elapsed, cursor);
}
void Simulation::rewindStep(const Input& input, float elapsed, int cursor) {
    auto& w = *world_;
    auto mode = w.state;
    if ((mode == TimeState::Fast && !input.fast) || (mode == TimeState::Trace && !input.trace)) {
        finish();
        return;
    }
    // Save the active time controller, which is itself snapshotted. Historical
    // restore is full; reapply this explicit branch controller after restore.
    auto held = w.heldRewind;
    auto fast = w.fast;
    auto generation = w.timelineGenerationId;
    int traceTarget = w.traceTarget, traceStart = w.traceStartCursor;
    Vec2 previous{w.kino.x, w.kino.y};
    Rect previousBody = kinoBody(w), oldBoss = asterBody(w);
    auto previousObjects = w.objects;
    struct BulletSample {
        std::uint32_t id;
        Vec2 position;
        float radius;
    };
    std::array<BulletSample, maxBullets> bulletSamples{};
    std::size_t sampleCount = 0;
    KinoState previousKino = w.kino;
    HurtConfig previousHurt = w.hurt;
    if (mode == TimeState::Fast) {
        for (const auto& b : w.bullets)
            bulletSamples[sampleCount++] = {b.id, {b.x, b.y}, b.hitR};
        // Bounded O(N log N), including adversarial IDs; no heap allocation or N^2 matching.
        std::sort(bulletSamples.begin(), bulletSamples.begin() + sampleCount,
                  [](const auto& a, const auto& b) { return a.id < b.id; });
    }
    held.heldSeconds = elapsed;
    double time = std::max(held.targetTime, held.startTime - held.heldSeconds);
    restore(history_.at(cursor));
    w.heldRewind = held;
    w.fast = fast;
    w.timelineGenerationId = generation;
    w.timelineCursor = cursor;
    w.traceTarget = traceTarget;
    w.traceStartCursor = traceStart;
    w.state = mode;
    w.gameplayClockPaused = false;
    if (mode == TimeState::Fast) {
        // HARD CONTRACT: world restore and Kino's NEW fixed A->B chord are separate.
        float u = fast.maxRewindSeconds > 0
                      ? std::clamp(held.heldSeconds / fast.maxRewindSeconds, 0.0f, 1.0f)
                      : 1;
        Vec2 next{fast.A.x + (fast.B.x - fast.A.x) * u, fast.A.y + (fast.B.y - fast.A.y) * u};
        auto restoredBody = kinoBody(w);
        float width = std::max(previousBody.w, restoredBody.w),
              height = std::max(previousBody.h, restoredBody.h);
        Vec2 bodyOffset{restoredBody.x - w.kino.x, restoredBody.y - w.kino.y};
        Vec2 bodyFrom{previousBody.x, previousBody.y};
        Vec2 bodyTo{next.x + bodyOffset.x, next.y + bodyOffset.y};
        float stop = 1;
        bool brake = false;
        auto testBrake = [&](const Zone& z) {
            if (z.brake && w.aster.attack.t >= z.start && w.aster.attack.t <= z.end) {
                float t;
                if (segmentRect({bodyFrom.x + width * .5f, bodyFrom.y + height * .5f},
                                {bodyTo.x + width * .5f, bodyTo.y + height * .5f},
                                {z.x - width * .5f, z.y - height * .5f, z.w + width, z.h + height},
                                &t) &&
                    t < stop) {
                    stop = t;
                    brake = true;
                }
            }
        };
        for (const auto& z : w.aster.attack.zones)
            testBrake(z);
        for (const auto& z : w.zones)
            testBrake(z);
        // Terrain and proximity guards both stop at their first swept contact.
        int minX = std::max(0, int(std::min(bodyFrom.x, bodyTo.x) / tileSize));
        int maxX = std::min(mapWidth - 1, int((std::max(bodyFrom.x, bodyTo.x) + width) / tileSize));
        int minY = std::max(0, int(std::min(bodyFrom.y, bodyTo.y) / tileSize));
        int maxY = std::min(mapHeight - 1, int((std::max(bodyFrom.y, bodyTo.y) + height) / tileSize));
        for (int ty = minY; ty <= maxY; ++ty)
            for (int tx = minX; tx <= maxX; ++tx)
                if (collisionAllowed(w, CollisionClass::Player, CollisionClass::Terrain) &&
                    w.level.at(tx, ty) == Tile::Solid) {
                    float t;
                    if (segmentRect(bodyFrom, bodyTo,
                                    {tx * tileSize - width + .05f, ty * tileSize - height + .05f,
                                     tileSize + width - .1f, tileSize + height - .1f},
                                    &t) &&
                        t < stop) {
                        stop = std::max(0.0f, t - .001f);
                        brake = true;
                    }
                }
        if (collisionAllowed(w, CollisionClass::Player, CollisionClass::Terrain))
            for (auto& collider : w.level.colliders) {
                float t;
                if (sweepCollider({bodyFrom.x, bodyFrom.y, width, height},
                                  {bodyTo.x - bodyFrom.x, bodyTo.y - bodyFrom.y}, collider, t) &&
                    t < stop) {
                    stop = std::max(0.0f, t - .001f);
                    brake = true;
                }
            }
        for (const auto& event : w.level.events)
            if (event.solid && !event.erased &&
                collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger)) {
                StaticCollider collider;
                collider.bounds = event.region;
                float t;
                if (sweepCollider({bodyFrom.x, bodyFrom.y, width, height},
                                  {bodyTo.x - bodyFrom.x, bodyTo.y - bodyFrom.y}, collider, t) &&
                    t < stop) {
                    stop = std::max(0.0f, t - .001f);
                    brake = true;
                }
            }
        for (const auto& o : w.objects.slots)
            if (o.active && o.alive && o.map == w.activeMap && o.solid && o.kind != ObjectKind::Player &&
                (o.mask & 2) && collisionAllowed(w, CollisionClass::Player, objectCollisionClass(o))) {
                StaticCollider c;
                c.bounds = objectBody(w, o);
                float t;
                if (sweepCollider({bodyFrom.x, bodyFrom.y, width, height},
                                  {bodyTo.x - bodyFrom.x, bodyTo.y - bodyFrom.y}, c, t) &&
                    t < stop) {
                    stop = std::max(0.f, t - .001f);
                    brake = true;
                }
            }
        for (auto& b : w.bullets)
            if (b.brake) {
                float t;
                if (segmentRect(bodyFrom, bodyTo,
                                {b.x - 6 - width, b.y - 6 - height, width + 12, height + 12}, &t) &&
                    t < stop) {
                    stop = t;
                    brake = true;
                }
            }
        if (w.rewind.fast_stop_near_bullets &&
            collisionAllowed(w, CollisionClass::Projectile, CollisionClass::Player)) {
            // Query against the NEW chord endpoint, never the historical Kino coordinate.
            // This restores the existing author-defined hurt shape/masks/animation geometry.
            w.kino.x = next.x;
            w.kino.y = next.y;
            for (const auto& b : w.bullets) {
                if (b.brake || b.damage <= 0)
                    continue; // amber keeps its separate body brake
                auto found = std::lower_bound(
                    bulletSamples.begin(), bulletSamples.begin() + sampleCount, b.id,
                    [](const auto& sample, std::uint32_t id) { return sample.id < id; });
                bool existed = found != bulletSamples.begin() + sampleCount && found->id == b.id;
                float radius =
                    std::max(b.hitR, existed ? found->radius : b.hitR) + w.rewind.fast_bullet_margin;
                auto test = [&](Vec2 from) {
                    float entry;
                    if (playerHurtSweep(w, previousKino, previousHurt, from, {b.x, b.y}, radius,
                                        w.hurt.projectileBox, hurtBullets, entry,
                                        &stats.collisionCandidates, true) &&
                        entry <= stop) {
                        // A bounded logical separation; render snapping never feeds this value.
                        float distance = std::hypot(next.x - previous.x, next.y - previous.y);
                        stop = std::max(0.f, entry - .001f / std::max(.001f, distance));
                        brake = true;
                    }
                };
                if (existed)
                    test(found->position); // relative bullet/player motion through time
                test({b.x, b.y});          // also keep the actual restored endpoint safe
            }
        }
        Vec2 result{previous.x + (next.x - previous.x) * stop,
                    previous.y + (next.y - previous.y) * stop};
        w.kino.x = result.x;
        w.kino.y = result.y;
        float hit = 2;
        Rect bossBody = asterBody(w);
        bool bossHit =
            collisionAllowed(w, CollisionClass::Player, CollisionClass::Enemy) && w.aster.enabled &&
            w.aster.invulnTimer <= 0 &&
            segmentRect({bodyFrom.x - oldBoss.x, bodyFrom.y - oldBoss.y},
                        {result.x + bodyOffset.x - bossBody.x, result.y + bodyOffset.y - bossBody.y},
                        {-width, -height, std::max(oldBoss.w, bossBody.w) + width,
                         std::max(oldBoss.h, bossBody.h) + height},
                        &hit);
        auto objectHit = findFastObjectImpact(
            w, {bodyFrom.x, bodyFrom.y, width, height},
            {result.x + bodyOffset.x - bodyFrom.x, result.y + bodyOffset.y - bodyFrom.y},
            &previousObjects);
        if (objectHit.entry <= 1 && (!bossHit || objectHit.entry <= hit)) {
            w.kino.x = previous.x + (result.x - previous.x) * objectHit.entry;
            w.kino.y = previous.y + (result.y - previous.y) * objectHit.entry;
            float distance = std::hypot(w.kino.x - fast.A.x, w.kino.y - fast.A.y);
            auto* enemy = resolveObject(w, objectHit.handle);
            damageObject(w, objectHit.handle, int(std::min(160.f, 40 + distance * .18f)),
                         {previous.x < enemy->x ? 545.f : -545.f, -80});
            auto& slot = w.objects.slots[objectHit.handle.slot];
            slot.invuln = 6;
            slot.knockback = .55f;
            finish(true);
            return;
        }
        if (bossHit) {
            w.kino.x = previous.x + (result.x - previous.x) * hit;
            w.kino.y = previous.y + (result.y - previous.y) * hit;
            auto& a = w.aster;
            float distance = std::hypot(w.kino.x - fast.A.x, w.kino.y - fast.A.y);
            a.hp = std::max(0, a.hp - int(std::min(160.0f, 40 + distance * .18f)));
            a.invulnTimer = 6;
            a.phaseId = std::clamp(1 + (a.maxHp - a.hp) / std::max(1, a.maxHp / 5), 1, 5);
            a.knockbackActive = true;
            a.knockbackTime = 0;
            a.knockbackDuration = .55f;
            a.knockbackStartX = a.x;
            a.knockbackTargetX =
                std::clamp(a.x + (w.kino.x < a.x ? 300 : -300), 0.0f, mapWidth * tileSize - a.width);
            a.resting = true;
            a.attackTimer = .8f;
            ++w.soundEvent;
            w.soundKind = 3;
            finish(true);
            return;
        }
        if (brake) {
            finish();
            return;
        }
    }
    if (time <= held.targetTime + 1e-6)
        finish();
}
bool Simulation::transferMap(int target, Vec2 destination, bool spawn) {
    if (target < 0 || target >= int(maps_.size())) {
        ++world_->events.errors;
        return false;
    }
    auto& w = *world_;
    w.level = maps_[target];
    w.activeMap = target;
    enterBarrageMap(w, barrage_);
    enterObjectMap(w, objects_);
    auto point = spawn ? w.level.spawn : destination;
    w.kino.x = point.x;
    w.kino.y = point.y;
    w.kino.vx = w.kino.vy = 0;
    w.kino.grounded = false;
    w.kino.standingPlatformId = -1;
    w.kino.coyoteTimer = w.kino.jumpBufferTimer = 0;
    w.bullets.clear();
    w.zones.clear();
    w.dialogue = {};
    w.gameplayClockPaused = false;
    w.aster.x = w.level.bossSpawn.x;
    w.aster.y = w.level.bossSpawn.y;
    w.aster.attack = {};
    w.aster.resting = true;
    w.aster.attackTimer = 1;
    cancelEvents(w, false);
    if (w.events.foreground < 32)
        w.events.foreground = -1;
    w.events.dialogueOwner = -1;
    w.transitionCooldown = .3f;
    ++w.timelineGenerationId;
    return true;
}
void Simulation::recordElapsed(float dt) {
    auto& w = *world_;
    if (w.rewind.enabled) {
        w.recordAccumulator += dt;
        double interval = 1.0 / w.rewind.record_hz;
        if (w.recordAccumulator + 1e-7 >= interval) {
            w.recordAccumulator -= interval;
            record();
        }
    }
}
void Simulation::requestTransfer(int target, Vec2 destination, bool spawn, const TransitionConfig& c) {
    auto& w = *world_;
    if (w.transition.phase != TransitionPhase::Idle)
        return;
    if (target < 0 || target >= int(maps_.size())) {
        ++w.events.errors;
        return;
    }
    validateTransition(c);
    if (c.kind == TransitionKind::None) {
        transferMap(target, destination, spawn);
        return;
    }
    w.transition = {};
    w.transition.config = c;
    w.transition.phase = TransitionPhase::Closing;
    w.transition.targetMap = target;
    w.transition.destination = destination;
    w.transition.useSpawn = spawn;
}
void Simulation::advanceTransition(float dt) {
    auto& w = *world_;
    // HARD CONTRACT: cover/commit/reveal is an explicit snapshotted state machine.
    // No map swap, input lock or restart completion may depend on renderer timing.
    for (int guard = 0; guard < 4 && w.transition.phase != TransitionPhase::Idle; ++guard) {
        auto& t = w.transition;
        float duration = t.phase == TransitionPhase::Closing   ? t.config.closeSeconds
                         : t.phase == TransitionPhase::Covered ? t.config.holdSeconds
                                                               : t.config.openSeconds;
        float amount = std::min(dt, std::max(0.0f, duration - t.elapsed));
        t.elapsed += amount;
        dt -= amount;
        if (t.elapsed + 1e-7f < duration)
            break;
        if (t.phase == TransitionPhase::Closing) {
            auto pending = t;
            if (pending.reason == TransitionReason::Restart) {
                bool held = w.previousRestart;
                reset(*initial_, HistoryClearReason::Restart);
                w.previousRestart = held;
            } else {
                transferMap(pending.targetMap, pending.destination, pending.useSpawn);
            }
            w.transition = pending;
            w.transition.phase = TransitionPhase::Covered;
            w.transition.elapsed = 0;
        } else if (t.phase == TransitionPhase::Covered) {
            t.phase = TransitionPhase::Opening;
            t.elapsed = 0;
        } else {
            t = {};
        }
        if (dt <= 0 && w.transition.phase != TransitionPhase::Idle) {
            const auto& n = w.transition;
            float next =
                n.phase == TransitionPhase::Covered ? n.config.holdSeconds : n.config.openSeconds;
            if (next > 0)
                break;
        }
    }
}
void Simulation::advanceEvents(const Input& input, float dt) {
    static const EventMap empty; // Immutable empty resource, never gameplay state.
    auto& w = *world_;
    const auto& local =
        w.activeMap >= 0 && w.activeMap < int(eventMaps_.size()) ? eventMaps_[w.activeMap] : empty;
    stepEvents(w, local, commonEvents_,
               controlHeld(w, resolveControl(w), PlayerControl::Interact, input.confirm), dt, hooks_);
    if (w.events.transferMap >= 0) {
        int target = w.events.transferMap;
        w.events.transferMap = -1;
        requestTransfer(target, w.events.transferPoint, w.events.transferSpawn, w.mapTransition);
    }
}
void Simulation::forward(const Input& input, float dt) {
    auto& w = *world_;
    auto previousState = w.state;
    Vec2 old{w.kino.x, w.kino.y};
    auto previousKino = w.kino;
    auto previousHurtConfig = w.hurt;
    auto previousHurt = kinoHurt(w);
    bool previousHurtBox = w.kino.animation.hurt.enabled;
    ++w.tick;
    w.clock += dt;
    w.rewindExitGrace = std::max(0.0f, w.rewindExitGrace - dt);
    w.screenShake = std::max(0.0f, w.screenShake - dt);
    advanceEvents(input, dt);
    Input action = permittedInput(w, input); // new event requests take effect in this very tick
    auto policy = resolveControl(w);
    if (w.transition.phase != TransitionPhase::Idle) {
        stepAudioState(w, dt);
        recordElapsed(dt);
        return;
    }
    if (hooks_ && policy.physics) {
        if (action.jump && !w.kino.jumpHeld)
            hooks_->key(w, "Space");
        if (action.dash && !w.kino.previousDash)
            hooks_->key(w, "Dash");
        if (action.down && !w.previousDown)
            hooks_->key(w, "Down");
    }
    stepFocus(w, action.focus, input.showHurt, policy.permits(PlayerControl::Focus));
    if (w.dialogue.active < 0) {
        stepPhysics(w, action, dt);
        if (w.state == TimeState::Hit) {
            cancelEvents(w, true);
            if (hooks_ && w.state != previousState)
                hooks_->stateChange(w, previousState, w.state);
            stepAudioState(w, dt);
            recordElapsed(dt);
            return;
        }
        stepObjects(w, objects_, dt, true);
        stepBoss(w, dt, hooks_);
        stepAnimations(w, animations_, dt);
        stepProgram(w, dt);
        stepBarrage(w, barrage_, dt);
        stepBullets(w, dt, old, stats, hooks_, &previousHurt, previousHurtBox, &previousHurtConfig,
                    &previousKino);
    }
    const auto body = kinoBody(w);
    int tx = int((body.x + body.w * .5f) / tileSize), ty = int((body.y + body.h - 1) / tileSize);
    if (w.kino.y > mapHeight * tileSize ||
        (collisionAllowed(w, CollisionClass::Player, CollisionClass::Terrain) &&
         w.level.at(tx, ty) == Tile::Spike)) {
        w.state = TimeState::Hit;
        w.gameplayClockPaused = true;
        w.kino.hp = 0;
    }
    if (w.state == TimeState::Hit)
        cancelEvents(w, true);
    if (w.state != previousState && hooks_)
        hooks_->stateChange(w, previousState, w.state);
    if (w.state != TimeState::Hit && w.dialogue.active < 0)
        for (std::size_t i = 0; i < w.level.dialogues.count; ++i) {
            auto& d = w.level.dialogues[i];
            auto r = d.region;
            bool inside = collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger) &&
                          body.x + body.w > r.x && body.x < r.x + r.w && body.y + body.h > r.y &&
                          body.y < r.y + r.h;
            bool trigger = inside && !d.entered && (!d.once || !d.triggered);
            d.entered = inside;
            if (trigger) {
                d.triggered = true;
                w.dialogue.active = int(i);
                w.dialogue.page = 0;
                w.dialogue.revealed = 0;
                w.gameplayClockPaused = true;
                break;
            }
        }
    w.transitionCooldown = std::max(0.0f, w.transitionCooldown - dt);
    if (w.state != TimeState::Hit && w.dialogue.active < 0 && w.transitionCooldown == 0) {
        for (auto& portal : w.level.portals) {
            auto r = portal.region;
            bool inside = collisionAllowed(w, CollisionClass::Player, CollisionClass::Trigger) &&
                          body.x + body.w > r.x && body.x < r.x + r.w && body.y + body.h > r.y &&
                          body.y < r.y + r.h;
            bool trigger = inside && !portal.entered;
            portal.entered = inside;
            if (trigger && portal.targetMap >= 0 && portal.targetMap < int(maps_.size())) {
                auto destination = portal.destination;
                bool spawn = portal.useSpawn;
                int target = portal.targetMap;
                auto transition = portal.transitionOverride ? portal.transition : w.mapTransition;
                requestTransfer(target, destination, spawn, transition);
                break;
            }
        }
    }
    stepAudioState(w, dt);
    if (w.rewind.enabled) {
        w.recordAccumulator += dt;
        double interval = 1.0 / w.rewind.record_hz;
        if (w.recordAccumulator + 1e-7 >= interval) {
            w.recordAccumulator -= interval;
            record();
        }
    }
}
void Simulation::tick(const Input& raw, float dt) {
    auto& w = *world_;
    stats = {};
    if (w.recordingEnabled != w.rewind.enabled) {
        history_.clear(w.rewind.enabled ? HistoryClearReason::ScriptEnabled
                                        : HistoryClearReason::ScriptDisabled);
        history_.prepare(w.rewind.enabled);
        w.recordingEnabled = w.rewind.enabled;
        w.recordAccumulator = 0;
        if (w.rewind.enabled)
            record();
    }
    if (w.state == TimeState::Fast || w.state == TimeState::Trace) {
        rewindTick(raw, dt);
        return;
    }
    if (w.state == TimeState::Hit)
        cancelEvents(w, true);
    const Input input = permittedInput(w, raw);
    bool restartPressed = input.restart && !w.previousRestart;
    w.previousRestart = input.restart;
    if (restartPressed && w.transition.phase == TransitionPhase::Idle && w.state != TimeState::Fast &&
        w.state != TimeState::Trace) {
        validateTransition(w.deathTransition);
        if (w.deathTransition.kind == TransitionKind::None) {
            reset(*initial_, HistoryClearReason::Restart);
            w.previousRestart = true;
            if (history_.size() > 0)
                history_.replace(0, w);
            return;
        }
        w.transition = {};
        w.transition.config = w.deathTransition;
        w.transition.phase = TransitionPhase::Closing;
        w.transition.reason = TransitionReason::Restart;
    }
    if (!input.fast && !input.trace)
        w.rewindInputLatched = false;
    if (!w.rewind.enabled && w.transition.phase == TransitionPhase::Idle) {
        // HARD CONTRACT: disabled Hit explicitly waits for restart; never calls begin.
        if (w.state == TimeState::Hit)
            return;
    } else if (!w.rewindInputLatched) {
        if (input.trace) {
            begin(TimeState::Trace);
            if (w.state == TimeState::Trace) {
                rewindTick(input, dt);
                return;
            }
        } else if (input.fast && w.state != TimeState::Hit) {
            begin(TimeState::Fast);
            if (w.state == TimeState::Fast) {
                rewindTick(input, dt);
                return;
            }
        }
    }
    if (w.transition.phase != TransitionPhase::Idle) {
        int substeps = w.rewind.record_hz == 120 ? 2 : 1;
        for (int i = 0; i < substeps; ++i) {
            float step = dt / substeps;
            advanceTransition(step);
            w.clock += step;
            stepAudioState(w, step);
            recordElapsed(step);
        }
        return;
    }
    if (w.dialogue.active >= 0) {
        int substeps = w.rewind.record_hz == 120 ? 2 : 1;
        float subDt = dt / substeps;
        for (int step = 0; step < substeps && w.dialogue.active >= 0; ++step) {
            advanceEvents(raw, subDt);
            permittedInput(w, raw);
            if (w.dialogue.active < 0 || w.transition.phase != TransitionPhase::Idle)
                break;
            auto& dialog = w.dialogue;
            auto& definition = activeDialogue(w);
            auto page = dialoguePage(definition, dialog.page);
            float count = float(utf8Characters(page));
            dialog.revealed = std::min(count, dialog.revealed + subDt * 30);
            if (dialog.choosing) {
                int direction = raw.move < 0 ? -1 : raw.move > 0 ? 1 : 0;
                // Movement keys navigate the modal UI only. No player physics runs here.
                if (direction && direction != dialog.previousNavigation && dialog.options.count) {
                    for (std::size_t n = 0; n < dialog.options.count; ++n) {
                        dialog.selected = (dialog.selected + direction + int(dialog.options.count)) %
                                          int(dialog.options.count);
                        if (dialog.options[dialog.selected].enabled)
                            break;
                    }
                }
                dialog.previousNavigation = direction;
            }
            if (controlHeld(w, resolveControl(w), PlayerControl::Confirm, raw.confirm) &&
                !dialog.previousConfirm) {
                if (dialog.choosing && dialog.revealed >= count) {
                    if (dialog.selected >= 0 && dialog.selected < int(dialog.options.count) &&
                        dialog.options[dialog.selected].enabled && dialog.resultVariable >= 0 &&
                        dialog.resultVariable < 64) {
                        w.events.variables[dialog.resultVariable] =
                            dialog.options[dialog.selected].result;
                        dialog.active = -1;
                        dialog.choosing = false;
                        w.gameplayClockPaused = false;
                    }
                } else if (dialog.revealed < count)
                    dialog.revealed = count;
                else {
                    ++dialog.page;
                    dialog.revealed = 0;
                    if (dialoguePage(definition, dialog.page).empty()) {
                        dialog.active = -1;
                        w.gameplayClockPaused = false;
                    }
                }
            }
            dialog.previousConfirm = raw.confirm;
            stepAudioState(w, subDt);
            w.clock += subDt;
            w.recordAccumulator += subDt;
            if (w.rewind.enabled && w.recordAccumulator + 1e-7 >= 1.0 / w.rewind.record_hz) {
                w.recordAccumulator -= 1.0 / w.rewind.record_hz;
                record();
            }
        }
        return;
    }
    if (!resolveControl(w).physics)
        return;
    // Fixed 60 Hz outer loop; 120 Hz recording uses two REAL simulation substeps.
    int steps = w.rewind.record_hz == 120 ? 2 : 1;
    for (int i = 0; i < steps; ++i) {
        forward(raw, dt / steps);
        if (w.gameplayClockPaused)
            break;
    }
}
} // namespace echo
