#include "echo/presets.hpp"
#include <algorithm>
namespace echo {
void applyJumpPreset(WorldState& w, int index) {
    w.jump = JumpConfig{};
    if (index == 1) {
        w.jump.move_speed = 220;
        w.jump.ground_acceleration = 1500;
        w.jump.air_acceleration = 850;
        w.jump.jump_velocity = -430;
        w.jump.coyote_time = .1f;
    }
    if (index == 2) {
        w.jump.move_speed = 200;
        w.jump.ground_acceleration = 2200;
        w.jump.ground_friction = 2400;
        w.jump.air_acceleration = 1800;
        w.jump.jump_velocity = -400;
        w.jump.coyote_time = .06f;
        w.jump.jump_buffer_time = .1f;
    }
    if (index == 3) {
        w.jump.move_speed = 150;
        w.jump.gravity = 760;
        w.jump.jump_velocity = -350;
        w.jump.coyote_time = .14f;
        w.jump.jump_buffer_time = .16f;
        w.jump.fall_gravity_multiplier = 1.05f;
    }
}
void planBulletPreset(WorldState& w) {
    auto& p = w.aster.attack;
    p.end = 5;
    p.warn = .85f;
    for (int i = 0; i < 3; ++i) {
        Volley v;
        v.at = 1.1f + i * .9f;
        v.origin = {w.aster.x + 24, w.aster.y + 24};
        constexpr int patterns[] = {1, 1, 0, 2, 3, 4};
        v.pattern = std::uint8_t(patterns[std::clamp(w.bulletPreset - 1, 0, 5)]);
        v.count = std::uint16_t(w.bulletPreset == 1 ? 5 : 12);
        v.speed = w.bulletPreset == 1 ? 75 : 125;
        v.aimed = w.bulletPreset == 1 || w.bulletPreset == 2;
        v.angle = i * .4f;
        p.volleys.push(v);
    }
}
std::string_view dialoguePage(const DialogueDefinition& d, int page) {
    std::string_view text(d.text.c_str());
    std::size_t start = 0;
    for (int i = 0; i < page; ++i) {
        auto n = text.find('\n', start);
        if (n == text.npos)
            return {};
        start = n + 1;
    }
    auto n = text.find('\n', start);
    auto result = text.substr(start, n == text.npos ? text.size() - start : n - start);
    if (!result.empty() && result.back() == '\r')
        result.remove_suffix(1);
    return result;
}
std::size_t utf8Characters(std::string_view s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xc0) != 0x80)
            ++n;
    return n;
}
} // namespace echo
