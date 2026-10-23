#include "echo/animations.hpp"
#include "echo/platform_features.hpp"
#include "echo/assets.hpp"
#include "echo/barrage.hpp"
#include "echo/database.hpp"
#include "echo/project_transaction.hpp"
#include "echo/recovery.hpp"
#include "echo/resources.hpp"
#include "echo/script.hpp"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
using namespace echo;
namespace {
void require(bool v, const char* message) {
    if (!v)
        throw std::runtime_error(message);
}
void fresh(WorldState& world) {
    world = makeWorld();
}
std::string projectText(const std::string& original, const std::map<std::string, std::string>& fields) {
    std::istringstream in(original);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto split = line.find('=');
        if (split == std::string::npos || !fields.contains(line.substr(0, split)))
            out << line << '\n';
    }
    for (const auto& [key, value] : fields)
        out << key << '=' << value << '\n';
    return out.str();
}
void copyResources(const Resources& source, const std::filesystem::path& target) {
    std::uint64_t bytes = 0;
    int entries = 0;
    for (const auto& folder : {"assets", "audio", "shaders"}) {
        auto base = source.root / folder;
        require(std::filesystem::is_directory(base), "Source example assets/audio/shaders missing");
        for (const auto& file : std::filesystem::recursive_directory_iterator(base)) {
            require(++entries <= 16384 && !file.is_symlink(),
                    "Example copy exceeds entry budget / symlink rejected");
            if (!file.is_regular_file())
                continue;
            bytes += file.file_size();
            require(bytes <= 256ull * 1024 * 1024, "Example assets exceed 256 MiB");
            auto relative = file.path().lexically_relative(source.root);
            auto destination = target / relative;
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(file.path(), destination, std::filesystem::copy_options::none);
        }
    }
    for (const auto& script : {"scripts/jump.lua", "scripts/rewind.lua"}) {
        std::filesystem::create_directories((target / script).parent_path());
        writeTextAtomically(target / script, source.text(script));
    }
}
} // namespace
int main(int argc, char** argv) {
    std::filesystem::path stage;
    try {
        require(argc == 3, "Usage: echo_example_builder SOURCE_PROJECT NEW_TARGET_PROJECT");
        Resources source;
        source.root = std::filesystem::weakly_canonical(pathFromUtf8(argv[1]));
        auto target = std::filesystem::absolute(pathFromUtf8(argv[2])).lexically_normal();
        require(!std::filesystem::exists(target),
                "Target already exists; example generator never overwrites it");
        auto relation = target.lexically_relative(source.root);
        require(relation.empty() || *relation.begin() == "..",
                "Example destination cannot be inside source");
        std::filesystem::create_directories(target.parent_path());
        stage = target;
        stage += L".stage-" + std::to_wstring(GetCurrentProcessId());
        require(std::filesystem::create_directory(stage), "Example staging directory already exists");
        copyResources(source, stage);
        auto configuration = source.config();
        auto text = projectText(source.text("project.echo"),
                                {{"title", "Echo Engine · 制作练习室"},
                                 {"level", "levels/start.echo"},
                                 {"maps", "levels/start.echo;levels/dawn.echo"},
                                 {"scripts", "scripts/jump.lua;scripts/rewind.lua;scripts/lab.lua"},
                                 {"bullet_preset", "0"},
                                 {"rewind_history_seconds", "6"},
                                 {"history_budget_mb", "512"}});
        writeTextAtomically(stage / "project.echo", text);
        writeTextAtomically(stage / "scripts/lab.lua",
                            "-- Complex examples can extend this file; normal authoring uses "
                            "forms.\nboss.enabled=false\n");
        Resources project;
        project.root = stage;
        AssetCatalog assets;
        scanAssets(project, assets);
        auto ref = [&](const std::string& path) {
            auto asset = findAssetPath(assets, path);
            require(asset != nullptr, "Example references a missing imported resource");
            return assetReference(asset->id);
        };
        std::map<std::string, std::string> references{
            {"player_sprite", ref(configuration.player.path)},
            {"boss_sprite", ref(configuration.boss.path)},
            {"tile_sprite", ref(configuration.tileSprite)},
            {"background_shader", ref(configuration.backgroundShader)},
            {"lighting_shader", ref(configuration.lightingShader)},
            {"fluid_shader", ref(configuration.fluidShader)},
            {"raymarch_shader", ref(configuration.raymarchShader)}};
        if (!configuration.music.empty())
            references["music"] = ref(configuration.music);
        for (int i = 0; i < 32; ++i)
            if (!configuration.effects[i].empty())
                references["se_" + std::to_string(i + 1)] = ref(configuration.effects[i]);
        for (auto& entry : assets.entries)
            entry.source.clear(); // Do not retain the transient staging path.
        text = projectText(text, references);
        ObjectLibrary objects;
        for (int i = 0; i < 9; ++i)
            objects.templates.push_back(objectPreset(allocateObjectTemplateId(objects), i));
        objects.templates[7].hp = 5;
        objects.templates[5].pickupVariable = 0;
        objects.templates[0].sprite.set(ref("assets/assetDemon.rgba"));
        objects.templates[1].sprite.set(ref("assets/assetDemon.rgba"));
        objects.templates[2].behavior =
            ObjectBehavior::Idle; // This cannon uses the authored emitter below.
        auto world = std::make_unique<WorldState>();
        fresh(*world);
        auto& level = world->level;
        level.objects.clear();
        level.dialogues.clear();
        level.events.clear();
        level.portals.clear();
        level.features.clear();
        const Rect featureRegions[] = {{352, 416, 32, 224}, {1024, 520, 256, 120}, {640, 640, 160, 16},
                                       {1440, 640, 96, 16}, {1280, 416, 160, 16},  {480, 576, 128, 16}};
        for (int kind = 0; kind < 6; ++kind)
            level.features.push(
                platformPreset(PlatformKind(kind), featureRegions[kind], level.nextFeatureId++));
        auto mobility = configuration.mobility;
        mobility.dashEnabled = mobility.wallJumpEnabled = true;
        text = projectText(text, {{"player_mobility", writeMobility(mobility)}});
        level.spawn = {160, 592};
        level.background = 2;
        auto place = [&](int preset, float x, float y) {
            ObjectPlacement p;
            p.id = level.nextObjectId++;
            p.templateId = objects.templates[preset].id;
            p.x = x;
            p.y = y;
            p.width = objects.templates[preset].width;
            p.height = objects.templates[preset].height;
            level.objects.push(p);
            return int(level.objects.count) - 1;
        };
        place(7, 160, 592);
        int npc = place(3, 240, 608);
        place(3, 512, 544); // NPC demonstrates riding the lift at {480,576}.
        place(5, 320, 608);
        place(0, 448, 608);
        auto chase = place(1, 672, 608);
        level.objects[chase].overrides = OverrideSpeed;
        level.objects[chase].speed = 50;
        int cannon = place(2, 960, 352);
        place(6, 1056, 592);
        auto door = place(4, 1184, 592);
        level.objects[door].eventId = 2;
        level.objects[npc].eventId = 1;
        Database db;
        ItemDefinition key;
        key.id = allocateDataId(db, false);
        key.name.set("回声钥匙");
        key.limit = 1;
        key.note.set("在升降平台上方取得，交给任务委托人。");
        db.items.push_back(key);
        QuestDefinition task;
        task.id = allocateDataId(db, true);
        task.name.set("找回回声钥匙");
        task.note.set("接受任务，取得钥匙，然后交付。进度与物品可以回溯。");
        db.quests.push_back(task);
        db.variables[0].name.set("已收集宝石");
        db.variables[1].name.set("剧情奖励");
        db.variables[2].name.set("跑步标记计数");
        db.switches[0].name.set("炮台停火");
        AnimationLibrary animations;
        for (int state = 0; state < 4; ++state) {
            AnimationClip clip;
            clip.id = allocateAnimationId(animations);
            const char* names[] = {"玩家待机", "玩家奔跑", "玩家跳跃", "玩家下落"};
            clip.name.set(names[state]);
            clip.sprite.set(ref(configuration.player.path));
            clip.frameWidth = clip.frameHeight = configuration.player.frame;
            clip.scale = configuration.player.width / configuration.player.frame;
            clip.anchorX = -configuration.player.offsetX / clip.scale;
            clip.anchorY = -configuration.player.offsetY / clip.scale;
            const auto& frames = configuration.player.clips[state];
            for (int i = 0; i < frames.count; ++i) {
                AnimationFrame f;
                f.column = frames.first + i;
                f.row = frames.row;
                f.seconds = 1 / frames.fps;
                if (state == 1 && (i == 2 || i == 6)) {
                    f.variable = 2;
                    f.delta = 1;
                }
                clip.frames.push_back(f);
            }
            animations.player[state] = clip.id;
            animations.clips.push_back(clip);
        }
        AnimationClip enemy;
        enemy.id = allocateAnimationId(animations);
        enemy.name.set("巡逻与追击");
        enemy.sprite.set(ref("assets/assetDemon.rgba"));
        enemy.frameWidth = enemy.frameHeight = 48;
        enemy.scale = 4; // One image texel per virtual-canvas pixel.
        enemy.anchorX = 24;
        enemy.anchorY = 40;

        for (int i = 0; i < 8; ++i) {
            AnimationFrame f;
            f.column = i;
            f.seconds = .125f;
            enemy.frames.push_back(f);
        }
        animations.clips.push_back(enemy);
        for (int i : {0, 1}) {
            AnimationBinding b;
            b.templateId = objects.templates[i].id;
            b.clips.fill(enemy.id);
            animations.bindings.push_back(b);
        }
        BarrageLibrary barrage;
        auto ring = barragePreset(allocateBarrageId(barrage), 0);
        ring.count = 16;
        ring.speed = 100;
        ring.lifetime = 5;
        auto fan = barragePreset(allocateBarrageId(barrage), 1);
        fan.count = 5;
        fan.group = 2;
        fan.speed = 120;
        fan.lifetime = 5;
        barrage.patterns = {ring, fan};
        BarrageEmitter emitter;
        emitter.id = allocateBarrageId(barrage);
        emitter.name.set("组合炮台");
        emitter.origin = BarrageOrigin::Object;
        emitter.object = level.objects[cannon].id;
        emitter.position = {16, 16};
        emitter.seed = 271828;
        emitter.loopDelay = 1.2f;
        emitter.waves = {{ring.id, 1, .25f, .85f, .8f, 0}, {fan.id, 3, .1f, .85f, .4f, 15}};
        barrage.emitters.push_back(emitter);
        EventMap events;
        events.events.push_back(eventPreset(3, 8));
        EventPlacement choiceNpc;
        choiceNpc.id = 3;
        choiceNpc.region = {420, 592, 32, 48};
        choiceNpc.graphic = 1;
        level.events.push(choiceNpc);
        auto tutorial = eventPreset(1, 0);
        tutorial.name.set("制作室说明");
        tutorial.pages[0].commands[0].text.set(
            "欢迎来到制作练习室。A/D 移动，空格跳跃，T 撤回，R 直线撞击。\n"
            "打开对象、动画与弹幕窗口即可修改本关卡；普通配置不需要 Lua。\n"
            "前方宝石会记入已收集宝石变量。按门进入第二张地图，再回来看是否保留。");
        events.events.push_back(tutorial);
        EventPlacement sign;
        sign.id = 1;
        sign.region = {228, 588, 48, 52};
        level.events.push(sign);
        auto transfer = eventPreset(2, 0);
        transfer.name.set("制作室出口");
        EventCommand move;
        move.op = EventOp::Transfer;
        move.a = 1;
        move.b = 1;
        transfer.pages[0].commands = {move};
        events.events.push_back(transfer);
        EventPlacement exit;
        exit.id = 2;
        exit.region = {1184, 576, 48, 64};
        level.events.push(exit);
        auto op = [](EventOp type, std::uint64_t reference = 0, int amount = 0, int flag = 0) {
            EventCommand c;
            c.op = type;
            c.reference = reference;
            c.value = float(amount);
            c.b = flag;
            return c;
        };
        auto say = [&](const char* text) {
            auto c = op(EventOp::Text);
            c.speaker.set("委托人");
            c.text.set(text);
            return c;
        };
        EventDefinition commission;
        commission.id = 4;
        commission.name.set("钥匙任务委托");
        EventPage job;
        job.commands = {op(EventOp::IfQuestStatus, task.id, 0, 2),
                        say("任务已经完成。你可以按T撤回物品和任务状态。"),
                        op(EventOp::Else),
                        op(EventOp::IfQuestStatus, task.id, 0, 1),
                        op(EventOp::IfItem, key.id, 1, 0),
                        op(EventOp::ChangeItem, key.id, -1),
                        op(EventOp::QuestProgress, task.id, 1),
                        say("收到钥匙，任务完成！"),
                        op(EventOp::Else),
                        say("还没找到钥匙；在升降平台上方有一处发光标记。"),
                        op(EventOp::EndIf),
                        op(EventOp::Else),
                        op(EventOp::QuestStatus, task.id, 0, 1),
                        say("请乘升降平台上去找回声钥匙，再回来交给我。"),
                        op(EventOp::EndIf),
                        op(EventOp::EndIf)};
        commission.pages = {job};
        events.events.push_back(commission);
        EventPlacement giver;
        giver.id = 4;
        giver.region = {360, 588, 32, 52};
        giver.graphic = 1;
        level.events.push(giver);
        EventDefinition pickup;
        pickup.id = 5;
        pickup.name.set("取得回声钥匙");
        EventPage take;
        take.trigger = EventTrigger::Touch;
        take.selfSwitch = 0;
        take.selfValue = false;
        take.commands = {op(EventOp::ChangeItem, key.id, 1), say("取得回声钥匙，回去交给委托人。"),
                         op(EventOp::SelfSwitch, 0, 0, 1)};
        pickup.pages = {take};
        events.events.push_back(pickup);
        EventPlacement marker;
        marker.id = 5;
        marker.region = {536, 336, 48, 48};
        marker.graphic = 1;
        level.events.push(marker);
        auto second = std::make_unique<WorldState>();
        fresh(*second);
        second->level.objects.clear();
        second->level.dialogues.clear();
        second->level.events.clear();
        second->level.portals.clear();
        second->level.background = 4;
        second->level.spawn = {256, 592};
        MapPortal back;
        back.region = {96, 544, 48, 96};
        back.targetMap = 0;
        back.useSpawn = true;
        second->level.portals.push(back);
        std::filesystem::create_directories(stage / "levels");
        std::filesystem::create_directories(stage / "events");
        commitProjectFiles(stage, {{"project.echo", text},
                                   {"assets.echo", saveAssets(assets)},
                                   {"objects.echo", saveObjects(objects)},
                                   {"animations.echo", saveAnimations(animations)},
                                   {"barrage.echo", saveBarrage(barrage)},
                                   {"database.echo", saveDatabase(db)},
                                   {"levels/start.echo", saveLevel(level)},
                                   {"levels/start.echo.events", saveEvents(events)},
                                   {"levels/dawn.echo", saveLevel(second->level)},
                                   {"levels/dawn.echo.events", saveEvents({})},
                                   {"events/common.echo", saveEvents({}, true)}});
        project.invalidateAssetCatalog();
        for (const auto& issue : inspectProject(project))
            require(!issue.error, issue.message.c_str());
        ScriptRuntime script;
        world->level = loadLevel(project.text("levels/start.echo"));
        applyDatabaseDefaults(*world, db);
        script.load(project, *world);
        Simulation simulation(192ull * 1024 * 1024);
        simulation.setObjects(objects);
        simulation.setAnimations(animations);
        simulation.setBarrage(barrage);
        simulation.setMaps({level, second->level});
        simulation.setEvents({events, {}}, {});
        world->kino.invulnTimer = 10000;
        simulation.reset(*world);
        for (int i = 0; i < 420; ++i)
            simulation.tick({});
        require(simulation.history().seconds() > 5.8 && simulation.world().barrage.volleys > 0,
                "Example six-second history / authored barrage validation failed");
        simulation.seekHistory(0);
        for (int i = 0; i < 60; ++i)
            simulation.tick({});
        require(simulation.world().kino.hp > 0, "Example oldest snapshot continuation failed");
        // Commit the complete, validated new project with one directory rename.
        require(simulation.world().kino.hp == 5 &&
                    std::abs(simulation.world().kino.x - level.spawn.x) < 1,
                "Example player template / spawn adapter mismatch");
        std::filesystem::rename(stage, target);
        stage.clear();
        std::cout << "Created " << pathUtf8(target) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        // Only remove the exact fresh staging directory owned by this invocation.
        std::error_code checkError;
        auto absolute = std::filesystem::weakly_canonical(stage, checkError);
        auto workspace = std::filesystem::weakly_canonical(std::filesystem::current_path(), checkError);
        auto relative = absolute.lexically_relative(workspace);
        if (!stage.empty() && !checkError && !relative.empty() && *relative.begin() != ".." &&
            !std::filesystem::is_symlink(stage) &&
            absolute == std::filesystem::absolute(stage).lexically_normal() &&
            std::filesystem::exists(stage) &&
            stage.filename().wstring().ends_with(L".stage-" + std::to_wstring(GetCurrentProcessId()))) {
            std::error_code error;
            std::filesystem::remove_all(stage, error);
        }
        return 1;
    }
}
