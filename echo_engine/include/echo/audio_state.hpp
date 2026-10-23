#pragma once
#include "world.hpp"
namespace echo {
void stepAudioState(WorldState&, float dt);
bool playSound(WorldState&, int clip, float gain = 1, float pitch = 1);
void fadeMusic(WorldState&, float target, float seconds);
void startMusic(WorldState&, bool restart = true);
void stopMusic(WorldState&);
} // namespace echo
