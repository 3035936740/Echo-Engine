#pragma once
#include <array>
#include <cstdint>
namespace echo {
// Every marker cursor and gameplay box is restored; rendering never owns playback.
struct AnimationBox {
    float x{}, y{}, w{}, h{};
    bool enabled{};
    bool operator==(const AnimationBox&) const = default;
};
struct AnimationPlayback {
    std::uint64_t clip{}, requested{}, strike{};
    std::uint32_t frame{}, loops{};
    float elapsed{};
    bool started{}, finished{};
    AnimationBox collision{}, hurt{}, attack{};
    int damage{};
    bool operator==(const AnimationPlayback&) const = default;
};
struct AnimationHit {
    std::uint64_t strike{}, target{};
    bool operator==(const AnimationHit&) const = default;
};
struct AnimationWorld {
    std::array<AnimationHit, 1024> hits{};
    std::uint64_t nextStrike = 1;
    std::uint32_t capacityRejected{}, invalidClips{};
    bool operator==(const AnimationWorld&) const = default;
};
} // namespace echo
