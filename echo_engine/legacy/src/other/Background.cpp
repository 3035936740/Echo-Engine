//
// Created by Bing on 2023/11/22.
//

#include "Background.hpp"

void echo::other::Background::mSetSprite(){
    this->m_background_texture.loadFromFile(this->m_background_path.string());
    this->m_sprite.setTexture(this->m_background_texture);
    this->m_sprite.scale(this->m_scale_x,this->m_scale_y);
};

echo::other::Background::Background() = default;

echo::other::Background::Background(std::filesystem::path&& filename,
                                   float scale_x, float scale_y) :
    m_background_path {std::move( filename )}, m_scale_x { scale_x }, m_scale_y { scale_y }
{
    mSetSprite();
};

echo::other::Background::~Background() = default;

echo::other::Background::Background(const echo::other::Background& music){
    *this = music;
}

echo::other::Background::Background(echo::other::Background&& music) noexcept {
    *this = std::move(music);
}

echo::other::Background& echo::other::Background::operator=(const echo::other::Background& music){
    if (this == &music)
        return *this;
    *this = music;
    return *this;
}

echo::other::Background& echo::other::Background::operator=(echo::other::Background&& music) noexcept {
    if (this == &music)
        return *this;
    *this = std::move(music);
    return *this;
}

bool echo::other::Background::checkBGModify(){
    if (this->m_is_modify_bg){
        this->m_is_modify_bg = false;
        return true;
    }
    return false;
}

void echo::other::Background::setScale(float x, float y){
    this->m_scale_x = x;
    this->m_scale_y = y;
    this->m_is_modify_bg = true;
    mSetSprite();
}

void echo::other::Background::setBackground(std::filesystem::path&& filename){
    this->m_background_path = std::move(filename);
    this->m_is_modify_bg = true;
    mSetSprite();
}

std::filesystem::path& echo::other::Background::getBackground(){
    return this->m_background_path;
}

sf::Sprite& echo::other::Background::getSprite(){
    return this->m_sprite;
}