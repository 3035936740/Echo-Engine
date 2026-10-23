#pragma once
#include "world.hpp"
#include "input_bindings.hpp"
#include "assets.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace echo {
using Bytes = std::vector<std::uint8_t>;
struct Clip {
    int row{}, first{}, count = 1;
    float fps = 8;
};
struct SpriteConfig {
    std::string path;
    int frame = 48;
    float width = 192, height = 192, offsetX = -16, offsetY = -16; // 48 texels at 4 logical px/texel
    std::array<Clip, 4> clips{};
};
struct ProjectConfig {
    KeyBindings keys{};
    MobilityConfig mobility{};
    CollisionRules collisions{};
    HurtConfig hurt{};
    FocusConfig focus{};
    int textureFilter = 0, upscaleFilter = 0, antiAliasing = 0;
    float renderSharpness = .25f;
    int frameLimit = 60;
    bool vsync = true;
    std::string title = "Echo Engine", level = "levels/start.echo",
                backgroundShader = "shaders/environment.frag";
    std::vector<std::string> scripts, maps;
    SpriteConfig player{}, boss{};
    TransitionConfig mapTransition{}, deathTransition{};
    std::string transitionShader;
    std::string lightingShader = "shaders/lighting.frag", fluidShader = "shaders/fluid.frag",
                raymarchShader = "shaders/raymarch.frag";
    std::string cameraSnap = "crisp";
    float cameraSmooth = .18f, cameraThreshold = .55f, cameraDeadzoneW = 96, cameraDeadzoneH = 40;
    int environment = 3, weather = 0;
    float weatherStrength = 0, wind = .3f, fog = .08f;
    std::string tileSprite;
    int tileFrame = 8;
    bool fluid = false, raymarch = false, sunlight = true, fireflies = true;
    std::size_t historyBudgetMB = 512;
    std::string music;
    float loopStart{}, loopEnd{};
    bool musicEnabled = false, musicLoop = true;
    float musicVolume = .7f, seVolume = .8f, fadeIn = 0;
    std::array<std::string, 32> effects;
};
class Resources {
    std::map<std::string, Bytes> embedded_, editorOverlay_;
    std::filesystem::path overlayRoot_;
    mutable AssetCatalog assets_;
    mutable std::filesystem::path assetsRoot_;
    mutable bool assetsLoaded_{};

  public:
    std::filesystem::path root;
    bool published{};
    // Recovery metadata is editor-only and never becomes hidden gameplay state.
    void setEditorOverlay(const std::map<std::string, std::string>&);
    void clearEditorOverlay();
    void invalidateAssetCatalog() {
        assetsLoaded_ = false;
    }
    const AssetCatalog& assetCatalog() const;
    void setAssetCatalog(const AssetCatalog&);
    std::string resolve(std::string_view name) const;
    bool contains(std::string_view name) const;
    Bytes read(std::string_view name) const;
    std::string text(std::string_view name) const;
    bool openEmbedded(const std::filesystem::path& exe);
    void exportGame(const std::filesystem::path& runtime, const std::filesystem::path& output,
                    bool encrypt) const;
    ProjectConfig config() const;
};
std::filesystem::path pathFromUtf8(std::string_view);
std::string pathUtf8(const std::filesystem::path&);
void writeTextAtomically(const std::filesystem::path&, std::string_view);
LevelState loadLevel(const std::string& text);
std::string saveLevel(const LevelState&);
} // namespace echo
