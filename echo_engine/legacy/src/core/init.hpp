//
// Created by Bing on 2023/11/21.
//
#pragma once

#ifndef INIT_HPP
#define INIT_HPP

namespace echo::core::init {
    [[maybe_unused]] const bool loadLeadTexture{[]() {
        using namespace echo::core::global;
        // global::lead.loadFromFile("data/spirit/kirby.png");
        // global::player.setTexture(global::lead);
        // global::player.setTexture(sf::IntRect());

        kirby.setTexture(INIT_KIRBY_PATH);

        auto &kirby_spirit{kirby.getSprite()};
        kirby_spirit.setTextureRect(sf::IntRect(4, 4, 24, 27));

        //kirby_spirit.setOrigin(10,20);

        kirby_spirit.setScale(2.0f, 2.0f);

        kirby_spirit.setPosition(10, 60);

        return true;
    }()};
}

#endif //INIT_HPP
