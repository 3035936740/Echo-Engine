#include "echo/project_transaction.hpp"
#include <windows.h>
#include "echo/assets.hpp"
#include "echo/resources.hpp"
#include "echo/objects.hpp"
#include "echo/animations.hpp"
#include "echo/events.hpp"
#include "echo/audio_decode.hpp"
#include <wincodec.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
namespace echo {
namespace {
std::string safePath(std::string_view name) {
    if (name.empty() || name.size() > 255 || name.find('\0') != std::string_view::npos ||
        name.find(':') != std::string_view::npos || name.starts_with('@'))
        throw std::runtime_error("Invalid asset path");
    auto p = pathFromUtf8(name);
    if (p.is_absolute() || p.has_root_name())
        throw std::runtime_error("Asset path must be project relative");
    for (const auto& part : p)
        if (part == "." || part == "..")
            throw std::runtime_error("Asset traversal rejected");
    return pathUtf8(p);
}
std::filesystem::path inside(const Resources& r, std::string_view name) {
    auto p = r.root / pathFromUtf8(safePath(name));
    auto relative = std::filesystem::weakly_canonical(p).lexically_relative(
        std::filesystem::weakly_canonical(r.root));
    if (relative.empty() || *relative.begin() == "..")
        throw std::runtime_error("Asset escaped project");
    if (std::filesystem::is_symlink(p))
        throw std::runtime_error("Asset symlinks are unsupported");
    return p;
}
std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}
template <class T> struct Com {
    T* p{};
    ~Com() {
        if (p)
            p->Release();
    }
    T** address() {
        return &p;
    }
    T* operator->() {
        return p;
    }
};
void checked(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("Image decode failed");
}
void ensureEditable(const Resources& r) {
    if (r.published)
        throw std::runtime_error("Published resources are read-only");
}
std::uint64_t nextId(AssetCatalog& c) {
    for (const auto& e : c.entries)
        if (e.id >= c.nextId)
            c.nextId = e.id + 1;
    if (!c.nextId || c.nextId == UINT64_MAX)
        throw std::runtime_error("Asset ID space exhausted");
    return c.nextId++;
}
using Patch = std::map<std::string, std::string>;
struct ReferenceWalk {
    const Resources& r;
    const AssetRecord& asset;
    std::vector<AssetUsage> uses;
    Patch patches;
    bool migrate{};
    bool replace(std::string& ref, const std::string& file, const std::string& location) {
        if (ref != asset.path && ref != assetReference(asset.id))
            return false;
        uses.push_back({file, location});
        if (migrate)
            ref = assetReference(asset.id);
        return migrate;
    }
    template <std::size_t N>
    bool replace(Text<N>& ref, const std::string& file, const std::string& location) {
        std::string value(ref.c_str());
        bool changed = replace(value, file, location);
        if (changed)
            ref.set(value);
        return changed;
    }
    void walk() {
        auto config = r.config();
        std::set<std::string> projectKeys;
        std::istringstream project(r.text("project.echo"));
        std::ostringstream out;
        std::string line;
        bool changed = false;
        while (std::getline(project, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            auto pos = line.find('=');
            if (pos != std::string::npos) {
                auto key = line.substr(0, pos), value = line.substr(pos + 1);
                projectKeys.insert(key);
                bool direct = key == "player_sprite" || key == "boss_sprite" || key == "tile_sprite" ||
                              key == "music" || key == "background_shader" ||
                              key == "transition_shader" || key == "lighting_shader" ||
                              key == "fluid_shader" || key == "raymarch_shader" ||
                              key.starts_with("se_");
                if (direct)
                    changed |= replace(value, "project.echo", key);
                if (key == "scripts") {
                    std::istringstream list(value);
                    std::string ref, next;
                    while (std::getline(list, ref, ';')) {
                        changed |= replace(ref, "project.echo", "Lua script");
                        if (!next.empty())
                            next += ';';
                        next += ref;
                    }
                    value = next;
                }
                line = key + "=" + value;
            }
            out << line << '\n';
        }
        for (auto [key, value] :
             std::map<std::string, std::string>{{"lighting_shader", config.lightingShader},
                                                {"fluid_shader", config.fluidShader},
                                                {"raymarch_shader", config.raymarchShader},
                                                {"background_shader", config.backgroundShader}})
            if (!projectKeys.contains(key) && replace(value, "project.echo", key)) {
                out << key << '=' << value << '\n';
                changed = true;
            }
        if (changed)
            patches["project.echo"] = out.str();
        for (const auto& map : config.maps) {
            auto level = loadLevel(r.text(map));
            bool modified = false;
            for (auto& layer : level.layers)
                modified |= replace(layer.image, map, "Layer image");
            for (auto& d : level.dialogues)
                modified |= replace(d.portrait, map, "Dialogue portrait");
            if (modified)
                patches[map] = saveLevel(level);
        }
        std::vector<std::string> eventPaths{"events/common.echo"};
        for (const auto& map : config.maps)
            eventPaths.push_back(map + ".events");
        for (const auto& file : eventPaths)
            if (r.contains(file)) {
                auto events = loadEvents(r.text(file), file == "events/common.echo");
                bool modified = false;
                for (auto& e : events.events)
                    for (auto& page : e.pages)
                        for (auto& cmd : page.commands)
                            modified |= replace(cmd.portrait, file,
                                                "Event #" + std::to_string(e.id) + " portrait");
                if (modified)
                    patches[file] = saveEvents(events, file == "events/common.echo");
            }
        if (r.contains("animations.echo")) {
            auto animations = loadAnimations(r.text("animations.echo"));
            bool modified = false;
            for (auto& clip : animations.clips)
                modified |= replace(clip.sprite, "animations.echo", "Animation #" +
                                     std::to_string(clip.id) + " sprite");
            if (modified) patches["animations.echo"] = saveAnimations(animations);
        }
        if (r.contains("objects.echo")) {
            auto objects = loadObjects(r.text("objects.echo"));
            bool modified = false;
            for (auto& t : objects.templates)
                modified |=
                    replace(t.sprite, "objects.echo", "Template #" + std::to_string(t.id) + " sprite");
            if (modified)
                patches["objects.echo"] = saveObjects(objects);
        }
    }
};
} // namespace
std::string assetReference(std::uint64_t id) {
    std::ostringstream s;
    s << '@' << std::hex << std::setw(16) << std::setfill('0') << id;
    return s.str();
}
std::uint64_t assetId(std::string_view ref) {
    std::uint64_t id{};
    if (ref.size() != 17 || ref[0] != '@')
        throw std::runtime_error("Asset ID requires @ plus 16 hex digits");
    auto [end, error] = std::from_chars(ref.data() + 1, ref.data() + 17, id, 16);
    if (error != std::errc{} || end != ref.data() + 17 || !id)
        throw std::runtime_error("Invalid asset ID");
    return id;
}
const AssetRecord* findAsset(const AssetCatalog& c, std::uint64_t id) {
    for (const auto& e : c.entries)
        if (e.id == id)
            return &e;
    return nullptr;
}
const AssetRecord* findAssetPath(const AssetCatalog& c, std::string_view path) {
    for (const auto& e : c.entries)
        if (e.path == path)
            return &e;
    return nullptr;
}
void validateAssets(const AssetCatalog& c) {
    if (c.entries.size() > 4096 || !c.nextId)
        throw std::runtime_error("Asset catalog capacity is 4096");
    std::set<std::uint64_t> ids;
    std::set<std::string> paths;
    for (const auto& e : c.entries) {
        safePath(e.path);
        if (!e.id || e.id >= c.nextId || !ids.insert(e.id).second ||
            !paths.insert(lower(e.path)).second || e.name.empty() || e.name.size() > 255 ||
            e.tags.size() > 1024 || e.source.size() > 2048 || int(e.kind) > 5 || e.width > 16384 ||
            e.height > 16384 || e.bytes > 256ull * 1024 * 1024 || !std::isfinite(e.duration) ||
            e.duration < 0)
            throw std::runtime_error("Invalid asset metadata or duplicate identity");
    }
}
AssetCatalog loadAssets(const std::string& text) {
    if (text.size() > 16ull * 1024 * 1024)
        throw std::runtime_error("Asset catalog exceeds 16 MiB");
    std::istringstream in(text);
    std::string header;
    int count;
    AssetCatalog c;
    if (!(in >> header >> c.nextId >> count) || header != "ECHO_ASSETS_1" || count < 0 || count > 4096)
        throw std::runtime_error("Invalid asset catalog header");
    for (int i = 0; i < count; ++i) {
        AssetRecord e;
        int kind;
        if (!(in >> e.id >> std::quoted(e.path) >> std::quoted(e.name) >> std::quoted(e.tags) >>
              std::quoted(e.source) >> kind >> e.width >> e.height >> e.bytes >> e.duration) ||
            kind < 0 || kind > 5)
            throw std::runtime_error("Invalid asset metadata row");
        e.kind = AssetKind(kind);
        c.entries.push_back(e);
    }
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Trailing asset data");
    validateAssets(c);
    return c;
}
std::string saveAssets(const AssetCatalog& c) {
    validateAssets(c);
    std::ostringstream out;
    out << "ECHO_ASSETS_1 " << c.nextId << ' ' << c.entries.size() << '\n' << std::setprecision(17);
    for (const auto& e : c.entries)
        out << e.id << ' ' << std::quoted(e.path) << ' ' << std::quoted(e.name) << ' '
            << std::quoted(e.tags) << ' ' << std::quoted(e.source) << ' ' << int(e.kind) << ' '
            << e.width << ' ' << e.height << ' ' << e.bytes << ' ' << e.duration << '\n';
    return out.str();
}
AssetRecord probeAsset(const std::filesystem::path& path, std::string relative) {
    AssetRecord e;
    e.path = safePath(relative);
    e.name = pathUtf8(path.stem());
    e.bytes = std::filesystem::file_size(path);
    if (!e.bytes || e.bytes > 256ull * 1024 * 1024)
        throw std::runtime_error("Asset file size must be 1 byte..256 MiB");
    auto ext = lower(pathUtf8(path.extension()));
    if (ext == ".rgba") {
        e.kind = AssetKind::Image;
        std::ifstream f(path, std::ios::binary);
        f.read(reinterpret_cast<char*>(&e.width), 4);
        f.read(reinterpret_cast<char*>(&e.height), 4);
        if (!f || !e.width || !e.height || e.width > 8192 || e.height > 8192 ||
            std::uint64_t(e.width) * e.height * 4 > 64ull * 1024 * 1024 ||
            e.bytes != 8 + std::uint64_t(e.width) * e.height * 4)
            throw std::runtime_error("Invalid RGBA image");
    } else if (ext == ".png" || ext == ".bmp" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif") {
        e.kind = AssetKind::Image;
        auto init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        struct Uninit {
            bool yes;
            ~Uninit() {
                if (yes)
                    CoUninitialize();
            }
        } uninit{SUCCEEDED(init)};
        Com<IWICImagingFactory> factory;
        checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_IWICImagingFactory, reinterpret_cast<void**>(factory.address())));
        Com<IWICBitmapDecoder> decoder;
        checked(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnLoad, decoder.address()));
        Com<IWICBitmapFrameDecode> frame;
        checked(decoder->GetFrame(0, frame.address()));
        UINT width, height;
        checked(frame->GetSize(&width, &height));
        e.width = width;
        e.height = height;
        if (!width || !height || width > 8192 || height > 8192 ||
            std::uint64_t(width) * height * 4 > 64ull * 1024 * 1024)
            throw std::runtime_error("Image exceeds 64 MiB decoded budget");
    } else if (ext == ".ogg" || ext == ".wav") {
        e.kind = AssetKind::Audio;
        std::ifstream f(path, std::ios::binary);
        Bytes bytes(e.bytes);
        f.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!f)
            throw std::runtime_error("Audio read failed");
        e.duration = decodeAudio(bytes).seconds();
    } else if (ext == ".lua")
        e.kind = AssetKind::Script;
    else if (ext == ".frag" || ext == ".vert" || ext == ".glsl")
        e.kind = AssetKind::Shader;
    else if (ext == ".ttf" || ext == ".otf")
        e.kind = AssetKind::Font;
    return e;
}
void scanAssets(const Resources& r, AssetCatalog& c) {
    ensureEditable(r);
    auto next = c;
    std::size_t visited = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(r.root);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (++visited > 16384) throw std::runtime_error("Asset scan exceeds 16384 project entries");
        const auto& entry = *it;
        if (entry.is_symlink()) throw std::runtime_error("Project asset symlink rejected");
        auto path = pathUtf8(entry.path().lexically_relative(r.root));
        auto folded = lower(path);
        if (entry.is_directory()) {
            if (folded == ".echo") it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file() || folded == "project.echo" || folded.ends_with(".echo") ||
            folded.ends_with(".events") || findAssetPath(next, path)) continue;
        if (next.entries.size() == 4096) throw std::runtime_error("Asset catalog capacity is 4096");
        auto e = probeAsset(entry.path(), path);
        e.id = nextId(next); e.source = path;
        next.entries.push_back(e);
    }
    validateAssets(next); c = std::move(next);
}
std::vector<AssetUsage> assetUsages(const Resources& r, const AssetRecord& e) {
    ReferenceWalk walker{r, e, {}, {}, false};
    walker.walk();
    return walker.uses;
}
AssetRecord importAsset(Resources& r, AssetCatalog& c, const std::filesystem::path& source,
                        std::string_view folder) {
    ensureEditable(r);
    std::string target(folder);
    if (!target.empty() && !target.ends_with('/'))
        target += '/';
    target += pathUtf8(source.filename());
    auto dest = inside(r, target);
    if (std::filesystem::exists(dest)) {
        if (std::filesystem::weakly_canonical(dest) != std::filesystem::weakly_canonical(source))
            throw std::runtime_error(
                "Asset destination already exists; use resource browser replacement");
        auto e = probeAsset(source, target);
        auto next = c;
        if (const auto* existing = findAssetPath(c, target)) {
            e.id = existing->id;
            e.name = existing->name;
            e.tags = existing->tags;
            e.source = existing->source;
            for (auto& entry : next.entries)
                if (entry.id == e.id)
                    entry = e;
        } else {
            e.id = nextId(next);
            e.source = pathUtf8(source);
            next.entries.push_back(e);
        }
        validateAssets(next);
        commitProjectFiles(r.root, {{"assets.echo", saveAssets(next)}});
        c = std::move(next);
        r.setAssetCatalog(c);
        return e;
    }
    auto e = probeAsset(source, target);
    e.source = pathUtf8(source);
    auto next = c;
    e.id = nextId(next);
    next.entries.push_back(e);
    validateAssets(next);
    std::string staging =
        ".echo/asset-import-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(e.id);
    auto stage = inside(r, staging);
    std::filesystem::create_directories(stage.parent_path());
    if (std::filesystem::exists(stage))
        throw std::runtime_error("Asset import staging file already exists");
    std::filesystem::copy_file(source, stage);
    try {
        commitProjectFiles(r.root, {{"assets.echo", saveAssets(next)}}, {{staging, target}});
    } catch (...) {
        if (std::filesystem::exists(stage))
            std::filesystem::remove(stage);
        throw;
    }
    c = std::move(next);
    r.setAssetCatalog(c);
    return e;
}
void moveAsset(Resources& r, AssetCatalog& c, std::uint64_t id, std::string_view target) {
    ensureEditable(r);
    const auto* found = findAsset(c, id);
    if (!found)
        throw std::runtime_error("Missing asset ID");
    auto old = *found;
    auto source = inside(r, old.path), dest = inside(r, target);
    if (source == dest)
        return;
    if (std::filesystem::exists(dest))
        throw std::runtime_error("Asset destination already exists");
    auto next = c;
    for (auto& e : next.entries)
        if (e.id == id)
            e.path = safePath(target);
    validateAssets(next);
    ReferenceWalk walker{r, old, {}, {}, true};
    walker.walk();
    walker.patches["assets.echo"] = saveAssets(next);
    commitProjectFiles(r.root, walker.patches, {{old.path, safePath(target)}});
    c = std::move(next);
    r.setAssetCatalog(c);
}
void relinkAsset(Resources& r, AssetCatalog& c, std::uint64_t id, const std::filesystem::path& source) {
    ensureEditable(r);
    const auto* found = findAsset(c, id);
    if (!found)
        throw std::runtime_error("Missing asset ID");
    auto updated = probeAsset(source, found->path);
    if (updated.kind != found->kind)
        throw std::runtime_error("Replacement asset type differs");
    auto dest = inside(r, found->path);
    if (std::filesystem::weakly_canonical(source) == std::filesystem::weakly_canonical(dest))
        return;
    updated.id = id;
    updated.tags = found->tags;
    updated.name = found->name;
    updated.source = pathUtf8(source);
    auto next = c;
    for (auto& e : next.entries)
        if (e.id == id)
            e = updated;
    validateAssets(next);
    auto prefix =
        ".echo/asset-relink-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(id);
    auto stage = inside(r, prefix + "-new"), backup = inside(r, prefix + "-old");
    if (std::filesystem::exists(stage) || std::filesystem::exists(backup))
        throw std::runtime_error("Asset replacement staging files already exist");
    std::filesystem::create_directories(stage.parent_path());
    std::filesystem::copy_file(source, stage);
    std::vector<ProjectFileMove> moves;
    if (std::filesystem::exists(dest))
        moves.push_back({found->path, prefix + "-old"});
    moves.push_back({prefix + "-new", found->path});
    try {
        commitProjectFiles(r.root, {{"assets.echo", saveAssets(next)}}, moves);
    } catch (...) {
        if (std::filesystem::exists(stage))
            std::filesystem::remove(stage);
        throw;
    }
    c = std::move(next);
    r.setAssetCatalog(c);
    if (std::filesystem::exists(backup))
        std::filesystem::remove(backup);
}
void deleteAssets(Resources& r, AssetCatalog& c, const std::vector<std::uint64_t>& ids) {
    ensureEditable(r);
    std::set<std::uint64_t> selected(ids.begin(), ids.end());
    if (selected.empty())
        return;
    auto next = c;
    std::vector<ProjectFileMove> moves;
    std::string uses;
    for (auto id : selected) {
        const auto* e = findAsset(c, id);
        if (!e)
            throw std::runtime_error("Missing asset ID");
        for (const auto& u : assetUsages(r, *e))
            uses += "\n" + e->name + " / " + u.file + " / " + u.location;
        auto backup =
            ".echo/asset-trash-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(id);
        if (std::filesystem::exists(inside(r, backup)))
            throw std::runtime_error("An asset trash backup already exists");
        if (std::filesystem::exists(inside(r, e->path)))
            moves.push_back({e->path, backup});
    }
    if (!uses.empty())
        throw std::runtime_error("Assets are referenced:" + uses);
    std::erase_if(next.entries, [&](const auto& e) { return selected.contains(e.id); });
    validateAssets(next);
    commitProjectFiles(r.root, {{"assets.echo", saveAssets(next)}}, moves);
    c = std::move(next);
    r.setAssetCatalog(c);
    for (const auto& m : moves)
        std::filesystem::remove(inside(r, m.to));
}
void deleteAsset(Resources& r, AssetCatalog& c, std::uint64_t id) {
    deleteAssets(r, c, {id});
}
} // namespace echo
