#include "echo/control.hpp"
namespace echo {
std::uint16_t controlProfile(int profile, std::uint16_t custom) {
    constexpr auto actions = std::uint16_t(1 | 2 | 4 | 8 | 16);
    switch (profile) {
    case 1:
        return allPlayerControls & ~controlBit(PlayerControl::Move);
    case 2:
        return allPlayerControls & ~actions;
    case 3:
        return allPlayerControls & ~(controlBit(PlayerControl::Fast) | controlBit(PlayerControl::Trace));
    case 4:
        return allPlayerControls & ~(actions | controlBit(PlayerControl::Fast));
    case 5:
        return custom & allPlayerControls;
    default:
        return allPlayerControls;
    }
}
ControlPolicy resolveControl(const WorldState& w) {
    ControlPolicy p;
    p.history = w.rewind.enabled;
    auto restrict = [&](ControlReason reason, std::uint16_t allowed, int event = -1) {
        p.owners.push({reason, event, allowed});
        p.allowed &= allowed;
    };
    constexpr auto rescue = std::uint16_t(64 | 128);
    // Active rewind owns execution. Historical event restrictions cannot interrupt its controller.
    if (w.state == TimeState::Fast || w.state == TimeState::Trace) {
        restrict(ControlReason::Rewind, 32 | 64);
        p.physics = p.events = p.audio = p.history = false;
        return p;
    }
    // Hit always permits T rescue (when enabled), regardless of a cancelled scene's policy.
    if (w.state == TimeState::Hit) {
        restrict(ControlReason::Hit, w.rewind.enabled ? rescue : 128);
        p.physics = p.events = p.audio = p.history = false;
        return p;
    }
    if ((w.state == TimeState::Suppressed || w.state == TimeState::Escape) && w.dialogue.active < 0 &&
        w.transition.phase == TransitionPhase::Idle) {
        restrict(ControlReason::Finished, 128);
        p.physics = p.events = p.audio = p.history = false;
        return p;
    }
    for (int owner = 0; owner < 48; ++owner) {
        const auto& r = owner < 32 ? w.events.local[owner] : w.events.common[owner - 32];
        if (r.running && r.controlActive)
            restrict(ControlReason::Event, r.controlAllowed, owner);
    }
    if (w.events.foreground >= 0)
        restrict(ControlReason::Foreground, controlProfile(2), w.events.foreground);
    if (w.transition.phase != TransitionPhase::Idle) {
        restrict(ControlReason::Transition, 32 | 64);
        p.physics = p.events = false;
    } else if (w.dialogue.active >= 0) {
        restrict(ControlReason::Dialogue, 32 | 64 | 128 | 256, w.events.dialogueOwner);
        p.physics = false;
    } else if (w.gameplayClockPaused) {
        restrict(ControlReason::Paused, 32 | 64 | 128);
        p.physics = p.events = p.audio = p.history = false;
    }
    if (!w.rewind.enabled)
        p.allowed &= ~(32 | 64);
    return p;
}
void updateControlEdges(WorldState& w, std::uint16_t held, const ControlPolicy& p) {
    // Continuous movement resumes immediately. Edge actions require release after denial.
    w.controlInput.physicalHeld = held;
    constexpr std::uint16_t edges = 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256;
    w.controlInput.heldBlocked = (w.controlInput.heldBlocked | (held & ~p.allowed & edges)) & held;
}
bool controlHeld(const WorldState& w, const ControlPolicy& p, PlayerControl c, bool held) {
    return held && p.permits(c) && !(w.controlInput.heldBlocked & controlBit(c));
}
} // namespace echo
