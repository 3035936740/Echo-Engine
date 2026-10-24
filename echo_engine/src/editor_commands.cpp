#include "echo/focus.hpp"
#include "echo/platform_features.hpp"
#include "echo/collision.hpp"
#include "echo/app.hpp"
#include "echo/project_transaction.hpp"
#include "echo/presets.hpp"
#include "echo/transition.hpp"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace echo {
void App::loadProject(HistoryClearReason reason) {
    for (HWND* popup : {&eventEditor,       &tilePalette,      &dialogEditor,
                        &audioEditor,       &mapEditor,        &layerEditor,
                        &transitionEditor,  &databaseEditor,   &mapToolsEditor,
                        &debuggerEditor,    &healthEditor,     &objectsEditor,
                        &assetsEditor,      &animationsEditor, &barrageTargetEditor,
                        &barrageEditor,     &focusEditor,      &inputEditor,
                        &rewindRulesEditor, &frameRateEditor,  &snapshotCompareEditor,
                        &collisionEditor,   &choiceEditor,     &mobilityEditor}) {
        if (*popup && IsWindow(*popup))
            DestroyWindow(*popup);
        *popup = nullptr;
    }
    if (animationBitmap) {
        DeleteObject(animationBitmap);
        animationBitmap = nullptr;
    }
    if (assetThumbnail) {
        DeleteObject(assetThumbnail);
        assetThumbnail = nullptr;
    }
    if (tileAtlas) {
        DeleteObject(tileAtlas);
        tileAtlas = nullptr;
    }
    snapshotA.reset();
    snapshotB.reset();
    snapshotReport.reset();
    snapshotFilterText.clear();
    debugPaused = debugStepRequested = false;
    renderer.debugCollisions = false;
    transitionPreview = false;
    if (!resources.published)
        recoverProjectTransaction(resources.root);
    resources.invalidateAssetCatalog();
    assetLibrary = resources.assetCatalog();
    if (!resources.published && !resources.contains("assets.echo"))
        scanAssets(resources, assetLibrary);
    resources.setAssetCatalog(assetLibrary);
    config = resources.config();
    eventClipboard.clear();
    eventDraftUndo.clear();
    eventDraftRedo.clear();
    eventRows.clear();
    eventCollapsed.reset();
    databaseUndo.clear();
    databaseRedo.clear();
    database =
        resources.contains("database.echo") ? loadDatabase(resources.text("database.echo")) : Database{};
    objectLibrary = resources.contains("objects.echo") ? loadObjects(resources.text("objects.echo"))
                                                       : ObjectLibrary{};
    if (!resources.contains("objects.echo"))
        for (int preset = 0; preset < 9; ++preset)
            objectLibrary.templates.push_back(objectPreset(preset + 1, preset));
    animationLibrary = resources.contains("animations.echo")
                           ? loadAnimations(resources.text("animations.echo"))
                           : AnimationLibrary{};
    editor.barrage = resources.contains("barrage.echo") ? loadBarrage(resources.text("barrage.echo"))
                                                        : BarrageLibrary{};
    validateBarrage(editor.barrage, int(config.maps.size()));
    barragePatternIndex = barrageEmitterIndex = -1;
    barragePreviewWorld.reset();
    objectTemplateIndex = 0;
    objectInstanceIndex = -1;
    maps.clear();
    eventMaps.clear();
    commonEvents = resources.contains("events/common.echo")
                       ? loadEvents(resources.text("events/common.echo"), true)
                       : EventMap{};
    editMap = 0;
    for (auto& name : config.maps) {
        if (name == config.level)
            editMap = int(maps.size());
        maps.push_back(loadLevel(resources.text(name)));
        auto eventPath = name + ".events";
        eventMaps.push_back(resources.contains(eventPath) ? loadEvents(resources.text(eventPath))
                                                          : EventMap{});
        for (const auto& placed : maps.back().events)
            if (!findEvent(eventMaps.back(), placed.id))
                throw std::runtime_error("地图引用了不存在的事件定义");
    }
    *initial_ = makeWorld();
    applyDatabaseDefaults(*initial_, database);
    initial_->collisions = config.collisions;
    initial_->hurt = config.hurt;
    initial_->focus = config.focus;
    initial_->mobility = config.mobility;
    initial_->mapTransition = config.mapTransition;
    initial_->deathTransition = config.deathTransition;
    initial_->level = maps[editMap];
    initial_->activeMap = editMap;
    initial_->kino.x = initial_->level.spawn.x;
    initial_->kino.y = initial_->level.spawn.y;
    initial_->aster.x = initial_->level.bossSpawn.x;
    initial_->aster.y = initial_->level.bossSpawn.y;
    audio.load(resources, config);
    audio.configure(*initial_, config);
    auto next = std::make_unique<ScriptRuntime>();
    next->load(resources, *initial_);
    scripts = std::move(next);
    std::istringstream s(resources.text("project.echo"));
    std::string line;
    while (std::getline(s, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto split = line.find('=');
        if (split == std::string::npos)
            continue;
        auto key = line.substr(0, split), value = line.substr(split + 1);
        if (key == "jump_preset")
            initial_->jumpPreset = std::stoi(value);
        if (key == "bullet_preset")
            initial_->bulletPreset = std::clamp(std::stoi(value), -1, 6);
        if (key == "rewind_history_seconds")
            initial_->rewind.history_seconds = std::clamp(std::stof(value), 1.0f, 30.0f);
        if (key == "rewind_record_hz")
            initial_->rewind.record_hz = std::stoi(value);
        if (key == "rewind_fast_stop_near_bullets") {
            if (value != "0" && value != "1")
                throw std::runtime_error("rewind_fast_stop_near_bullets 只能是 0 或 1");
            initial_->rewind.fast_stop_near_bullets = value == "1";
        }
        if (key == "rewind_fast_bullet_margin") {
            std::size_t used{};
            initial_->rewind.fast_bullet_margin = std::stof(value, &used);
            if (value.find_first_not_of(" \t", used) != std::string::npos)
                throw std::runtime_error("rewind_fast_bullet_margin 数值无效");
        }
        if (key == "rewind_enabled")
            initial_->rewind.enabled = std::stoi(value) != 0;
    }
    if (initial_->jumpPreset >= 0)
        applyJumpPreset(*initial_, initial_->jumpPreset);
    for (auto& [key, entry] : portraits) {
        DeleteObject(std::get<0>(entry));
    }
    portraits.clear();
    if (initial_->rewind.record_hz != 30 && initial_->rewind.record_hz != 60 &&
        initial_->rewind.record_hz != 120)
        throw std::runtime_error("record_hz 只能是 30 / 60 / 120");
    if (!std::isfinite(initial_->rewind.fast_bullet_margin) || initial_->rewind.fast_bullet_margin < 0 ||
        initial_->rewind.fast_bullet_margin > 64)
        throw std::runtime_error("fast_bullet_margin 需在 0–64 逻辑像素之间");
    simulation = std::make_unique<Simulation>(config.historyBudgetMB * 1024 * 1024);
    simulation->setHooks(scripts.get());
    simulation->setObjects(objectLibrary);
    simulation->setAnimations(animationLibrary);
    simulation->setBarrage(editor.barrage);
    simulation->setMaps(maps);
    simulation->setEvents(eventMaps, commonEvents);
    simulation->reset(*initial_, reason);
    camera.snapMode = config.cameraSnap == "crisp" ? SnapMode::Crisp : SnapMode::Smooth;
    camera.smoothTime = config.cameraSmooth;
    camera.snapThreshold = config.cameraThreshold;
    camera.deadzoneW = config.cameraDeadzoneW;
    camera.deadzoneH = config.cameraDeadzoneH;
    editor.set(initial_->level, eventMaps[editMap], commonEvents, editMap);
    camera.update(*initial_, 0, true);
    if (initialized) {
        renderer.setResources(&resources);
        renderer.environment = editor.level.background;
        SetWindowTextW(window, (L"Echo Engine — " + wide(config.title)).c_str());
        SendMessageW(controls[Snap], CB_SETCURSEL, camera.snapMode == SnapMode::Crisp ? 0 : 1, 0);
        setNumber(WindowSeconds, initial_->rewind.history_seconds);
        setNumber(Fog, config.fog);
        setNumber(Wind, config.wind);
        SendMessageW(controls[RecordHz], CB_SETCURSEL,
                     initial_->rewind.record_hz == 30   ? 0
                     : initial_->rewind.record_hz == 60 ? 1
                                                        : 2,
                     0);
        SendMessageW(controls[Enabled], BM_SETCHECK, initial_->rewind.enabled, 0);
        SendMessageW(controls[Environment], CB_SETCURSEL, editor.level.background, 0);
        SendMessageW(controls[Weather], CB_SETCURSEL, config.weather, 0);
        SendMessageW(controls[Strength], CB_SETCURSEL, int(std::round(config.weatherStrength * 5)), 0);
        SendMessageW(controls[Fluid], BM_SETCHECK, config.fluid, 0);
        SendMessageW(controls[Raymarch], BM_SETCHECK, config.raymarch, 0);
        SendMessageW(controls[Sunlight], BM_SETCHECK, config.sunlight, 0);
        SendMessageW(controls[Fireflies], BM_SETCHECK, config.fireflies, 0);
        loadTileAtlas();
        loadWorkspaceSettings();
        refreshWorkspace(true);
    }
}
void App::save() {
    if (resources.published)
        return;
    finishBrush();
    if (eventEditor && IsWindow(eventEditor) && eventId >= 0)
        eventCommand(EventApply);
    initial_->level = editor.level;
    maps[editMap] = editor.level;
    eventMaps[editMap] = editor.events;
    commonEvents = editor.common;
    std::map<std::string, std::string> files{{"events/common.echo", saveEvents(editor.common, true)},
                                             {"database.echo", saveDatabase(database)},
                                             {"objects.echo", saveObjects(objectLibrary)},
                                             {"animations.echo", saveAnimations(animationLibrary)},
                                             {"barrage.echo", saveBarrage(editor.barrage)},
                                             {"assets.echo", saveAssets(assetLibrary)},
                                             {"project.echo", projectText()}};
    for (std::size_t i = 0; i < maps.size(); ++i) {
        files[config.maps[i]] = saveLevel(maps[i]);
        files[config.maps[i] + ".events"] = saveEvents(eventMaps[i]);
    }
    commitProjectFiles(resources.root, files);
    resources.clearEditorOverlay();
    editor.dirty = false;
    message = "工程已保存";
}
std::string App::projectText() {
    std::string mapNames;
    for (auto& name : config.maps) {
        if (!mapNames.empty())
            mapNames += ';';
        mapNames += name;
    }
    std::map<std::string, std::string> changes{
        {"camera_snap_mode", camera.snapMode == SnapMode::Crisp ? "crisp" : "smooth"},
        {"camera_smooth_time", std::to_string(camera.smoothTime)},
        {"camera_snap_threshold", std::to_string(camera.snapThreshold)},
        {"camera_deadzone_w", std::to_string(camera.deadzoneW)},
        {"camera_deadzone_h", std::to_string(camera.deadzoneH)},
        {"collision_rules", writeCollisionRules(initial_->collisions)},
        {"player_hurt", writeHurt(initial_->hurt)},
        {"player_focus", writeFocus(initial_->focus)},
        {"player_mobility", writeMobility(initial_->mobility)},
        {"texture_filter", std::to_string(renderer.textureFilter)},
        {"upscale_filter", std::to_string(renderer.upscaleFilter)},
        {"anti_aliasing", std::to_string(renderer.antiAliasing)},
        {"render_sharpness", std::to_string(renderer.renderSharpness)},
        {"frame_limit", std::to_string(config.frameLimit)},
        {"vsync", config.vsync ? "1" : "0"},
        {"keyboard_bindings", writeKeys(config.keys)},
        {"maps", mapNames},
        {"level", config.level},
        {"environment", std::to_string(renderer.environment)},
        {"weather", std::to_string(renderer.weather)},
        {"weather_strength", std::to_string(renderer.weatherStrength)},
        {"wind", std::to_string(renderer.wind)},
        {"fog", std::to_string(renderer.fog)},
        {"music", config.music},
        {"music_loop", config.musicLoop ? "1" : "0"},
        {"music_loop_start", std::to_string(config.loopStart)},
        {"music_loop_end", std::to_string(config.loopEnd)},
        {"music_volume", std::to_string(config.musicVolume)},
        {"se_volume", std::to_string(config.seVolume)},
        {"music_fade_in", std::to_string(config.fadeIn)},
        {"jump_preset", std::to_string(initial_->jumpPreset)},
        {"bullet_preset", std::to_string(initial_->bulletPreset)},
        {"sunlight", renderer.sunlight ? "1" : "0"},
        {"fireflies", renderer.fireflies ? "1" : "0"},
        {"tile_sprite", config.tileSprite},
        {"tile_frame", std::to_string(config.tileFrame)},
        {"fluid", renderer.fluid ? "1" : "0"},
        {"raymarch", renderer.raymarch ? "1" : "0"},
        {"background_shader", config.backgroundShader},
        {"map_transition", writeTransition(initial_->mapTransition)},
        {"death_transition", writeTransition(initial_->deathTransition)},
        {"transition_shader", config.transitionShader},
        {"rewind_history_seconds", std::to_string(initial_->rewind.history_seconds)},
        {"rewind_record_hz", std::to_string(initial_->rewind.record_hz)},
        {"rewind_enabled", initial_->rewind.enabled ? "1" : "0"},
        {"rewind_fast_stop_near_bullets", initial_->rewind.fast_stop_near_bullets ? "1" : "0"},
        {"rewind_fast_bullet_margin", std::to_string(initial_->rewind.fast_bullet_margin)}};
    for (int i = 0; i < 32; ++i)
        changes["se_" + std::to_string(i + 1)] = config.effects[i];
    std::istringstream s(resources.text("project.echo"));
    std::ostringstream out;
    std::string line;
    while (std::getline(s, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto pos = line.find('=');
        auto key = line.substr(0, pos);
        auto found = changes.find(key);
        if (found != changes.end()) {
            out << key << '=' << found->second << '\n';
            changes.erase(found);
        } else
            out << line << '\n';
    }
    for (auto& [key, value] : changes)
        out << key << '=' << value << '\n';
    return out.str();
}
void App::brush(int x, int y, bool start) {
    if (x < renderer.offsetX || y < renderer.offsetY || x >= renderer.offsetX + 320 * renderer.scale ||
        y >= renderer.offsetY + 180 * renderer.scale)
        return;
    float worldX = (x - renderer.offsetX) / float(renderer.scale) / pixelsPerUnit + camera.renderX,
          worldY = (y - renderer.offsetY) / float(renderer.scale) / pixelsPerUnit + camera.renderY;
    if (start) {
        brushX = worldX;
        brushY = worldY;
    }
    float distance = std::hypot(worldX - brushX, worldY - brushY);
    int steps = std::max(1, int(distance / 8));
    if (editor.tool >= 10)
        editor.paint(worldX, worldY, erasing);
    else
        for (int i = 1; i <= steps; ++i)
            editor.paint(brushX + (worldX - brushX) * i / steps, brushY + (worldY - brushY) * i / steps,
                         erasing);
    if (editor.tool == 23 && start && editor.selectedEmitter)
        for (std::size_t i = 0; i < editor.barrage.emitters.size(); ++i)
            if (editor.barrage.emitters[i].id == editor.selectedEmitter)
                barrageEmitterIndex = int(i);
    if (editor.tool == 22 && editor.selectedObject >= 0 &&
        editor.selectedObject < int(editor.level.objects.count)) {
        auto& p = editor.level.objects[editor.selectedObject];
        if (const auto* t = findObjectTemplate(objectLibrary, p.templateId);
            t && !(p.overrides & OverrideSize)) {
            p.width = t->width;
            p.height = t->height;
        }
    }
    if (editor.tool == 22 && start && objectsEditor) {
        objectInstanceIndex = editor.selectedObject;
        objectForm();
    }
    brushX = worldX;
    brushY = worldY;
    if (editor.selectedLight >= 0 && editor.selectedLight < int(editor.level.lights.count)) {
        auto& lamp = editor.level.lights[editor.selectedLight];
        setNumber(LightRadius, lamp.radius);
        setNumber(LightIntensity, lamp.intensity);
    }
    if (editor.selectedPlatform >= 0 && editor.selectedPlatform < int(editor.level.platforms.count)) {
        auto& p = editor.level.platforms[editor.selectedPlatform];
        setNumber(PlatformAmplitude, p.amplitude);
        setNumber(PlatformSpeed, p.speed);
    }
}
void App::finishBrush() {
    bool finishedEmitter = dragging && editor.tool == 23;
    dragging = false;
    editor.end();
    refreshWorkspace();
    if (GetCapture() == canvas)
        ReleaseCapture();
    if (finishedEmitter) {
        if (barrageTargetEditor)
            SendMessageW(barrageTargetEditor, WM_CLOSE, 0, 0);
        barrageForm();
        if (barrageEditor)
            resetBarragePreview();
    }
}
void App::refreshAuthoringHistory() {
    refreshWorkspace();
    if (barrageTargetEditor)
        SendMessageW(barrageTargetEditor, WM_CLOSE, 0, 0);
    barrageForm();
    if (barrageEditor)
        resetBarragePreview();
    if (eventEditor) {
        auto id = eventId;
        eventRefreshList();
        if (findEvent(eventCommon ? editor.common : editor.events, id))
            eventLoad(id);
        else {
            eventId = -1; // Closing must not re-apply the future draft after undo.
            SendMessageW(eventEditor, WM_CLOSE, 0, 0);
        }
    }
    if (objectsEditor)
        objectForm();
    renderer.environment = editor.level.background;
    SendMessageW(controls[Environment], CB_SETCURSEL, renderer.environment, 0);
}
void App::command(int id, int notification) {
    if (resources.published)
        return;
    if (id == DebugMuteMusic) {
        debugMuteMusic = !debugMuteMusic;
        updateAudioOutputPolicy();
        saveWorkspaceSettings();
        message = debugMuteMusic ? "调试时已静音 BGM（音效保留）" : "调试时已开启 BGM";
        refreshChrome();
        return;
    }
    if (id == RenderPixelPreset) {
        renderer.setSampling(0, 0, 0, .25f);
        camera.snapMode = SnapMode::Crisp;
        config.textureFilter = config.upscaleFilter = config.antiAliasing = 0;
        config.cameraSnap = "crisp";
        SendMessageW(controls[Snap], CB_SETCURSEL, 0, 0);
        if (focusEditor) {
            SendMessageW(controls[RenderTexture], CB_SETCURSEL, 0, 0);
            SendMessageW(controls[RenderUpscale], CB_SETCURSEL, 0, 0);
            SendMessageW(controls[RenderAA], CB_SETCURSEL, 0, 0);
        }
        editor.dirty = true;
        message = "已应用像素清晰预设：最近邻纹理、整数倍画布、关闭抗锯齿、镜头像素吸附";
        refreshChrome();
        return;
    }

    if (resources.published)
        return;
    if (id == AnimationNativePixels) {
        setNumber(AnimationScale, 1.0f / pixelsPerUnit);
        animationCommand(AnimationApply);
        return;
    }
    if (id >= MobilityEditor && id <= FeatureSelectTool) {
        mobilityCommand(id, notification);
        return;
    }
    if (id >= EventChoiceOpen && id <= ChoiceApply) {
        choiceCommand(id, notification);
        return;
    }
    if (id >= WorkspaceFile && id <= WorkspaceTileLast) {
        workspaceCommand(id, notification);
        return;
    }
    if (id >= CollisionEditor && id <= CollisionLast) {
        collisionCommand(id);
        return;
    }
    if (id >= SnapshotCompareEditor && id <= SnapshotExport) {
        snapshotCompareCommand(id);
        return;
    }
    if (id >= RewindRulesEditor && id <= RewindRulesApply) {
        rewindRulesCommand(id);
        return;
    }
    if (id >= InputEditor && id <= InputApply) {
        inputCommand(id);
        return;
    }
    if (id >= FrameRateEditor && id <= FrameRateApply) {
        frameRateCommand(id);
        return;
    }
    if (id >= FocusEditor && id <= HurtPreview) {
        focusCommand(id, notification);
        return;
    }
    if (id >= BarrageEditor && id <= BarrageRedo) {
        barrageCommand(id, notification);
        return;
    }
    if (id >= AnimationsEditor && id <= AnimationUnbind) {
        animationCommand(id, notification);
        return;
    }
    if (id >= AssetsEditor && id <= AssetThumbnail) {
        assetCommand(id, notification);
        return;
    }
    if (id >= ObjectsEditor && id <= ObjectInstanceDelete) {
        objectCommand(id, notification);
        return;
    }
    if (id >= DatabaseEditor && id <= DataApply) {
        databaseCommand(id);
        return;
    }
    if (id >= MapToolsEditor && id <= MapHide) {
        mapToolsCommand(id);
        return;
    }
    if (id >= DebuggerEditor && id <= DebugControlClocks) {
        debuggerCommand(id);
        return;
    }
    if (id >= ProjectHealthEditor && id <= RecoveryRestore) {
        healthCommand(id);
        return;
    }
    if (id >= TransitionEditor && id <= TransitionApply) {
        transitionCommand(id);
        return;
    }
    if (id == TilePaletteEditor || (id >= 400 && id < 416)) {
        tileCommand(id);
        return;
    }
    if (id == EventCatalogSearch && notification == EN_CHANGE) {
        eventCommand(id, notification);
        return;
    }
    if (auto found = controls.find(id); found != controls.end()) {
        wchar_t type[32]{};
        GetClassNameW(found->second, type, 32);
        if (std::wstring_view(type) == L"Edit" && id != Fog && id != Wind)
            return;
        if (std::wstring_view(type) == L"ComboBox" && notification != CBN_SELCHANGE)
            return;
    }
    if (id >= EventEditor && id <= EventDraftRedo) {
        eventCommand(id, notification);
        return;
    }
    if (id >= 1 && id <= 17) {
        finishBrush();
        editor.emitterPlacementArmed = false;
        editor.tool = id;
        editing = true;
        message.clear();
        refreshWorkspace();
        refreshChrome();
        SetFocus(canvas);
        return;
    }
    if ((id == Environment || id == Weather || id == Strength || id == Snap || id == Layer ||
         id == TileIndex || id == JumpPreset || id == BulletPreset) &&
        notification != CBN_SELCHANGE)
        return;
    if (id >= AudioEditor && id <= SoundPreview) {
        audioCommand(id);
        return;
    }
    if (id >= LayerEditor && id <= LayerImageClear) {
        layerCommand(id);
        return;
    }
    if (id >= MapsEditor && id <= PortalApply) {
        mapCommand(id);
        return;
    }
    if (id == DialogEditor || id == DialogApply || id == DialogImport) {
        dialogueCommand(id);
        return;
    }
    if (id == JumpPreset) {
        int selected = selection(id) - 1;
        initial_->jumpPreset = selected;
        if (selected >= 0)
            applyJumpPreset(*initial_, selected);
        simulation->world().jump = initial_->jump;
        simulation->world().jumpPreset = selected;
        editor.dirty = true;
    } else if (id == BulletPreset) {
        initial_->bulletPreset = selection(id) - 1;
        simulation->world().bulletPreset = initial_->bulletPreset;
        simulation->world().aster.attack = {};
        simulation->world().aster.resting = true;
        simulation->world().aster.attackTimer = 1;
        editor.dirty = true;
    } else if (id == Sunlight) {
        renderer.sunlight = SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
        editor.dirty = true;
    } else if (id == Fireflies) {
        renderer.fireflies = SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
        editor.dirty = true;
    } else if (id == Layer) {
        editor.layer = selection(id);
        if (mapToolsEditor) {
            SendMessageW(controls[MapLock], BM_SETCHECK, editor.layerLocked[editor.layer], 0);
            SendMessageW(controls[MapHide], BM_SETCHECK, editorHidden[editor.layer], 0);
        }
    } else if (id == TileIndex)
        editor.tileIndex = selection(id) + 1;
    else if (id == AutoCollision)
        editor.autoCollision = SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
    else if (id == LightApply) {
        if (editor.selectedLight >= 0 && editor.selectedLight < int(editor.level.lights.count)) {
            editor.begin();
            auto& lamp = editor.level.lights[editor.selectedLight];
            lamp.radius = std::clamp(number(LightRadius), 4.0f, 2048.0f);
            lamp.intensity = std::clamp(number(LightIntensity), 0.0f, 8.0f);
            editor.end();
            editor.dirty = true;
        }
    } else if (id == ImportTiles) {
        auto path = choose(false, L"Tile atlas\0*.png;*.bmp\0\0",
                           L"Import tile atlas (4x4 tiles, tile_frame in project.echo)", L"png");
        if (!path.empty()) {
            config.tileSprite = importResource(path, "assets", AssetKind::Image);
            save();
            renderer.setResources(&resources);
            loadTileAtlas();
            refreshWorkspace();
            message = "图块图集已导入";
        }
    } else if (id == Fog || id == Wind) {
        if (notification != EN_KILLFOCUS)
            return;
        if (id == Fog)
            renderer.fog = std::clamp(number(id), 0.0f, 1.0f);
        else
            renderer.wind = std::clamp(number(id), -3.0f, 3.0f);
        editor.dirty = true;
    } else if (id == Play) {
        finishBrush();
        if (eventEditor && IsWindow(eventEditor))
            eventCommand(EventApply);
        initial_->level = editor.level;
        initial_->kino = {};
        initial_->kino.x = editor.level.spawn.x;
        initial_->kino.y = editor.level.spawn.y;
        initial_->activeMap = editMap;
        maps[editMap] = editor.level;
        eventMaps[editMap] = editor.events;
        commonEvents = editor.common;
        scripts->setObjects(objectLibrary);
        scripts->setAnimations(animationLibrary);
        simulation->setObjects(objectLibrary);
        simulation->setAnimations(animationLibrary);
        simulation->setBarrage(editor.barrage);
        simulation->setMaps(maps);
        simulation->setEvents(eventMaps, commonEvents);
        previewAudio = false;
        initial_->aster.x = editor.level.bossSpawn.x;
        initial_->aster.y = editor.level.bossSpawn.y;
        simulation->reset(*initial_, HistoryClearReason::Play);
        editing = false;
        camera.update(simulation->world(), 0, true);
        message.clear();
        SetFocus(canvas);
    } else if (id == Edit) {
        debugPaused = false;
        debugStepRequested = false;
        renderer.debugCollisions = false;
        previewAudio = false;
        editing = true;
        message.clear();
        audio.pause(true);
    } else if (id == Save)
        save();
    else if (id == Undo || id == Redo) {
        if (!editing)
            return; // Authoring undo never mutates an active gameplay timeline.
        finishBrush();
        if ((id == Undo && !editor.undoCount()) || (id == Redo && !editor.redoCount()))
            return;
        if (id == Undo)
            editor.undo();
        else
            editor.redo();
        refreshAuthoringHistory();
        message = id == Undo ? "已撤销" : "已重做";
    } else if (id == Environment) {
        renderer.environment = selection(id);
        editor.level.background = renderer.environment;
        editor.dirty = true;
    } else if (id == Weather) {
        renderer.weather = selection(id);
        editor.dirty = true;
    } else if (id == Strength) {
        renderer.weatherStrength = selection(id) / 5.0f;
        editor.dirty = true;
    } else if (id == Fluid) {
        renderer.fluid = SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
        editor.dirty = true;
    } else if (id == Raymarch) {
        renderer.raymarch = SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
        editor.dirty = true;
    } else if (id == Snap) {
        camera.snapMode = selection(id) == 0 ? SnapMode::Crisp : SnapMode::Smooth;
        editor.dirty = true;
    } else if (id == Apply) {
        auto c = simulation->world().rewind;
        c.history_seconds = number(WindowSeconds);
        c.record_hz = selection(RecordHz) == 0 ? 30 : selection(RecordHz) == 1 ? 60 : 120;
        c.enabled = SendMessageW(controls[Enabled], BM_GETCHECK, 0, 0) == BST_CHECKED;
        simulation->configureRewind(c);
        initial_->rewind = simulation->world().rewind;
        renderer.fog = std::clamp(number(Fog), 0.0f, 1.0f);
        renderer.wind = std::clamp(number(Wind), -3.0f, 3.0f);
        editor.dirty = true;
        message = "场景参数已应用";
    } else if (id == PlatformApply) {
        if (editor.selectedPlatform >= 0 &&
            editor.selectedPlatform < int(editor.level.platforms.count)) {
            editor.begin();
            auto& p = editor.level.platforms[editor.selectedPlatform];
            p.amplitude = std::clamp(number(PlatformAmplitude), 0.0f, 1024.0f);
            p.speed = std::clamp(number(PlatformSpeed), 0.0f, 10.0f);
            editor.end();
            editor.dirty = true;
        }
    } else if (id == Open) {
        auto path = choose(false, L"Echo Project\0project.echo\0\0", L"打开 Echo Engine 项目", L"echo");
        if (!path.empty()) {
            save();
            auto oldRoot = resources.root;
            resources.root = path.parent_path();
            try {
                loadProject();
            } catch (...) {
                resources.root = oldRoot;
                loadProject();
                throw;
            }
            editing = true;
            message = "工程已打开";
        }
    } else if (id == New) {
        auto path =
            choose(true, L"Echo Project\0*.echo\0\0", L"选择新项目的 project.echo 路径", L"echo");
        if (!path.empty()) {
            auto target = std::filesystem::weakly_canonical(path.parent_path()),
                 original = std::filesystem::weakly_canonical(resources.root);
            auto relation = target.lexically_relative(original);
            if (!relation.empty() && *relation.begin() != "..")
                throw std::runtime_error("新工程不能建在模板工程文件夹内");
            if (std::filesystem::exists(target / "project.echo"))
                throw std::runtime_error("请选择一个新的工程文件夹");
            save();
            std::filesystem::create_directories(target);
            for (auto& entry : std::filesystem::recursive_directory_iterator(original)) {
                auto relative = entry.path().lexically_relative(original);
                if (!relative.empty() && *relative.begin() == ".echo")
                    continue;
                if (entry.is_regular_file()) {
                    auto dest = target / relative;
                    std::filesystem::create_directories(dest.parent_path());
                    std::filesystem::copy_file(entry.path(), dest);
                }
            }
            resources.root = target;
            loadProject();
            editing = true;
            message = "已以当前工程为模板新建工程";
        }
    } else if (id == ExportPublic || id == ExportEncrypted) {
        save();
        auto path =
            choose(true, L"Windows Game\0*.exe\0\0", L"导出作品 · 创建同名 release 目录", L"exe");
        if (!path.empty()) {
            auto output = path.parent_path() / (path.stem().wstring() + L"_release");
            resources.exportGame(executable, output, id == ExportEncrypted);
            if (executable.filename() != path.filename())
                std::filesystem::rename(output / executable.filename(), output / path.filename());
            message = "已导出：" + pathUtf8(output);
            MessageBoxW(window, wide(message).c_str(), L"Echo Engine", MB_OK);
        }
    } else if (id == Reload) {
        save();
        loadProject(HistoryClearReason::ScriptReload);
        message = "资源与脚本已重载";
    } else if (id == CustomShader) {
        auto path = choose(false, L"GLSL Fragment\0*.frag;*.glsl\0\0", L"导入自定义背景 GLSL", L"frag");
        if (!path.empty()) {
            std::ifstream shaderFile(path);
            std::string source((std::istreambuf_iterator<char>(shaderFile)), {});
            renderer.setBackgroundSource(source);
            config.backgroundShader = importResource(path, "shaders", AssetKind::Shader);
            save();
            renderer.setResources(&resources);
            message = "自定义背景着色器已加载";
        }
    } else if (id == Stress) {
        stress = !stress;
        simulation->world().kino.invulnTimer = stress ? 100000 : 0;
        simulation->world().aster.enabled = !stress;
        if (stress) {
            simulation->world().bullets.clear();
            for (int i = 0; i < maxBullets; ++i) {
                Bullet b;
                b.x = 50 + (i % 64) * 18;
                b.y = 50 + (i / 64) * 16;
                b.angle = i * .3f;
                b.vx = std::cos(b.angle) * 12;
                b.vy = std::sin(b.angle) * 12;
                b.expireAge = 100;
                spawnBullet(simulation->world(), b);
            }
        }
        editing = false;
        message = stress ? "弹幕压力测试：2048 发" : "压力测试已结束";
    }
    refreshChrome();
    if (id != LightRadius && id != LightIntensity && id != WindowSeconds && id != PlatformAmplitude &&
        id != PlatformSpeed && id != Fog && id != Wind)
        SetFocus(canvas);
}
} // namespace echo
