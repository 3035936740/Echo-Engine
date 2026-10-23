#pragma once
#include "world.hpp"
#include <string>
namespace echo {
void validateMobility(const MobilityConfig&);
std::string writeMobility(const MobilityConfig&);
MobilityConfig readMobility(const std::string&);
void validateFeature(const PlatformFeature&);
void validateFeatures(const LevelState&);
PlatformFeature platformPreset(PlatformKind, Rect, std::uint32_t id);
int featureSupportId(const PlatformFeature&);
bool featureSupports(const PlatformFeature&);
const PlatformFeature* supportFeature(const LevelState&, int);
void stepPlatformFeatures(WorldState&, float, Rect playerBody);
void updateMobilityContact(WorldState&, Rect);
float terrainMovementFraction(const WorldState&, Rect, Vec2);
void carryPlatformPlayer(WorldState&, Rect, Vec2);
bool movingSupportContact(Rect playerStart, Rect playerEnd, Rect previous, Rect current, float& time);
} // namespace echo
