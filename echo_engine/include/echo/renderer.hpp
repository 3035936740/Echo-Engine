#pragma once
#include "camera.hpp"
#include "animations.hpp"
#include "barrage.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <windows.h>
namespace echo {
class Resources;
class Renderer {
    struct Impl;
    std::unique_ptr<Impl> p_;

  public:
    bool backgroundEnabled = true, fluid = false, raymarch = false, sunlight = true, fireflies = true;
    int textureFilter{}, upscaleFilter{}, antiAliasing{};
    float renderSharpness = .25f;
    int environment = 3, weather = 0;
    bool editorDark{};
    std::array<float, 3> editorBackdrop{.933f, .941f, .957f}; // matches the editor chrome theme
    bool editorGrid = true, editorCollisions = true, debugCollisions = false, selectionVisible = false;
    std::array<bool, 3> editorHidden{};
    Rect selectionRect{};
    float weatherStrength = 0, wind = .3f, fog = .04f;
    int scale = 1, offsetX{}, offsetY{}, drawCalls{};
    Renderer();
    ~Renderer();
    void initialize(HWND, Resources* resources = nullptr);
    void resize(int w, int h);
    void draw(const WorldState&, const CameraState&, bool editing = false, int tool = 1,
              const BarrageLibrary* emitters = nullptr, std::uint64_t selectedEmitter = 0);
    void setBackgroundSource(const std::string&);
    void setTransitionSource(const std::string&);
    void capture(const std::filesystem::path&);
    bool setVsync(bool);
    void captureDisplay(const std::filesystem::path&);
    void setDialogueOverlay(const void* bgra, int width, int height, int x, int y);
    void hideDialogueOverlay();
    void setResources(Resources*);
    void setSampling(int texture, int upscale, int aa, float sharpness);
    void setAnimations(const AnimationLibrary&);
};
} // namespace echo
