#pragma once
#include "world.hpp"
#include <string>
namespace echo {
// Exact circle-vs-AABB sweep and convex SAT sweep. Broad phase is swept AABB;
// narrow phase is bounded by 64 authored shapes / 16 vertices, with no heap work.
bool sweepCollider(Rect box, Vec2 delta, const StaticCollider&, float& entry);
int colliderVertices(const StaticCollider&, std::array<Vec2, 16>&);
bool collisionAllowed(const WorldState&, CollisionClass, CollisionClass) noexcept;
void setCollisionPair(CollisionRules&, CollisionClass, CollisionClass, bool) noexcept;
CollisionClass objectCollisionClass(const ObjectState&) noexcept;
std::string writeCollisionRules(const CollisionRules&);
CollisionRules readCollisionRules(std::string_view);
// Solid event routes stop or slide at first contact. No push/crush response is implied.
Vec2 moveEventBody(WorldState&, int eventId, Rect body, Vec2 delta, bool& blocked);
} // namespace echo
