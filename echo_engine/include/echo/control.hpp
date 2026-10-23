#pragma once
#include "world.hpp"
namespace echo {
enum class PlayerControl : std::uint16_t {
    Move = 1,
    Jump = 2,
    Down = 4,
    Focus = 8,
    Interact = 16,
    Fast = 32,
    Trace = 64,
    Restart = 128,
    Confirm = 256
};
inline constexpr std::uint16_t controlBit(PlayerControl c) {
    return std::uint16_t(c);
}
enum class ControlReason : std::uint8_t {
    Event,
    Foreground,
    Dialogue,
    Transition,
    Hit,
    Rewind,
    Paused,
    Finished
};
struct ControlOwner {
    ControlReason reason{};
    int event = -1;
    std::uint16_t allowed = allPlayerControls;
};
// Derived solely from snapshot truth; diagnostics must never become a second controller.
struct ControlPolicy {
    std::uint16_t allowed = allPlayerControls;
    FixedList<ControlOwner, 56> owners;
    bool physics = true, events = true, audio = true, history = true;
    bool permits(PlayerControl c) const {
        return (allowed & controlBit(c)) != 0;
    }
};
std::uint16_t controlProfile(int profile, std::uint16_t custom = allPlayerControls);
ControlPolicy resolveControl(const WorldState&);
void updateControlEdges(WorldState&, std::uint16_t held, const ControlPolicy&);
bool controlHeld(const WorldState&, const ControlPolicy&, PlayerControl, bool held);
} // namespace echo
