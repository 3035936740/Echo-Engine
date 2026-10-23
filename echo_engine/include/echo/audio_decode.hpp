#pragma once
#include "resources.hpp"
namespace echo {
struct PcmClip {
    std::vector<std::int16_t> samples;
    double seconds() const {
        return samples.size() / 88200.0;
    }
};
PcmClip decodeAudio(const Bytes&, std::size_t maxBytes = 64ull * 1024 * 1024);
} // namespace echo
