#pragma once
#include "world.hpp"
#include <string>
#include <vector>
namespace echo {
enum class BarrageShape : std::uint8_t { Ring, Fan, Stream, Formation };
enum class BarrageAim : std::uint8_t { Fixed, AtFire, AtWarning };
enum class BarrageOrigin : std::uint8_t { World, Player, Boss, Object };
enum class BarrageTarget : std::uint8_t { Player, Boss, Object, NearestObject, Point };
enum class BarrageFactionFilter : std::uint8_t { Any, Only, Except };
struct BarragePattern {
    std::uint64_t id{};
    Text<64> name;
    BarrageShape shape{};
    BarrageAim aim{};
    int count = 12, damage = 1;
    std::uint16_t group = 1;
    std::uint8_t palette{}, terrain{};
    float speed = 140, acceleration{}, maxSpeed = 1200, lifetime = 10;
    float direction{}, spread = 360, turnRate{}, turnSeconds{};
    float visualRadius = 6, hitRadius = 3, wobble{}, frequency = 5;
    float growth = 2, rotation{}, releaseSeconds{};
    float jitter{};
};
struct BarrageWave {
    std::uint64_t pattern{};
    int repeat = 1;
    float delay{}, warning = .85f, interval = .8f, angleStep{};
};
struct BarrageEmitter {
    std::uint64_t id{}, object{};
    Text<64> name;
    int map{}, gateSwitch = -1;
    BarrageOrigin origin{};
    Vec2 position{640, 256};
    BarrageTarget target = BarrageTarget::Player;
    BarrageFactionFilter factionFilter = BarrageFactionFilter::Any;
    std::uint64_t targetObject{};
    std::uint16_t targetFaction = 2;
    Vec2 targetPoint{640, 360};
    float targetRange = 8192; // Nearest-object query radius; zero means unlimited.
    bool excludeParent = true;
    float startDelay{}, loopDelay = 1;
    std::uint32_t seed = 1;
    bool enabled = true, loop = true, persistent{};
    std::vector<BarrageWave> waves;
};
struct BarrageLibrary {
    std::uint64_t nextId = 1;
    BulletOverflowPolicy overflow{};
    std::vector<BarragePattern> patterns;
    std::vector<BarrageEmitter> emitters;
};
// Authored map anchor; no gameplay state or pixel snapping is involved.
bool barrageEditorOrigin(const LevelState&, const BarrageEmitter&, Vec2&);
struct BarrageTargetResult {
    Vec2 position{};
    ObjectHandle handle{};
    std::uint64_t id{};
};
// Bounded, allocation-free query per emitter, never per bullet. Object faction IDs are exact values.
bool queryBarrageTarget(const WorldState&, const BarrageEmitter&, Vec2 origin, BarrageTargetResult&,
                        std::uint32_t* candidates = nullptr);
const BarragePattern* findBarragePattern(const BarrageLibrary&, std::uint64_t);
void validateBarrage(const BarrageLibrary&, int mapCount = 9);
std::uint64_t allocateBarrageId(BarrageLibrary&);
BarragePattern barragePreset(std::uint64_t id, int preset);
BarrageLibrary loadBarrage(const std::string&);
std::string saveBarrage(const BarrageLibrary&);
void resetBarrage(WorldState&, const BarrageLibrary&);
void enterBarrageMap(WorldState&, const BarrageLibrary&);
void stepBarrage(WorldState&, const BarrageLibrary&, float);
void fireBarrage(WorldState&, const BarragePattern&, Vec2, float, std::uint32_t& seed,
                 std::uint64_t source = 0);
std::uint32_t clearBulletGroup(WorldState&, std::uint16_t group);
bool setBarrageEmitter(WorldState&, std::uint64_t id, bool enabled, bool restart = false);
} // namespace echo
