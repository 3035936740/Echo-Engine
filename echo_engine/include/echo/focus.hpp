#pragma once
#include "world.hpp"
#include <string>
namespace echo {
void validateHurt(const HurtConfig&);
void validateFocus(const FocusConfig&);
std::string writeHurt(const HurtConfig&);
HurtConfig readHurt(const std::string&);
std::string writeFocus(const FocusConfig&);
FocusConfig readFocus(const std::string&);
void stepFocus(WorldState&, bool held, bool show, bool controlAllowed);
bool showHurt(const WorldState&);
HurtRegion effectiveHurt(const WorldState&, std::uint32_t);
HurtRegion effectiveHurt(const KinoState&, const HurtConfig&, std::uint32_t);
Vec2 hurtCenter(const KinoState&, const HurtConfig&, const HurtRegion&);
Rect hurtBounds(const KinoState&, const HurtConfig&, const HurtRegion&);
// Exact circle/capsule/convex polygon sweeps; ellipse is a documented 32-edge approximation.
FixedList<Vec2, 32> hurtPolygon(const KinoState&, const HurtConfig&, const HurtRegion&);
bool playerHurtSweep(const WorldState&, const KinoState&, const HurtConfig&, Vec2 from, Vec2 to,
                     float radius, bool box, std::uint8_t mask, float& entry,
                     std::uint32_t* candidates = nullptr, bool proximity = false);
bool playerHurtRect(const WorldState&, Rect attack, std::uint8_t mask);
} // namespace echo
