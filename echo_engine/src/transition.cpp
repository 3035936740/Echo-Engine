#include "echo/transition.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace echo {
void validateTransition(const TransitionConfig& c) {
    if (int(c.kind) > 7 || int(c.ease) > 3 || c.direction > 3)
        throw std::runtime_error("Invalid transition type/easing/direction");
    for (float t : {c.closeSeconds, c.holdSeconds, c.openSeconds})
        if (!std::isfinite(t) || t < 0 || t > 10)
            throw std::runtime_error("Transition durations must be 0..10 seconds");
    for (float channel : c.color)
        if (!std::isfinite(channel) || channel < 0 || channel > 1)
            throw std::runtime_error("Transition color must be 0..1");
    if (!std::isfinite(c.detail) || c.detail < 1 || c.detail > 64)
        throw std::runtime_error("Transition detail must be 1..64");
}
TransitionConfig readTransition(const std::string& text) {
    TransitionConfig c;
    int kind = -1, ease = -1, dir = -1, center = -1;
    std::istringstream s(text);
    s >> kind >> ease >> dir >> center >> c.closeSeconds >> c.holdSeconds >> c.openSeconds >>
        c.color[0] >> c.color[1] >> c.color[2] >> c.detail;
    if (!s || kind < 0 || kind > 7 || ease < 0 || ease > 3 || dir < 0 || dir > 3 ||
        (center != 0 && center != 1))
        throw std::runtime_error("Malformed transition config");
    s >> std::ws;
    if (!s.eof())
        throw std::runtime_error("Unexpected transition config data");
    c.kind = TransitionKind(kind);
    c.ease = TransitionEase(ease);
    c.direction = std::uint8_t(dir);
    c.playerCenter = center != 0;
    validateTransition(c);
    return c;
}
std::string writeTransition(const TransitionConfig& c) {
    validateTransition(c);
    std::ostringstream s;
    s.precision(9);
    s << int(c.kind) << ' ' << int(c.ease) << ' ' << int(c.direction) << ' ' << c.playerCenter << ' '
      << c.closeSeconds << ' ' << c.holdSeconds << ' ' << c.openSeconds;
    for (float channel : c.color)
        s << ' ' << channel;
    s << ' ' << c.detail;
    return s.str();
}
float transitionCover(const TransitionState& t) {
    if (t.phase == TransitionPhase::Idle || t.config.kind == TransitionKind::None)
        return 0;
    if (t.phase == TransitionPhase::Covered)
        return 1;
    float duration = t.phase == TransitionPhase::Closing ? t.config.closeSeconds : t.config.openSeconds;
    float u = duration > 0 ? std::clamp(t.elapsed / duration, 0.0f, 1.0f) : 1;
    switch (t.config.ease) {
    case TransitionEase::Smooth:
        u = u * u * (3 - 2 * u);
        break;
    case TransitionEase::In:
        u = u * u;
        break;
    case TransitionEase::Out:
        u = 1 - (1 - u) * (1 - u);
        break;
    default:
        break;
    }
    return t.phase == TransitionPhase::Closing ? u : 1 - u;
}
} // namespace echo
