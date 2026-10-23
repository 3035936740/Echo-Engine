#include "echo/script.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace echo;
void check(bool v, const char* text) {
    if (!v)
        throw std::runtime_error(text);
}
int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: echo_project_tests <project> <new-output> <runtime>\n";
        return 2;
    }
    try {
        Resources r;
        r.root = std::filesystem::path(argv[1]);
        auto w = std::make_unique<WorldState>(makeWorld());
        ScriptRuntime scripts;
        scripts.load(r, *w);
        check(w->jump.jump_velocity == -430 && w->rewind.history_seconds == 6, "project overrides");
        for (int phase = 1; phase <= 5; ++phase)
            for (int variant = 0; variant < 5; ++variant) {
                w->aster.attack = {};
                w->aster.phaseId = phase;
                w->aster.attack.variant = variant;
                scripts.planAttack(*w);
                check(w->aster.attack.volleys.count > 0 && w->aster.attack.zones.count > 0,
                      "25 Boss plans");
                for (auto& z : w->aster.attack.zones)
                    check(z.start >= z.lead && z.lead >= .85f, "every zone warns");
            }
        scripts.key(*w, "Space");
        auto value = w->script.persistent[0];
        auto saved = std::make_unique<WorldState>(*w);
        scripts.key(*w, "Space");
        restore(*w, *saved);
        scripts.afterRestore(*w);
        check(w->script.persistent[0] == value, "Lua persistent restore");
        auto level = loadLevel(r.text("levels/start.echo"));
        check(saveLevel(loadLevel(saveLevel(level))) == saveLevel(level), "level roundtrip");
        // Exercise every DSL primitive including nested repeat parse and execution.
        Resources example;
        example.root = r.root;
        auto temp = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(temp / "dsl");
        std::ofstream(temp / "dsl" / "project.echo") << "scripts=barrage.lua\n";
        std::ofstream(temp / "dsl" / "barrage.lua") << r.text("scripts/barrage.lua");
        example.root = temp / "dsl";
        ScriptRuntime dsl;
        dsl.load(example, *w);
        check(w->script.program.count == 13, "repeat flattens into fixed program");
        w->bullets.clear();
        stepProgram(*w, fixedDt);
        check(w->bullets.count == 12, "DSL fire executes");
        for (int i = 0; i < 160; ++i)
            stepProgram(*w, fixedDt);
        check(w->bullets.count >= 60, "wait repeat aim increment set_direction execute");
        std::filesystem::create_directory(temp / "audio-api");
        Resources audioAPI;
        audioAPI.root = temp / "audio-api";
        writeTextAtomically(audioAPI.root / "project.echo", "scripts=audio.lua\n");
        writeTextAtomically(audioAPI.root / "audio.lua",
                            "music.musicVolume=.6;music.loopStart=2;music.loopEnd=8;engine.fade_bgm(1,."
                            "2);engine.play_se(1,.5,1);engine.stop_bgm();engine.play_bgm(true)\n");
        writeTextAtomically(audioAPI.root / "audio.lua",
                            audioAPI.text("audio.lua") +
                                "engine.set_switch(3,true);engine.set_variable(2,42);assert(engine.get_"
                                "switch(3));assert(engine.get_variable(2)==42)\n");
        w->audio.lengths[0] = 1;
        ScriptRuntime audioScript;
        audioScript.load(audioAPI, *w);
        check(w->audio.fadeDuration == 1 && w->audio.fadeTarget == .2f && w->audio.voices.count == 1 &&
                  w->audio.playing,
              "Lua audio controls write snapshot state");
        check(w->events.switches[2] && w->events.variables[1] == 42,
              "Lua event switches and variables share WorldState");
        Resources objectAPI;
        objectAPI.root = temp / "object-api";
        std::filesystem::create_directories(objectAPI.root);
        ObjectLibrary objects;
        auto npc = objectPreset(1, 3);
        npc.gravity = false;
        objects.templates.push_back(npc);
        writeTextAtomically(objectAPI.root / "objects.echo", saveObjects(objects));
        AnimationLibrary animations;
        AnimationClip animation;
        animation.id = allocateAnimationId(animations);
        animation.name.set("Lua request");
        animation.sprite.set("assets/example.png");
        animation.frames.push_back({});
        animations.clips.push_back(animation);
        writeTextAtomically(objectAPI.root / "animations.echo", saveAnimations(animations));
        writeTextAtomically(objectAPI.root / "project.echo", "scripts=objects.lua\n");
        writeTextAtomically(objectAPI.root / "objects.lua",
            "local slot, gen = engine.create_object(1,500,400)\n"
            "assert(slot and engine.object_valid(slot,gen))\n"
            "engine.state[9]=slot;engine.state[10]=gen\n"
            "assert(engine.play_animation(slot,gen,1))\n"
            "engine.damage_object(slot,gen,1)\n"
            "function on_key_press(key)\n"
            "  local s,g=engine.state[9],engine.state[10]\n"
            "  assert(engine.object_valid(s,g));assert(engine.destroy_object(s,g))\n"
            "  assert(not engine.object_valid(s,g))\n"
            "end\n");
        w->objects = {};
        ScriptRuntime objectScript;
        objectScript.load(objectAPI, *w);
        auto handle = ObjectHandle{std::uint16_t(w->script.persistent[8]),
                                   std::uint32_t(w->script.persistent[9])};
        check(resolveObject(*w, handle) && resolveObject(*w, handle)->hp == 2 &&
              resolveObject(*w, handle)->animation.requested == 1, "Lua object and animation APIs");
        auto objectSnapshot = std::make_unique<WorldState>(*w);
        objectScript.key(*w, "Space");
        check(!resolveObject(*w, handle), "Lua invalidates destroyed handle");
        restore(*w, *objectSnapshot);
        objectScript.afterRestore(*w);
        check(resolveObject(*w, handle), "Lua persistent handle restores with world");
        objectScript.key(*w, "Space");
        check(!resolveObject(*w, handle), "Lua restored handle is safe on new branch");
        Resources unicode;
        unicode.root = temp / "unicode";
        std::filesystem::create_directory(unicode.root);
        const std::string name = "\xe4\xb8\xad\xe6\x96\x87.txt";
        writeTextAtomically(unicode.root / "project.echo", "title=Unicode path test\n");
        writeTextAtomically(unicode.root / pathFromUtf8(name), "UTF-8 resource");
        check(unicode.text(name) == "UTF-8 resource", "Chinese resource path");
        std::filesystem::create_directories(unicode.root / ".echo");
        writeTextAtomically(unicode.root / ".echo/recovery-latest.echo",
                            "Private authoring recovery data");
        unicode.exportGame(argv[3], temp / "unicode-package", true);
        Resources unicodePackage;
        check(unicodePackage.openEmbedded(temp / "unicode-package" /
                                          std::filesystem::path(argv[3]).filename()),
              "Unicode package mounts");
        check(unicodePackage.text(name) == "UTF-8 resource", "Unicode filename export roundtrip");
        check(!unicodePackage.contains(".echo/recovery-latest.echo"),
              "Editor recovery excluded from encrypted export");
        unicode.exportGame(argv[3], temp / "unicode-public", false);
        check(!std::filesystem::exists(temp / "unicode-public/game/.echo"),
              "Editor recovery excluded from public export");
        r.exportGame(argv[3], temp / "encrypted", true);
        auto exe = temp / "encrypted" / std::filesystem::path(argv[3]).filename();
        Resources encrypted;
        check(encrypted.openEmbedded(exe), "encrypted mount");
        check(encrypted.text("project.echo") == r.text("project.echo"), "XChaCha20 export roundtrip");
        auto bytes = std::filesystem::file_size(exe);
        std::fstream corrupt(exe, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekg(bytes - 120);
        char c;
        corrupt.read(&c, 1);
        c ^= 1;
        corrupt.seekp(bytes - 120);
        corrupt.write(&c, 1);
        corrupt.close();
        bool rejected = false;
        try {
            Resources changed;
            changed.openEmbedded(exe);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "tamper authentication");
        r.exportGame(argv[3], temp / "public", false);
        Resources pub;
        pub.root = temp / "public" / "game";
        check(pub.text("project.echo") == r.text("project.echo"), "public export");
        bool traversal = false;
        try {
            r.read("../CMakeLists.txt");
        } catch (...) {
            traversal = true;
        }
        check(traversal, "resource traversal rejected");
        std::cout
            << "PASS Lua config/persistence/DSL, 25 plans, level roundtrip, exports, tamper rejection\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
