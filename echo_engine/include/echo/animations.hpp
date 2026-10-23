#pragma once
#include "world.hpp"
#include <vector>
#include <string>
namespace echo {
struct AnimationFrame {
    int column{}, row{};
    float seconds = .1f;
    AnimationBox collision{}, hurt{}, attack{};
    int sound = -1, damage = 1, variable = -1, delta{};
};
struct AnimationClip {
    std::uint64_t id{};
    Text<64> name;
    Text<256> sprite;
    int frameWidth = 32, frameHeight = 32;
    float scale = 4, anchorX{}, anchorY{};
    bool loop = true, flip = true;
    std::vector<AnimationFrame> frames;
};
struct AnimationBinding {
    std::uint64_t templateId{};
    // Player: Idle/Run/Jump/Fall. Boss: Idle/Walk/Cast. Other: ObjectAI.
    std::array<std::uint64_t, 6> clips{};
};
struct AnimationLibrary {
    std::uint64_t nextId = 1;
    std::vector<AnimationClip> clips;
    std::vector<AnimationBinding> bindings;
    std::array<std::uint64_t, 4> player{};
    std::array<std::uint64_t, 3> boss{};
};
const AnimationClip* findAnimation(const AnimationLibrary&, std::uint64_t);
void validateAnimations(const AnimationLibrary&);
AnimationLibrary loadAnimations(const std::string&);
std::string saveAnimations(const AnimationLibrary&);
std::uint64_t allocateAnimationId(AnimationLibrary&);
void advanceAnimation(WorldState&, AnimationPlayback&, const AnimationLibrary&, std::uint64_t, float, int actor = -1);
void stepAnimations(WorldState&, const AnimationLibrary&, float, bool combat = true);
bool requestAnimation(WorldState&, const AnimationLibrary&, ObjectHandle, std::uint64_t);
Rect animationBox(AnimationBox, Vec2 origin, float bodyWidth, int facing);
Rect kinoBody(const WorldState&);
Rect kinoHurt(const WorldState&);
Rect asterBody(const WorldState&);
} // namespace echo
