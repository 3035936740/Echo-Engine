//
// Created by Bing on 2023/11/22.
//

#ifndef BACKGROUND_HPP
#define BACKGROUND_HPP
#include <utility>
#include <filesystem>
#include <utility>
#include <stdexcept>
#include <SFML/Graphics.hpp>

namespace echo::other {
    class Background {
    private:
        sf::Texture m_background_texture{};
        sf::Sprite m_sprite{};
        bool m_is_modify_bg{false};
        std::filesystem::path m_background_path{};
        float m_scale_x{1.0f}, m_scale_y{1.0f};
        void mSetSprite();
    public:
        Background();

        explicit Background(std::filesystem::path&& filename, float scale_x = 1.0f, float scale_y = 1.0f);

        virtual ~Background();

        Background(const Background &);

        Background(Background &&) noexcept;

        virtual Background &operator=(const Background &);

        virtual Background &operator=(Background &&) noexcept;

        bool checkBGModify();

        void setBackground(std::filesystem::path&&);

        void setScale(float x, float y);

        std::filesystem::path& getBackground();

        sf::Sprite& getSprite();
    };
}

#endif //BACKGROUND_HPP
