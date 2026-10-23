#pragma once

#ifndef MAIN_HPP
#define MAIN_HPP
#include <core_game>
#include "spirit/Kirby.hpp"

// Legacy archive only. Media must be configured by the owning game project.
inline void configureLegacyMusic(const std::filesystem::path& path, sf::Time start, sf::Time end) {
    echo::core::global::bgMusic.setMusic(std::filesystem::path(path), sf::Time(start), sf::Time(end));
}

inline void loopDraw(sf::RenderWindow& window){
    using namespace echo::core::global;
    window.draw(bg.getSprite());
    window.draw(kirby.getSprite());
}

inline void behaviors(sf::Time& current){
    echo::core::global::kirby.behavior(current);
}

#endif // !MAIN_HPP