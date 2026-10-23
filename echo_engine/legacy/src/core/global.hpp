#pragma once

#ifndef GLOBAL_HPP
#define GLOBAL_HPP

#include <filesystem>
#include <SFML/Graphics.hpp>
#include "SFML/System/Time.hpp"
#include "other/BackgroundMusic.hpp"
#include "other/Background.hpp"
#include "spirit/Kirby.hpp"
#include "common/utils/FramePerSecond.hpp"

using namespace std::chrono_literals;
using namespace echo::utils;
using namespace echo::utils::fps_literals;

namespace echo::core::global {
    constexpr cstring INIT_BG_PATH {"data/bg/rainbow_route_1.png\0"};
    constexpr cstring INIT_KIRBY_PATH {"data/spirit/kirby.png\0"};

    inline sf::Time currentTime{ sf::milliseconds(0) };
    sf::Clock clock;

    const inline std::shared_ptr<std::mutex> mutex;

    inline echo::other::BackgroundMusic bgMusic;
    inline echo::utils::FramePerSecond framePerSecond(30_fps, mutex);
    inline echo::spirit::Kirby kirby;
    inline echo::other::Background bg(INIT_BG_PATH, 3.0f, 3.0f);
}
#endif // !GLOBAL_HPP
