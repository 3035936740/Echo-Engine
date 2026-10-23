#pragma once
#include "world.hpp"
#include <string_view>
namespace echo {
void applyJumpPreset(WorldState&, int index);
void planBulletPreset(WorldState&);
// Each newline is a dialogue page, authored without Lua. Returns a non-owning
// UTF-8 slice; all runtime reveal/trigger/page state is inside WorldState.
std::string_view dialoguePage(const DialogueDefinition&, int page);
std::size_t utf8Characters(std::string_view);
} // namespace echo
