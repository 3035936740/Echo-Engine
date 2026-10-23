#pragma once
#include "world.hpp"
#include <vector>
#include <string>
namespace echo {
struct ObjectTemplate {
    std::uint64_t id = 1;
    Text<64> name;
    Text<256> sprite;
    ObjectKind kind = ObjectKind::Enemy;
    ObjectBehavior behavior = ObjectBehavior::Patrol;
    float width = 28, height = 32, speed = 80, patrolRange = 96, detectRange = 240,
          attackInterval = 1.2f;
    int hp = 3, damage = 1, pickupVariable = -1, pickupValue = 1;
    std::uint16_t category = 4, mask = 3, faction = 2;
    bool gravity = true, solid{}, ridesPlatforms = true, crushKills = true;
};
struct ObjectLibrary {
    std::vector<ObjectTemplate> templates;
    std::uint64_t nextId = 1;
};
std::uint64_t allocateObjectTemplateId(ObjectLibrary&);
ObjectLibrary loadObjects(const std::string&);
std::string saveObjects(const ObjectLibrary&);
void validateObjects(const ObjectLibrary&);
const ObjectTemplate* findObjectTemplate(const ObjectLibrary&, std::uint64_t);
ObjectTemplate objectPreset(std::uint64_t, int);
ObjectHandle createObject(WorldState&, const ObjectTemplate&, Vec2, int map,
                          std::uint64_t placementId = 0);
bool destroyObject(WorldState&, ObjectHandle);
ObjectState* resolveObject(WorldState&, ObjectHandle);
const ObjectState* resolveObject(const WorldState&, ObjectHandle);
void enterObjectMap(WorldState&, const ObjectLibrary&);
void stepObjects(WorldState&, const ObjectLibrary&, float, bool platformStepPrepared = false);
void prepareObjectPlatforms(WorldState&);
void carryPlatformObjects(WorldState&, int supportId, Vec2 delta);
bool objectPlatformRider(const WorldState&, int supportId);
void damageObject(WorldState&, ObjectHandle, int, Vec2 knockback = {});
struct ObjectImpact {
    ObjectHandle handle{};
    float entry = 2;
};
ObjectImpact findFastObjectImpact(const WorldState&, Rect from, Vec2 motion,
                                  const ObjectWorld* previous = nullptr);
bool fastImpactObjects(WorldState&, Vec2 from, Vec2 to);
Rect objectBody(const WorldState&, const ObjectState&);
} // namespace echo
