//
// Created by Bing on 2023/11/29.
//
#pragma once

#ifndef SPIRITBASE_HPP
#define SPIRITBASE_HPP
#include <array>
namespace echo::spirit {
    enum class CollisionBoxType : std::size_t {
        rectangle,
        circle
    };

    struct CollisionBox{
        float width; // 矩形碰撞箱的宽
        float height; // 矩形碰撞箱的高
        float radius; // 圆形碰撞箱的半径
    };

    class SpiritBase {
    private:
        struct Position {
            float x;
            float y;
        };
    protected:
        CollisionBoxType collisionBoxType { CollisionBoxType::rectangle }; // 碰撞箱的类型
        CollisionBox collisionBox{}; // 碰撞箱的设置(宽高/半径)

        Position collisionBoxCenterPoint{};
    public:
        SpiritBase();
        SpiritBase(const SpiritBase&);
        SpiritBase(SpiritBase&&) noexcept ;
        SpiritBase& operator=(const SpiritBase&);
        SpiritBase& operator=(SpiritBase&&) noexcept;
        virtual ~SpiritBase();
        virtual void setCollisionBoxCenterPoint(float offsetX, float offsetY);
        virtual void setCollisionBoxType(CollisionBoxType type);
    };
};

#endif //SPIRITBASE_HPP
