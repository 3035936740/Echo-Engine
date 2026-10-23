#include "echo/focus.hpp"
#include "echo/collision.hpp"
#include <windows.h>
#include "echo/resources.hpp"
#include "echo/platform_features.hpp"
#include "echo/transition.hpp"
#include <algorithm>
#include <bcrypt.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <monocypher.h>
#include <sstream>
#include <stdexcept>
namespace echo {
static constexpr std::size_t maxArchive = 256ull * 1024 * 1024;
std::filesystem::path pathFromUtf8(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
std::string pathUtf8(const std::filesystem::path& path) {
    auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}
void writeTextAtomically(const std::filesystem::path& path, std::string_view text) {
    auto temporary = path;
    temporary += L".echo-tmp-" + std::to_wstring(GetCurrentProcessId());
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), std::streamsize(text.size()));
    stream.flush();
    if (!stream) {
        stream.close();
        std::filesystem::remove(temporary);
        throw std::runtime_error("Atomic save write failed");
    }
    stream.close();
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Atomic save replace failed");
    }
}
static std::string safeName(std::string_view s) {
    if (s.find('\0') != std::string_view::npos || s.find(':') != std::string_view::npos)
        throw std::runtime_error("Invalid resource path characters");
    auto p = pathFromUtf8(s);
    if (p.empty() || p.is_absolute() || p.has_root_name())
        throw std::runtime_error("Invalid resource path");
    for (const auto& part : p)
        if (part == ".." || part == ".")
            throw std::runtime_error("Resource path traversal rejected");
    return pathUtf8(p);
}
static Bytes readFile(const std::filesystem::path& p, std::size_t max = maxArchive) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Cannot read resource: " + pathUtf8(p));
    auto n = f.tellg();
    if (n < 0 || std::uint64_t(n) > max)
        throw std::runtime_error("Resource exceeds budget");
    Bytes b(static_cast<std::size_t>(n));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(b.data()), n);
    if (!f)
        throw std::runtime_error("Resource read failed");
    return b;
}
void Resources::setEditorOverlay(const std::map<std::string, std::string>& files) {
    if (published)
        throw std::runtime_error("Recovery is editor-only");
    std::map<std::string, Bytes> next;
    std::size_t total = 0;
    for (const auto& [name, value] : files) {
        auto key = safeName(name);
        total += key.size() + value.size();
        if (total > 32ull * 1024 * 1024 || files.size() > 32)
            throw std::runtime_error("Recovery metadata budget exceeded");
        next.emplace(key, Bytes(value.begin(), value.end()));
    }
    editorOverlay_ = std::move(next);
    assetsLoaded_ = false;
    overlayRoot_ = std::filesystem::weakly_canonical(root);
}
void Resources::clearEditorOverlay() {
    editorOverlay_.clear();
    assetsLoaded_ = false;
    overlayRoot_.clear();
}
const AssetCatalog& Resources::assetCatalog() const {
    if (!assetsLoaded_ || assetsRoot_ != root) {
        assets_ = contains("assets.echo") ? loadAssets(text("assets.echo")) : AssetCatalog{};
        assetsLoaded_ = true;
        assetsRoot_ = root;
    }
    return assets_;
}
void Resources::setAssetCatalog(const AssetCatalog& c) {
    validateAssets(c);
    assets_ = c;
    assetsRoot_ = root;
    assetsLoaded_ = true;
}
std::string Resources::resolve(std::string_view name) const {
    if (name.starts_with('@')) {
        auto* asset = findAsset(assetCatalog(), assetId(name));
        if (!asset)
            throw std::runtime_error("Unknown resource identity: " + std::string(name));
        return safeName(asset->path);
    }
    return safeName(name);
}
Bytes Resources::read(std::string_view name) const {
    auto key = resolve(name);
    if (!published && overlayRoot_ == std::filesystem::weakly_canonical(root)) {
        if (auto found = editorOverlay_.find(key); found != editorOverlay_.end())
            return found->second;
    }
    if (published && !embedded_.empty()) {
        auto i = embedded_.find(key);
        if (i == embedded_.end())
            throw std::runtime_error("Missing packaged resource: " + key);
        return i->second;
    }
    auto resolved = std::filesystem::weakly_canonical(root / pathFromUtf8(key));
    auto relative = resolved.lexically_relative(std::filesystem::weakly_canonical(root));
    if (relative.empty() || *relative.begin() == "..")
        throw std::runtime_error("Resource escaped project");
    return readFile(resolved);
}
std::string Resources::text(std::string_view name) const {
    auto b = read(name);
    return {b.begin(), b.end()};
}
#pragma pack(push, 1)
struct Footer {
    char magic[8] = {'E', 'C', 'H', 'O', 'P', 'A', 'K', '1'};
    std::uint64_t size{};
    std::array<std::uint8_t, 24> nonce{};
    std::array<std::uint8_t, 32> key{};
    std::array<std::uint8_t, 16> mac{};
};
#pragma pack(pop)
static void append32(Bytes& b, std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
        b.push_back(std::uint8_t(n >> (8 * i)));
}
static std::uint32_t take32(const Bytes& b, std::size_t& cursor) {
    if (cursor + 4 > b.size())
        throw std::runtime_error("Invalid package length");
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i)
        n |= std::uint32_t(b[cursor++]) << (8 * i);
    return n;
}
bool Resources::openEmbedded(const std::filesystem::path& exe) {
    invalidateAssetCatalog();
    std::ifstream f(exe, std::ios::binary | std::ios::ate);
    if (!f)
        return false;
    auto total = f.tellg();
    if (total < std::streamoff(sizeof(Footer)))
        return false;
    Footer footer;
    f.seekg(total - std::streamoff(sizeof(footer)));
    f.read(reinterpret_cast<char*>(&footer), sizeof(footer));
    if (std::memcmp(footer.magic, "ECHOPAK1", 8) != 0)
        return false;
    if (footer.size > maxArchive || footer.size > std::uint64_t(total) - sizeof(footer))
        throw std::runtime_error("Invalid package size");
    Bytes payload(footer.size);
    f.seekg(total - std::streamoff(sizeof(footer)) - std::streamoff(footer.size));
    f.read(reinterpret_cast<char*>(payload.data()), payload.size());
    if (!f)
        throw std::runtime_error("Truncated package");
    int bad = crypto_aead_unlock(
        payload.data(), footer.mac.data(), footer.key.data(), footer.nonce.data(),
        reinterpret_cast<const std::uint8_t*>(footer.magic), 16, payload.data(), payload.size());
    crypto_wipe(footer.key.data(), footer.key.size());
    if (bad)
        throw std::runtime_error("Resource authentication failed: package changed");
    std::map<std::string, Bytes> result;
    std::size_t pos = 0;
    auto count = take32(payload, pos);
    if (count > 4096)
        throw std::runtime_error("Too many resources");
    for (std::uint32_t i = 0; i < count; ++i) {
        auto nameSize = take32(payload, pos), size = take32(payload, pos);
        if (nameSize == 0 || nameSize > 1024 || pos + nameSize + std::uint64_t(size) > payload.size())
            throw std::runtime_error("Invalid resource entry");
        std::string name(reinterpret_cast<char*>(payload.data() + pos), nameSize);
        pos += nameSize;
        safeName(name);
        if (result.contains(name))
            throw std::runtime_error("Duplicate package resource");
        result.emplace(name, Bytes(payload.begin() + pos, payload.begin() + pos + size));
        pos += size;
    }
    if (pos != payload.size())
        throw std::runtime_error("Trailing package data");
    crypto_wipe(payload.data(), payload.size());
    embedded_ = std::move(result);
    published = true;
    return true;
}
void Resources::exportGame(const std::filesystem::path& runtime, const std::filesystem::path& output,
                           bool encrypt) const {
    if (published)
        throw std::runtime_error("Published games cannot export themselves");
    if (!editorOverlay_.empty() && overlayRoot_ == std::filesystem::weakly_canonical(root))
        throw std::runtime_error("Save recovered metadata before export");
    if (std::filesystem::exists(output))
        throw std::runtime_error("Choose a new export folder; destination already exists");
    auto rootAbs = std::filesystem::weakly_canonical(root),
         outAbs = std::filesystem::weakly_canonical(output);
    auto relation = outAbs.lexically_relative(rootAbs);
    if (!relation.empty() && *relation.begin() != "..")
        throw std::runtime_error("Export outside the project resource folder");
    std::map<std::string, Bytes> files;
    std::size_t total = 4;
    for (auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        auto relative = entry.path().lexically_relative(root);
        if (!relative.empty() && *relative.begin() == ".echo")
            continue;
        if (entry.is_symlink())
            throw std::runtime_error("Resource symlinks are not supported");
        if (!entry.is_regular_file())
            continue;
        std::string name = safeName(pathUtf8(entry.path().lexically_relative(root)));
        auto data = read(name);
        total += 8 + name.size() + data.size();
        if (total > maxArchive || files.size() >= 4096)
            throw std::runtime_error("Export resource budget exceeded");
        files.emplace(std::move(name), std::move(data));
    }
    std::filesystem::create_directories(output);
    auto notices = runtime.parent_path() / "THIRD_PARTY_NOTICES.txt";
    if (std::filesystem::exists(notices))
        std::filesystem::copy_file(notices, output / "THIRD_PARTY_NOTICES.txt");
    auto target = output / runtime.filename();
    std::filesystem::copy_file(runtime, target);
    if (!encrypt) {
        std::filesystem::create_directory(output / "game");
        for (auto& [name, data] : files) {
            auto dest = output / "game" / pathFromUtf8(name);
            std::filesystem::create_directories(dest.parent_path());
            std::ofstream f(dest, std::ios::binary);
            f.write(reinterpret_cast<const char*>(data.data()), data.size());
            if (!f)
                throw std::runtime_error("Public export failed");
        }
        return;
    }
    Bytes payload;
    payload.reserve(total);
    append32(payload, files.size());
    for (auto& [name, data] : files) {
        append32(payload, name.size());
        append32(payload, data.size());
        payload.insert(payload.end(), name.begin(), name.end());
        payload.insert(payload.end(), data.begin(), data.end());
    }
    Footer footer;
    footer.size = payload.size();
    if (BCryptGenRandom(nullptr, footer.key.data(), footer.key.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) <
            0 ||
        BCryptGenRandom(nullptr, footer.nonce.data(), footer.nonce.size(),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Windows CSPRNG failed");
    // XChaCha20-Poly1305, fresh key + 192-bit nonce on EVERY export, metadata authenticated.
    // Offline runtimes necessarily carry decrypt capability. This is resource encryption,
    // NOT an anti-reverse-engineering guarantee. Never invent a custom crypto algorithm.
    crypto_aead_lock(payload.data(), footer.mac.data(), footer.key.data(), footer.nonce.data(),
                     reinterpret_cast<const std::uint8_t*>(footer.magic), 16, payload.data(),
                     payload.size());
    std::ofstream f(target, std::ios::binary | std::ios::app);
    f.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    f.write(reinterpret_cast<const char*>(&footer), sizeof(footer));
    if (!f)
        throw std::runtime_error("Encrypted export failed");
    crypto_wipe(footer.key.data(), footer.key.size());
}
ProjectConfig Resources::config() const {
    ProjectConfig c;
    std::map<std::string, std::string> fields;
    std::istringstream stream(text("project.echo"));
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        auto split = line.find('=');
        if (split != std::string::npos)
            fields[line.substr(0, split)] = line.substr(split + 1);
    }
    auto str = [&](const char* key, std::string fallback) {
        auto i = fields.find(key);
        return i == fields.end() ? fallback : i->second;
    };
    auto num = [&](const char* key, float value) {
        auto i = fields.find(key);
        float n = i == fields.end() ? value : std::stof(i->second);
        if (!std::isfinite(n))
            throw std::runtime_error("Non-finite project setting");
        return n;
    };
    c.collisions = readCollisionRules(str("collision_rules", writeCollisionRules(c.collisions)));
    c.hurt = readHurt(str("player_hurt", writeHurt(c.hurt)));
    c.focus = readFocus(str("player_focus", writeFocus(c.focus)));
    auto integerSetting = [&](const char* key, int fallback, int maximum) {
        auto i = fields.find(key);
        if (i == fields.end())
            return fallback;
        std::size_t used = 0;
        auto value = std::stoll(i->second, &used);
        if (value < 0 || value > maximum ||
            i->second.find_first_not_of(" \t", used) != std::string::npos)
            throw std::runtime_error(std::string("Invalid integer project setting: ") + key);
        return int(value);
    };
    auto mode = [&](const char* key, int maximum) { return integerSetting(key, 0, maximum); };
    c.textureFilter = mode("texture_filter", 2);
    c.upscaleFilter = mode("upscale_filter", 5);
    c.antiAliasing = mode("anti_aliasing", 4);
    c.renderSharpness = num("render_sharpness", .25f);
    if (c.renderSharpness < 0 || c.renderSharpness > 1)
        throw std::runtime_error("Sharpness 0..1");
    c.frameLimit = integerSetting("frame_limit", 60, 1000);
    c.vsync = integerSetting("vsync", 1, 1) != 0;
    c.keys = readKeys(str("keyboard_bindings", writeKeys(c.keys)));
    c.mobility = readMobility(str("player_mobility", writeMobility(c.mobility)));
    c.title = str("title", c.title);
    c.level = str("level", c.level);
    c.backgroundShader = str("background_shader", c.backgroundShader);
    c.lightingShader = str("lighting_shader", c.lightingShader);
    c.fluidShader = str("fluid_shader", c.fluidShader);
    c.raymarchShader = str("raymarch_shader", c.raymarchShader);
    std::istringstream scripts(str("scripts", ""));
    while (std::getline(scripts, line, ';'))
        if (!line.empty())
            c.scripts.push_back(safeName(line));
    std::istringstream maps(str("maps", c.level));
    while (std::getline(maps, line, ';'))
        if (!line.empty())
            c.maps.push_back(safeName(line));
    if (c.maps.empty() || c.maps.size() > 8 ||
        std::find(c.maps.begin(), c.maps.end(), c.level) == c.maps.end())
        throw std::runtime_error("Project supports 1..8 maps");
    if (c.focus.map >= int(c.maps.size()))
        throw std::runtime_error("Focus condition references a missing map");
    c.mapTransition = readTransition(str("map_transition", writeTransition(c.mapTransition)));
    c.deathTransition = readTransition(str("death_transition", writeTransition(c.deathTransition)));
    c.transitionShader = str("transition_shader", "");
    if (!c.transitionShader.empty())
        safeName(c.transitionShader);
    if ((c.mapTransition.kind == TransitionKind::Custom ||
         c.deathTransition.kind == TransitionKind::Custom) &&
        c.transitionShader.empty())
        throw std::runtime_error("Custom transitions require transition_shader");
    c.cameraSnap = str("camera_snap_mode", "crisp");
    if (c.cameraSnap != "crisp" && c.cameraSnap != "smooth")
        throw std::runtime_error("Camera mode must be crisp/smooth");
    c.cameraSmooth = std::clamp(num("camera_smooth_time", .18f), .001f, 2.0f);
    c.cameraThreshold = std::clamp(num("camera_snap_threshold", .55f), .05f, 2.0f);
    c.cameraDeadzoneW = std::clamp(num("camera_deadzone_w", 96), 0.0f, 640.0f);
    c.cameraDeadzoneH = std::clamp(num("camera_deadzone_h", 40), 0.0f, 360.0f);
    c.environment = int(std::clamp(num("environment", 3), 0.0f, 6.0f));
    c.weather = int(std::clamp(num("weather", 0), 0.0f, 2.0f));
    c.weatherStrength = num("weather_strength", 0);
    c.wind = num("wind", .3f);
    c.fog = num("fog", .08f);
    c.tileSprite = str("tile_sprite", "");
    c.tileFrame = std::clamp(int(num("tile_frame", 8)), 1, 256);
    c.sunlight = num("sunlight", 1) != 0;
    c.fireflies = num("fireflies", 1) != 0;
    c.fluid = num("fluid", 0) != 0;
    c.raymarch = num("raymarch", 0) != 0;
    c.historyBudgetMB = std::size_t(std::clamp(num("history_budget_mb", 512), 16.0f, 2048.0f));
    c.music = str("music", "");
    c.loopStart = num("music_loop_start", 0);
    c.loopEnd = num("music_loop_end", 0);
    c.musicEnabled = !c.music.empty();
    c.musicLoop = num("music_loop", 1) != 0;
    c.musicVolume = std::clamp(num("music_volume", .7f), 0.0f, 1.0f);
    c.seVolume = std::clamp(num("se_volume", .8f), 0.0f, 1.0f);
    c.fadeIn = std::max(0.0f, num("music_fade_in", 0));
    for (int i = 0; i < 32; ++i)
        c.effects[i] = str(("se_" + std::to_string(i + 1)).c_str(), "");
    auto sprite = [&](const char* prefix, SpriteConfig& s) {
        std::string p(prefix);
        s.path = str((p + "_sprite").c_str(), "");
        s.frame = int(num((p + "_frame").c_str(), 48));
        s.width = num((p + "_width").c_str(), 64);
        s.height = num((p + "_height").c_str(), 64);
        s.offsetX = num((p + "_offset_x").c_str(), -16);
        s.offsetY = num((p + "_offset_y").c_str(), -16);
        const char* names[] = {"idle", "run", "jump", "fall"};
        for (int i = 0; i < 4; ++i) {
            auto v = str((p + "_" + names[i]).c_str(), "0,0,1,8");
            std::replace(v.begin(), v.end(), ',', ' ');
            std::istringstream ss(v);
            ss >> s.clips[i].row >> s.clips[i].first >> s.clips[i].count >> s.clips[i].fps;
            s.clips[i].count = std::max(1, s.clips[i].count);
        }
    };
    sprite("player", c.player);
    sprite("boss", c.boss);
    return c;
}
bool Resources::contains(std::string_view name) const {
    auto key = resolve(name);
    if (!published && overlayRoot_ == std::filesystem::weakly_canonical(root) &&
        editorOverlay_.contains(key))
        return true;
    if (published && !embedded_.empty())
        return embedded_.contains(key);
    return std::filesystem::is_regular_file(root / pathFromUtf8(key));
}
LevelState loadLevel(const std::string& text) {
    LevelState level;
    std::istringstream s(text);
    std::string magic;
    s >> magic;
    if (magic != "ECHO_LEVEL_1" && magic != "ECHO_LEVEL_2" && magic != "ECHO_LEVEL_3" &&
        magic != "ECHO_LEVEL_4" && magic != "ECHO_LEVEL_5" && magic != "ECHO_LEVEL_6" &&
        magic != "ECHO_LEVEL_7" && magic != "ECHO_LEVEL_8" && magic != "ECHO_LEVEL_9" &&
        magic != "ECHO_LEVEL_10" && magic != "ECHO_LEVEL_11")
        throw std::runtime_error("Unknown level format");
    s >> level.background >> level.spawn.x >> level.spawn.y >> level.bossSpawn.x >> level.bossSpawn.y;
    for (std::size_t i = 0; i < level.tiles.size(); ++i) {
        int n = -1;
        s >> n;
        if (n < 0 || n > 4)
            throw std::runtime_error("Invalid tile");
        level.tiles[i] = Tile(n);
        if (n)
            level.layers[1].cells[i] = 1;
    }
    int count = -1;
    s >> count;
    if (count < 0 || count > 16)
        throw std::runtime_error("Invalid platform count");
    for (int i = 0; i < count; ++i) {
        MovingPlatform p;
        s >> p.base.x >> p.base.y >> p.base.w >> p.base.h >> p.amplitude >> p.speed;
        p.pos = p.previous = {p.base.x, p.base.y};
        p.id = i;
        level.platforms.push(p);
    }
    if (magic == "ECHO_LEVEL_2" || magic == "ECHO_LEVEL_3" || magic == "ECHO_LEVEL_4" ||
        magic == "ECHO_LEVEL_5" || magic == "ECHO_LEVEL_6" || magic == "ECHO_LEVEL_7" ||
        magic == "ECHO_LEVEL_8" ||
        (magic == "ECHO_LEVEL_9" || (magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11"))) {
        for (auto& layer : level.layers) {
            int visible = 1;
            s >> layer.parallax >> visible;
            layer.visible = visible != 0;
            if (!std::isfinite(layer.parallax) || layer.parallax < 0 || layer.parallax > 2)
                throw std::runtime_error("Invalid parallax");
            for (auto& cell : layer.cells) {
                int n = -1;
                s >> n;
                if (n < 0 || n > 16)
                    throw std::runtime_error("Invalid tile atlas index");
                cell = std::uint8_t(n);
            }
        }
        s >> count;
        if (count < 0 || count > 64)
            throw std::runtime_error("Invalid collider count");
        for (int i = 0; i < count; ++i) {
            StaticCollider c;
            int kind = -1, n = -1;
            s >> kind >> c.bounds.x >> c.bounds.y >> c.bounds.w >> c.bounds.h >> n;
            if (kind < 0 || kind > 4 || n < 0 || n > 16 || !std::isfinite(c.bounds.x) ||
                !std::isfinite(c.bounds.y) || !std::isfinite(c.bounds.w) || !std::isfinite(c.bounds.h) ||
                c.bounds.w <= 0 || c.bounds.h <= 0)
                throw std::runtime_error("Invalid collider");
            c.kind = ColliderKind(kind);
            for (int j = 0; j < n; ++j) {
                Vec2 p;
                s >> p.x >> p.y;
                if (!std::isfinite(p.x) || !std::isfinite(p.y))
                    throw std::runtime_error("Invalid polygon");
                c.points.push(p);
            }
            if (c.kind == ColliderKind::Polygon && n < 3)
                throw std::runtime_error("Polygon needs 3 vertices");
            level.colliders.push(c);
        }
        s >> count;
        if (count < 0 || count > 32)
            throw std::runtime_error("Invalid light count");
        for (int i = 0; i < count; ++i) {
            SceneLight lamp;
            s >> lamp.pos.x >> lamp.pos.y >> lamp.radius >> lamp.intensity >> lamp.warmColor.x >>
                lamp.warmColor.y >> lamp.blue >> lamp.flicker;
            if (!std::isfinite(lamp.pos.x) || !std::isfinite(lamp.pos.y) ||
                !std::isfinite(lamp.radius) || !std::isfinite(lamp.intensity) || lamp.radius <= 0 ||
                lamp.radius > 2048 || lamp.intensity < 0 || lamp.intensity > 8)
                throw std::runtime_error("Invalid light");
            level.lights.push(lamp);
        }
    }
    if (magic == "ECHO_LEVEL_3" || magic == "ECHO_LEVEL_4" || magic == "ECHO_LEVEL_5" ||
        magic == "ECHO_LEVEL_6" || magic == "ECHO_LEVEL_7" || magic == "ECHO_LEVEL_8" ||
        (magic == "ECHO_LEVEL_9" || (magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11"))) {
        s >> count;
        if (count < 0 || count > 8)
            throw std::runtime_error("Dialogue capacity: 8");
        for (int i = 0; i < count; ++i) {
            DialogueDefinition d;
            std::string speaker, portrait, content;
            int once = 1;
            s >> d.region.x >> d.region.y >> d.region.w >> d.region.h >> once >> std::quoted(speaker) >>
                std::quoted(portrait) >> std::quoted(content);
            if (speaker.size() >= 64 || portrait.size() >= 256 || content.size() >= 1024 ||
                !std::isfinite(d.region.x) || !std::isfinite(d.region.y) || d.region.w <= 0 ||
                d.region.h <= 0)
                throw std::runtime_error("Invalid dialogue definition");
            d.speaker.set(speaker);
            d.portrait.set(portrait);
            d.text.set(content);
            d.once = once != 0;
            level.dialogues.push(d);
        }
    }
    if (magic == "ECHO_LEVEL_4" || magic == "ECHO_LEVEL_5" || magic == "ECHO_LEVEL_6" ||
        magic == "ECHO_LEVEL_7" || magic == "ECHO_LEVEL_8" ||
        (magic == "ECHO_LEVEL_9" || (magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11"))) {
        int clamp = 1;
        s >> clamp >> count;
        level.cameraClamp = clamp != 0;
        if (count < 0 || count > 16)
            throw std::runtime_error("Portal capacity: 16");
        for (int i = 0; i < count; ++i) {
            MapPortal p;
            int spawn = 1;
            s >> p.region.x >> p.region.y >> p.region.w >> p.region.h >> p.targetMap >>
                p.destination.x >> p.destination.y >> spawn;
            if (!std::isfinite(p.region.x) || !std::isfinite(p.region.y) || p.region.w <= 0 ||
                p.region.h <= 0 || p.targetMap < 0 || p.targetMap > 7)
                throw std::runtime_error("Invalid map portal");
            p.useSpawn = spawn != 0;
            level.portals.push(p);
        }
    }
    if (magic == "ECHO_LEVEL_5" || magic == "ECHO_LEVEL_6" || magic == "ECHO_LEVEL_7" ||
        magic == "ECHO_LEVEL_8" ||
        (magic == "ECHO_LEVEL_9" || (magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11"))) {
        for (auto& layer : level.layers) {
            std::string image;
            s >> std::quoted(image) >> layer.imageBounds.x >> layer.imageBounds.y >>
                layer.imageBounds.w >> layer.imageBounds.h;
            if (image.size() >= 256 || !std::isfinite(layer.imageBounds.x) ||
                !std::isfinite(layer.imageBounds.y) || !std::isfinite(layer.imageBounds.w) ||
                !std::isfinite(layer.imageBounds.h) || layer.imageBounds.w <= 0 ||
                layer.imageBounds.h <= 0)
                throw std::runtime_error("Invalid layer image");
            if (!image.empty())
                safeName(image);
            layer.image.set(image);
        }
    }
    if (magic == "ECHO_LEVEL_6" || magic == "ECHO_LEVEL_7" || magic == "ECHO_LEVEL_8" ||
        (magic == "ECHO_LEVEL_9" || (magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11"))) {
        s >> count;
        if (count < 0 || count > 32)
            throw std::runtime_error("Event placement capacity: 32");
        std::array<bool, 33> ids{};
        for (int i = 0; i < count; ++i) {
            EventPlacement event;
            s >> event.id >> event.region.x >> event.region.y >> event.region.w >> event.region.h;
            if (event.id < 1 || event.id > 32 || ids[event.id] || !std::isfinite(event.region.x) ||
                !std::isfinite(event.region.y) || !std::isfinite(event.region.w) ||
                !std::isfinite(event.region.h) || event.region.w <= 0 || event.region.h <= 0)
                throw std::runtime_error("Invalid event placement");
            ids[event.id] = true;
            level.events.push(event);
        }
    }
    if (magic == "ECHO_LEVEL_7" || magic == "ECHO_LEVEL_8" ||
        (magic == "ECHO_LEVEL_9" || (magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11"))) {
        for (auto& p : level.portals) {
            int override = -1;
            std::string value;
            s >> override >> std::quoted(value);
            if (override != 0 && override != 1)
                throw std::runtime_error("Invalid portal transition override");
            p.transitionOverride = override != 0;
            p.transition = readTransition(value);
        }
    }
    if (magic == "ECHO_LEVEL_8" ||
        (magic == "ECHO_LEVEL_9" || (magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11"))) {
        s >> count;
        if (count < 0 || count > int(maxObjectPlacements))
            throw std::runtime_error("Object placement capacity: 64");
        for (int i = 0; i < count; ++i) {
            ObjectPlacement p;
            int persistent = -1;
            s >> p.id >> p.templateId >> p.x >> p.y >> p.speed >> p.width >> p.height >> p.hp >>
                p.damage >> p.eventId >> p.overrides >> persistent;
            if (!s || !p.id || p.id >= UINT64_MAX - 1 || !p.templateId || !std::isfinite(p.x) ||
                !std::isfinite(p.y) || !std::isfinite(p.speed) || p.speed < 0 || p.speed > 10000 ||
                !std::isfinite(p.width) || !std::isfinite(p.height) || p.width <= 0 || p.height <= 0 ||
                p.width > 2048 || p.height > 2048 || p.hp < 1 || p.hp > 1000000 || p.damage < 0 ||
                p.damage > 1000000 || p.eventId < 0 || p.eventId > 32 || p.overrides > 15 ||
                (persistent != 0 && persistent != 1))
                throw std::runtime_error("Invalid object placement");
            for (const auto& prior : level.objects)
                if (prior.id == p.id)
                    throw std::runtime_error("Duplicate object placement ID");
            p.persistent = persistent != 0;
            level.objects.push(p);
        }
    }
    for (const auto& p : level.objects)
        level.nextObjectId = std::max(level.nextObjectId, p.id + 1);
    if ((magic == "ECHO_LEVEL_9" || (magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11"))) {
        std::uint64_t next;
        if (!(s >> next) || next < level.nextObjectId)
            throw std::runtime_error("Invalid object ID allocator");
        level.nextObjectId = next;
    }
    if ((magic == "ECHO_LEVEL_10" || magic == "ECHO_LEVEL_11")) {
        std::string marker;
        int n = -1;
        s >> marker >> level.nextFeatureId >> n;
        if (marker != "PLATFORM_FEATURES" || n < 0 || n > 64)
            throw std::runtime_error("Invalid platform feature section");
        for (int i = 0; i < n; ++i) {
            PlatformFeature f;
            int kind = -1, enabled = -1;
            s >> f.id >> kind >> f.base.x >> f.base.y >> f.base.w >> f.base.h >> f.speed >> f.delay >>
                f.resetSeconds >> enabled;
            if (kind < 0 || kind > (magic == "ECHO_LEVEL_11" ? 5 : 4) || enabled < 0 || enabled > 1)
                throw std::runtime_error("Invalid feature kind/enable");
            if (magic == "ECHO_LEVEL_11") {
                int repeat = -1;
                s >> f.travel.x >> f.travel.y >> repeat;
                if (repeat < 0 || repeat > 1)
                    throw std::runtime_error("Invalid platform route repeat");
                f.repeat = repeat != 0;
            }
            f.kind = PlatformKind(kind);
            f.enabled = enabled != 0;
            f.bounds = f.base;
            f.previous = {f.base.x, f.base.y};
            level.features.push(f);
        }
        validateFeatures(level);
    }
    bool parsed = bool(s);
    s >> std::ws;
    if (!s.eof())
        throw std::runtime_error("Trailing level data");
    if (!parsed || s.bad() || !std::isfinite(level.spawn.x) || !std::isfinite(level.spawn.y) ||
        level.background < 0 || level.background > 6 || !std::isfinite(level.bossSpawn.x) ||
        !std::isfinite(level.bossSpawn.y))
        throw std::runtime_error("Invalid level data");
    return level;
}
std::string saveLevel(const LevelState& level) {
    std::ostringstream s;
    s.precision(9);
    s << "ECHO_LEVEL_11\n"
      << level.background << ' ' << level.spawn.x << ' ' << level.spawn.y << ' ' << level.bossSpawn.x
      << ' ' << level.bossSpawn.y << '\n';
    for (int y = 0; y < mapHeight; ++y) {
        for (int x = 0; x < mapWidth; ++x)
            s << int(level.at(x, y)) << ' ';
        s << '\n';
    }
    s << level.platforms.count << '\n';
    for (auto& p : level.platforms)
        s << p.base.x << ' ' << p.base.y << ' ' << p.base.w << ' ' << p.base.h << ' ' << p.amplitude
          << ' ' << p.speed << '\n';
    for (auto& layer : level.layers) {
        s << layer.parallax << ' ' << layer.visible << '\n';
        for (int y = 0; y < mapHeight; ++y) {
            for (int x = 0; x < mapWidth; ++x)
                s << int(layer.cells[y * mapWidth + x]) << ' ';
            s << '\n';
        }
    }
    s << level.colliders.count << '\n';
    for (auto& c : level.colliders) {
        s << int(c.kind) << ' ' << c.bounds.x << ' ' << c.bounds.y << ' ' << c.bounds.w << ' '
          << c.bounds.h << ' ' << c.points.count;
        for (auto p : c.points)
            s << ' ' << p.x << ' ' << p.y;
        s << '\n';
    }
    s << level.lights.count << '\n';
    for (auto& l : level.lights)
        s << l.pos.x << ' ' << l.pos.y << ' ' << l.radius << ' ' << l.intensity << ' ' << l.warmColor.x
          << ' ' << l.warmColor.y << ' ' << l.blue << ' ' << l.flicker << '\n';
    s << level.dialogues.count << '\n';
    for (auto& d : level.dialogues)
        s << d.region.x << ' ' << d.region.y << ' ' << d.region.w << ' ' << d.region.h << ' ' << d.once
          << ' ' << std::quoted(d.speaker.c_str()) << ' ' << std::quoted(d.portrait.c_str()) << ' '
          << std::quoted(d.text.c_str()) << '\n';
    s << level.cameraClamp << ' ' << level.portals.count << '\n';
    for (auto& p : level.portals)
        s << p.region.x << ' ' << p.region.y << ' ' << p.region.w << ' ' << p.region.h << ' '
          << p.targetMap << ' ' << p.destination.x << ' ' << p.destination.y << ' ' << p.useSpawn
          << '\n';
    for (auto& layer : level.layers)
        s << std::quoted(layer.image.c_str()) << ' ' << layer.imageBounds.x << ' ' << layer.imageBounds.y
          << ' ' << layer.imageBounds.w << ' ' << layer.imageBounds.h << '\n';
    s << level.events.count << '\n';
    for (const auto& e : level.events)
        s << e.id << ' ' << e.region.x << ' ' << e.region.y << ' ' << e.region.w << ' ' << e.region.h
          << '\n';
    for (const auto& p : level.portals)
        s << p.transitionOverride << ' ' << std::quoted(writeTransition(p.transition)) << '\n';
    s << level.objects.count << '\n';
    for (const auto& p : level.objects)
        s << p.id << ' ' << p.templateId << ' ' << p.x << ' ' << p.y << ' ' << p.speed << ' ' << p.width
          << ' ' << p.height << ' ' << p.hp << ' ' << p.damage << ' ' << p.eventId << ' ' << p.overrides
          << ' ' << p.persistent << '\n';
    auto next = level.nextObjectId;
    for (const auto& p : level.objects)
        next = std::max(next, p.id + 1);
    if (!next)
        throw std::runtime_error("Invalid object ID allocator");
    s << next << '\n';
    validateFeatures(level);
    s << "PLATFORM_FEATURES " << level.nextFeatureId << ' ' << level.features.count << '\n';
    for (const auto& f : level.features)
        s << f.id << ' ' << int(f.kind) << ' ' << f.base.x << ' ' << f.base.y << ' ' << f.base.w << ' '
          << f.base.h << ' ' << f.speed << ' ' << f.delay << ' ' << f.resetSeconds << ' ' << f.enabled
          << ' ' << f.travel.x << ' ' << f.travel.y << ' ' << f.repeat << '\n';
    auto result = s.str();
    loadLevel(result); // Refuse invalid authored data before touching the project file.
    return result;
}
} // namespace echo
