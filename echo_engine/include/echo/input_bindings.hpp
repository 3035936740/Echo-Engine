#pragma once
#include "simulation.hpp"
#include <array>
#include <string>
#include <string_view>
namespace echo {
// Physical keyboard mapping is immutable project configuration during a simulation.
// Logical held/blocked input and dialogue edges remain in WorldState snapshots.
struct KeyBindings {
    std::array<int, 8> keys{65, 68, 32, 40, 69, 82, 84, 13};
    bool arrowMovement = true;
};
enum class KeyAction { Left, Right, Jump, Down, Confirm, Fast, Trace, Restart };
void validateKeys(const KeyBindings&);
std::string writeKeys(const KeyBindings&);
KeyBindings readKeys(std::string_view);
Input keyboardInput(const KeyBindings&, const std::array<bool, 256>& held, int focusKey, int hurtKey,
                    int dashKey = 88, int climbKey = 87);
} // namespace echo
