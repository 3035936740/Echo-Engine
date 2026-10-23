#include "echo/assets.hpp"
#include "echo/resources.hpp"
#include "echo/objects.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace echo;
static void check(bool v, const char* label) {
    if (!v)
        throw std::runtime_error(label);
}
template <class F> static void rejects(F f, const char* label) {
    bool rejected = false;
    try {
        f();
    } catch (...) {
        rejected = true;
    }
    check(rejected, label);
}
int main(int argc, char** argv) {
    try {
        if (argc < 3)
            throw std::runtime_error("Usage: assets test <sample> <output>");
        auto base = std::filesystem::path(argv[2]) /
                    ("asset-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(base);
        Resources r;
        r.root = base / "project";
        std::filesystem::copy(argv[1], r.root, std::filesystem::copy_options::recursive);
        AssetCatalog catalog;
        scanAssets(r, catalog);
        writeTextAtomically(r.root / "assets.echo", saveAssets(catalog));
        r.setAssetCatalog(catalog);
        check(saveAssets(loadAssets(saveAssets(catalog))) == saveAssets(catalog), "Catalog roundtrip");
        rejects([&] { loadAssets(saveAssets(catalog) + "junk"); }, "Trailing catalog data rejected");
        auto duplicate = catalog;
        duplicate.entries[1].id = duplicate.entries[0].id;
        rejects([&] { saveAssets(duplicate); }, "Duplicate stable identity rejected");
        auto player = r.config().player.path;
        auto* found = findAssetPath(catalog, player);
        check(found && found->kind == AssetKind::Image && found->width > 0, "Imported RGBA metadata");
        auto id = found->id;
        auto ref = assetReference(id);
        auto original = r.read(ref);
        auto objects = ObjectLibrary{};
        auto t = objectPreset(1, 0);
        t.sprite.set(player);
        objects.templates.push_back(t);
        writeTextAtomically(r.root / "objects.echo", saveObjects(objects));
        auto config = r.config();
        auto map = loadLevel(r.text(config.maps[0]));
        map.layers[0].image.set(player);
        map.layers[0].imageBounds = {0, 0, 100, 100};
        DialogueDefinition d;
        d.portrait.set(player);
        d.text.set("test");
        d.region = {100, 100, 30, 30};
        map.dialogues.push(d);
        writeTextAtomically(r.root / pathFromUtf8(config.maps[0]), saveLevel(map));
        check(assetUsages(r, *found).size() >= 4,
              "Usage graph includes project, layer, dialogue and object");
        rejects([&] { deleteAsset(r, catalog, id); }, "Referenced asset deletion blocked");
        moveAsset(r, catalog, id, "assets/moved/player.rgba");
        check(r.config().player.path == ref && r.read(ref) == original,
              "Rename preserves stable image identity and bytes");
        check(!std::filesystem::exists(r.root / pathFromUtf8(player)), "Original path moved");
        auto restored = loadLevel(r.text(config.maps[0]));
        check(std::string(restored.layers[0].image.c_str()) == ref &&
                  std::string(restored.dialogues[restored.dialogues.count-1].portrait.c_str()) == ref,
              "Map references migrated");
        check(std::string(loadObjects(r.text("objects.echo")).templates[0].sprite.c_str()) == ref,
              "Template sprite migrated");
        rejects([&] { moveAsset(r, catalog, id, "../escape.rgba"); }, "Traversal move rejected");
        rejects([&] { moveAsset(r, catalog, id, "assets/tiles.png"); },
                "Existing destination preserved");
        check(r.read(ref) == original, "Failed move preserves original");
        auto* lighting = findAssetPath(catalog, "shaders/lighting.frag");
        check(lighting, "Implicit shader registered");
        auto lightId = lighting->id;
        moveAsset(r, catalog, lightId, "shaders/moved-light.frag");
        check(r.config().lightingShader == assetReference(lightId),
              "Implicit shader reference becomes explicit stable ID");
        std::filesystem::remove(r.root / "assets/moved/player.rgba");
        check(!r.contains(ref), "Missing identity remains locatable");
        relinkAsset(r, catalog, id, std::filesystem::path(argv[1]) / pathFromUtf8(player));
        check(r.read(ref) == original, "Relink preserves stable ID");
        auto bad = base / "wrong.rgba";
        writeTextAtomically(bad, "bad image");
        rejects([&] { relinkAsset(r, catalog, id, bad); }, "Wrong image cannot replace original");
        check(r.read(ref) == original, "Replacement failure preserves bytes");
        auto outside = base / "unused.txt";
        writeTextAtomically(outside, "Unused resource");
        auto a = importAsset(r, catalog, outside, "assets");
        auto b = importAsset(r, catalog, outside, "extra");
        check(a.id != b.id && r.contains(assetReference(a.id)),
              "Imported resources receive unique identities");
        auto next = catalog.nextId;
        rejects([&] { deleteAssets(r, catalog, {a.id, id}); },
                "Batch delete preflights all dependencies");
        check(r.contains(assetReference(a.id)), "Failed batch delete preserves every file");
        deleteAssets(r, catalog, {a.id, b.id});
        check(!findAsset(catalog, a.id) && !findAsset(catalog, b.id), "Unreferenced batch delete");
        auto newer = importAsset(r, catalog, outside, "again");
        check(newer.id >= next, "Deleted asset identities are not reused");
        auto ogg = probeAsset(std::filesystem::path(argv[1]) / "audio/ambient.ogg", "audio/test.ogg");
        check(ogg.kind == AssetKind::Audio && ogg.duration > 1,
              "Audio metadata validates decode and duration");
        check(assetId(ref) == id && r.resolve(ref) == "assets/moved/player.rgba",
              "Stable reference parse and resolve");
        std::cout << "PASS asset IDs, import metadata, reference migration, dependency checks, "
                     "missing/relink, batch deletion and rollback\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
