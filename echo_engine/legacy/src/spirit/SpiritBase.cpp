//
// Created by Bing on 2023/11/29.
//

#include "SpiritBase.hpp"

echo::spirit::SpiritBase::SpiritBase() = default;
echo::spirit::SpiritBase::SpiritBase(const echo::spirit::SpiritBase &left) {
    this->collisionBox = left.collisionBox;
    this->collisionBoxCenterPoint = left.collisionBoxCenterPoint;
    this->collisionBoxType = left.collisionBoxType;
}

echo::spirit::SpiritBase::SpiritBase(echo::spirit::SpiritBase &&right) noexcept {
    *this = std::move(right);
}

echo::spirit::SpiritBase &echo::spirit::SpiritBase::operator=(const echo::spirit::SpiritBase &left) {
    if (this == &left)
        return *this;
    this->collisionBox = left.collisionBox;
    this->collisionBoxCenterPoint = left.collisionBoxCenterPoint;
    this->collisionBoxType = left.collisionBoxType;
    return *this;
}

echo::spirit::SpiritBase &echo::spirit::SpiritBase::operator=(echo::spirit::SpiritBase &&right) noexcept {
    if (this == &right)
        return *this;
    *this = std::move(right);
    return *this;
}

void echo::spirit::SpiritBase::setCollisionBoxCenterPoint(float offsetX, float offsetY) {
    this->collisionBoxCenterPoint.x = offsetX;
    this->collisionBoxCenterPoint.y = offsetY;
}

void echo::spirit::SpiritBase::setCollisionBoxType(echo::spirit::CollisionBoxType type) {
    this->collisionBoxType = type;
}

echo::spirit::SpiritBase::~SpiritBase() = default;
