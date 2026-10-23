#pragma once
#include "animation_state.hpp"
#include <array>
#include <cstdint>
namespace echo {
inline constexpr int maxObjects = 256, maxObjectPlacements = 64;
enum class ObjectKind : std::uint8_t { Player, Enemy, NPC, Mechanism, Pickup, Decoration, Boss };
enum class ObjectBehavior : std::uint8_t { Idle, Patrol, Chase, Turret, Door, Collect };
enum class ObjectAI : std::uint8_t { Idle, Patrol, Chase, Attack, Return, Dead };
enum ObjectOverride : std::uint32_t {
    OverrideSpeed = 1,
    OverrideHP = 2,
    OverrideSize = 4,
    OverrideDamage = 8
};
struct ObjectHandle {
    std::uint16_t slot = 65535;
    std::uint32_t generation{};
};
struct ObjectPlacement {
    std::uint64_t id{}, templateId{};
    float x{}, y{}, speed = 80, width = 28, height = 32;
    int hp = 3, damage = 1, eventId{};
    std::uint32_t overrides{};
    bool persistent = true;
};
struct ObjectState {
    AnimationPlayback animation{};
    std::uint64_t id{}, placementId{}, templateId{};
    std::uint32_t generation = 1;
    int map{}, hp = 3, maxHP = 3, damage = 1, eventId{}, pickupVariable = -1, pickupValue = 1;
    ObjectKind kind = ObjectKind::Decoration;
    ObjectBehavior behavior = ObjectBehavior::Idle;
    ObjectAI ai = ObjectAI::Idle;
    float x{}, y{}, px{}, py{}, vx{}, vy{}, width = 28, height = 32, speed = 80, homeX{}, homeY{};
    float patrolRange = 96, detectRange = 240, attackInterval = 1.2f, timer{}, invuln{}, animTime{},
          knockback{};
    std::uint16_t category = 4, mask = 3, faction = 2;
    int facing = 1;
    bool active{}, alive = true, grounded{}, gravity{}, solid{}, persistent = true, entered{};
    // HARD CONTRACT: support attachment, behavior policy and launch/crush state rewind together.
    int standingPlatformId = -1;
    float springCooldown{};
    bool ridesPlatforms = true, crushKills = true, crushed{};
    bool operator==(const ObjectState&) const = default;
};
struct ObjectWorld {
    std::array<ObjectState, maxObjects> slots{};
    std::array<bool, 8> visited{};
    std::uint64_t nextId = 1;
    std::uint32_t rejected{}, invalidReferences{};
    bool operator==(const ObjectWorld&) const = default;
};
} // namespace echo
