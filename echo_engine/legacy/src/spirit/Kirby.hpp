//
// Created by Bing on 2023/11/22.
//

#ifndef KIRBY_HPP
#define KIRBY_HPP
#include <utility>
#include <SFML/Graphics.hpp>
#include <filesystem>
#include "SpiritBase.hpp"
#include "common/utils.hpp"

namespace echo::spirit{
    class Kirby : public SpiritBase {
    private:
        bool hasKeyPressedLastTime{false};
        sf::Texture m_texture{};
        sf::Sprite m_sprite{};
        float m_speed{250.0f};

    public:
        Kirby();
        virtual ~Kirby();
        Kirby(const Kirby&);
        Kirby(Kirby&&) noexcept ;
        Kirby& operator=(const Kirby&);
        Kirby& operator=(Kirby&&) noexcept;
        virtual void setTexture(const std::filesystem::path&);
        virtual void setTexture(sf::Texture&&);
        sf::Sprite& getSprite();
        void setSpeed(float speed);
        void behavior(sf::Time&);
    };
}

#endif // KIRBY_HPP
