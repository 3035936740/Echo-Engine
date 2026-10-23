#pragma once
#include "world.hpp"
#include <algorithm>
#include <cmath>
namespace echo {
enum class SnapMode { Crisp, Smooth };
struct CameraState {
    float logicalX{}, logicalY{}, targetX{}, targetY{}, smoothTime = .18f;
    int pixelGridX{}, pixelGridY{};
    float pixelSnapStrength = 1, deadzoneW = 96, deadzoneH = 40;
    SnapMode snapMode = SnapMode::Crisp;
    float snapThreshold = .55f;
    float renderX{}, renderY{};
    int previousMap = -1;
    std::uint64_t previousGeneration = ~0ull; // visual-only branch tracking
    void update(const WorldState& w, float dt, bool instant = false) {
        const auto& k = w.kino;
        float desiredX = k.x + k.width / 2 - 640.0f + k.facing * 64, desiredY = k.y - 400.0f;
        if (w.level.cameraClamp) {
            desiredX = std::clamp(desiredX, 0.0f, mapWidth * tileSize - 1280);
            desiredY = std::clamp(desiredY, 0.0f, mapHeight * tileSize - 720);
        }
        if (previousGeneration != w.timelineGenerationId) {
            instant = true; // a guarded R can finish in the same outer tick; no camera tail
            previousGeneration = w.timelineGenerationId;
        }
        if (previousMap != w.activeMap) {
            instant = true;
            previousMap = w.activeMap;
        }
        bool rewinding = w.state == TimeState::Fast || w.state == TimeState::Trace;
        if (w.transition.phase != TransitionPhase::Idle)
            instant = true;
        if (instant || rewinding) {
            logicalX = targetX = desiredX;
            logicalY = targetY = desiredY;
        } else {
            if (std::abs(desiredX - targetX) > deadzoneW / 2)
                targetX = desiredX - std::copysign(deadzoneW / 2, desiredX - targetX);
            if (std::abs(desiredY - targetY) > deadzoneH / 2)
                targetY = desiredY - std::copysign(deadzoneH / 2, desiredY - targetY);
            float a = 1 - std::exp(-dt / std::max(.001f, smoothTime));
            logicalX += (targetX - logicalX) * a;
            logicalY += (targetY - logicalY) * a;
        }
        if (instant || rewinding || std::abs(logicalX - renderX) * pixelsPerUnit > snapThreshold)
            pixelGridX = static_cast<int>(std::round(logicalX * pixelsPerUnit));
        if (instant || rewinding || std::abs(logicalY - renderY) * pixelsPerUnit > snapThreshold)
            pixelGridY = static_cast<int>(std::round(logicalY * pixelsPerUnit));
        renderX = pixelGridX / pixelsPerUnit;
        renderY = pixelGridY / pixelsPerUnit;
    }
    // HARD CONTRACT: floor only at submission, NEVER write snapped coords to WorldState.
    Vec2 project(float x, float y) const {
        return {std::floor((x - renderX) * pixelsPerUnit), std::floor((y - renderY) * pixelsPerUnit)};
    }
};
} // namespace echo
