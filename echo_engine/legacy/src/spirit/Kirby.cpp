//
// Created by Bing on 2023/11/22.
//

#include "Kirby.hpp"

echo::spirit::Kirby::Kirby() = default;

echo::spirit::Kirby::~Kirby() = default;

echo::spirit::Kirby::Kirby(const echo::spirit::Kirby& kirby){
    *this = kirby;
}

echo::spirit::Kirby::Kirby(echo::spirit::Kirby&& kirby) noexcept {
    *this = std::move(kirby);
}

echo::spirit::Kirby& echo::spirit::Kirby::operator=(const echo::spirit::Kirby& kirby){
    if (this == &kirby)
        return *this;
    *this = kirby;
    return *this;
}

echo::spirit::Kirby& echo::spirit::Kirby::operator=(echo::spirit::Kirby&& kirby) noexcept {
    if (this == &kirby)
        return *this;
    *this = std::move(kirby);
    return *this;
}

void echo::spirit::Kirby::setTexture(const std::filesystem::path& path) {
    this->m_texture.loadFromFile(path.string());
    this->m_sprite.setTexture(this->m_texture);
    // this->m_sprite.setPosition();
}

sf::Sprite &echo::spirit::Kirby::getSprite() {
    return this->m_sprite;
}

void echo::spirit::Kirby::setTexture(sf::Texture &&tex) {
    this->m_texture = tex;
}

void echo::spirit::Kirby::setSpeed(float speed) {
    this->m_speed = speed;
}

void echo::spirit::Kirby::behavior(sf::Time& current) {
    auto& sprite { this->m_sprite };
    auto& [x, y] { this->m_sprite.getPosition() };

    float dt {current.asSeconds()};

    auto dx {this->m_speed * dt};

    bool hasKeyPressed{false};

    echo::utils::println(dt);

    using sf::Keyboard;

    if (Keyboard::isKeyPressed(Keyboard::Left)){
        sprite.setPosition(x - dx, y);
        hasKeyPressed = true;
    }if (Keyboard::isKeyPressed(Keyboard::Right)){
        sprite.setPosition(x + dx, y);
        hasKeyPressed = true;
    }if (Keyboard::isKeyPressed(Keyboard::Down)){
        sprite.setPosition(x, y);
        hasKeyPressed = true;
    }if (Keyboard::isKeyPressed(Keyboard::Up)){
        sprite.setPosition(x, y);
        hasKeyPressed = true;
    }

    this->hasKeyPressedLastTime = hasKeyPressed;
}

