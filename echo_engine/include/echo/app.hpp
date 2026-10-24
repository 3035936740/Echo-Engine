#pragma once
#include "audio.hpp"
#include "editor.hpp"
#include "database.hpp"
#include "event_edit.hpp"
#include "renderer.hpp"
#include "script.hpp"
#include "barrage.hpp"
#include "snapshot_diff.hpp"
#include "ui.hpp"
#include "workspace_view.hpp"
#include <map>
#include <string>
#include <tuple>
namespace echo {
std::wstring wide(const std::string&);
enum Command {
    Play = 100,
    Edit,
    Save,
    Undo,
    Redo,
    Open,
    New,
    ExportPublic,
    ExportEncrypted,
    Reload,
    Stress,
    CustomShader,
    Environment = 150,
    Weather,
    Strength,
    Fluid,
    Raymarch,
    Snap,
    WindowSeconds,
    RecordHz,
    Enabled,
    Apply,
    PlatformAmplitude,
    PlatformSpeed,
    PlatformApply,
    Fog,
    Wind,
    Sunlight,
    Fireflies,
    LightRadius,
    LightIntensity,
    LightApply,
    Layer,
    TileIndex,
    AutoCollision,
    ImportTiles,
    JumpPreset,
    BulletPreset,
    DialogEditor,
    DialogSpeaker,
    DialogPortrait,
    DialogText,
    DialogApply,
    DialogImport,
    DialogX,
    DialogY,
    DialogW,
    DialogH,
    ThemeMode,
    AudioEditor,
    MusicImport,
    MusicPath,
    MusicLoopEnabled,
    MusicLoopStart,
    MusicLoopEnd,
    MusicVolume,
    SoundVolume,
    MusicFadeTime,
    AudioApply,
    MusicPreview,
    MusicPause,
    MusicStop,
    MusicFadeOut,
    MusicFadeIn,
    SoundSlot,
    SoundPath,
    SoundImport,
    SoundPreview,
    MapsEditor,
    MapSelect,
    MapNew,
    MapStart,
    MapClamp,
    PortalTarget,
    PortalX,
    PortalY,
    PortalSpawn,
    PortalApply,
    LayerEditor,
    LayerImageImport,
    LayerImagePath,
    LayerParallax,
    LayerImageX,
    LayerImageY,
    LayerImageW,
    LayerImageH,
    LayerImageApply,
    LayerImageClear,
    EventEditor = 300,
    EventScope,
    EventList,
    EventPageSelect,
    EventPageNew,
    EventPageCopy,
    EventPageDelete,
    EventName,
    EventTriggerMode,
    EventSwitchCondition,
    EventSwitchConditionValue,
    EventVariableCondition,
    EventVariableMinimum,
    EventSelfCondition,
    EventSelfConditionValue,
    EventSolid,
    EventGraphic,
    EventCommandList,
    EventCommandType,
    EventTarget,
    EventFlag,
    EventValue,
    EventX,
    EventY,
    EventSpeaker,
    EventPortrait,
    EventContent,
    EventInsert,
    EventUpdate,
    EventRemove,
    EventUp,
    EventDown,
    EventApply,
    EventNew,
    EventDelete,
    EventPreset,
    EventPresetApply,
    EventImportPortrait,
    EventTargetLabel,
    EventValueLabel,
    EventXLabel,
    EventYLabel,
    EventFlagLabel,
    EventSpeakerLabel,
    EventPortraitLabel,
    EventHelp,
    EventControlFirst,
    EventControlLast = EventControlFirst + 8,
    EventControlHint,
    EventPageScriptEnabled,
    EventPageScriptSource,
    EventCatalogCategory,
    EventCatalogSearch,
    EventCopy,
    EventCut,
    EventPaste,
    EventFold,
    EventExpand,
    EventDraftUndo,
    EventDraftRedo,
    TilePaletteEditor = 380,
    TransitionEditor = 420,
    TransitionScope,
    TransitionPreset,
    TransitionClose,
    TransitionHold,
    TransitionOpen,
    TransitionDirection,
    TransitionEasing,
    TransitionCenter,
    TransitionRed,
    TransitionGreen,
    TransitionBlue,
    TransitionDetail,
    TransitionOverride,
    TransitionShaderPath,
    TransitionImport,
    TransitionPreview,
    TransitionApply,
    DatabaseEditor = 460,
    DataCategory,
    DataList,
    DataName,
    DataNote,
    DataInitial,
    DataNew,
    DataDelete,
    DataLimit,
    DataStatus,
    DataAuto,
    DataInitialLabel,
    DataLimitLabel,
    DataRuntime,
    DataOpenObjects,
    DataOpenAnimations,
    DataUndo,
    DataRedo,
    DataPreset,
    DataApply,
    MapToolsEditor = 480,
    MapToolMode,
    MapBrushType,
    MapCopy,
    MapPaste,
    MapClear,
    MapFlipH,
    MapFlipV,
    MapAutoTile,
    MapGrid,
    MapCollisions,
    MapLock,
    MapHide,
    DebuggerEditor = 520,
    DebugPause,
    DebugStep,
    DebugEvents,
    DebugData,
    DebugValue,
    DebugWrite,
    DebugTimeline,
    DebugSeek,
    DebugOldest,
    DebugSummary,
    DebugHistoryInfo,
    DebugHistoryReason,
    DebugHistoryClear,
    DebugControlSummary,
    DebugControlClocks,
    ProjectHealthEditor = 560,
    HealthList,
    HealthRefresh,
    RecoverySave,
    RecoveryRestore,
    ObjectsEditor = 600,
    ObjectList,
    ObjectPresetSelect,
    ObjectNew,
    ObjectCopy,
    ObjectDelete,
    ObjectName,
    ObjectKindSelect,
    ObjectBehaviorSelect,
    ObjectSprite,
    ObjectWidth,
    ObjectHeight,
    ObjectSpeed,
    ObjectHP,
    ObjectDamage,
    ObjectRange,
    ObjectDetect,
    ObjectInterval,
    ObjectGravity,
    ObjectSolid,
    ObjectRide,
    ObjectCrush,
    ObjectCategory,
    ObjectMask,
    ObjectFaction,
    ObjectPickupVariable,
    ObjectPickupValue,
    ObjectApply,
    ObjectImport,
    ObjectPlace,
    ObjectInstances,
    ObjectX,
    ObjectY,
    ObjectInstanceSpeed,
    ObjectInstanceHP,
    ObjectInstanceDamage,
    ObjectOverrideSpeed,
    ObjectOverrideHP,
    ObjectOverrideDamage,
    ObjectInstanceEvent,
    ObjectPersistent,
    ObjectInstanceApply,
    ObjectInherit,
    ObjectInstanceDelete,
    AssetsEditor = 650,
    AssetSearch,
    AssetFolder,
    AssetType,
    AssetList,
    AssetName,
    AssetTags,
    AssetPath,
    AssetInfo,
    AssetUses,
    AssetImport,
    AssetScan,
    AssetApply,
    AssetMove,
    AssetRelink,
    AssetDelete,
    AssetCopyID,
    AssetThumbnail,
    AnimationsEditor = 700,
    AnimationList,
    AnimationNew,
    AnimationCopy,
    AnimationDelete,
    AnimationName,
    AnimationSprite,
    AnimationImport,
    AnimationWidth,
    AnimationHeight,
    AnimationScale,
    AnimationAnchorX,
    AnimationAnchorY,
    AnimationLoop,
    AnimationFlip,
    AnimationApply,
    AnimationFrames,
    AnimationColumn,
    AnimationRow,
    AnimationSeconds,
    AnimationFrameApply,
    AnimationFrameAdd,
    AnimationFrameRemove,
    AnimationFrameUp,
    AnimationFrameDown,
    AnimationSplitColumn,
    AnimationSplitRow,
    AnimationSplitCount,
    AnimationSplitFPS,
    AnimationSplit,
    AnimationCollision,
    AnimationCollisionX,
    AnimationCollisionY,
    AnimationCollisionW,
    AnimationCollisionH,
    AnimationHurt,
    AnimationHurtX,
    AnimationHurtY,
    AnimationHurtW,
    AnimationHurtH,
    AnimationAttack,
    AnimationAttackX,
    AnimationAttackY,
    AnimationAttackW,
    AnimationAttackH,
    AnimationDamage,
    AnimationSound,
    AnimationVariable,
    AnimationDelta,
    AnimationPreview,
    AnimationPreviewPlay,
    AnimationPreviewFacing,
    AnimationBindingActor,
    AnimationBindingState,
    AnimationBind,
    AnimationUnbind,
    BarrageEditor = 800,
    BarragePatterns,
    BarragePreset,
    BarrageNew,
    BarrageCopy,
    BarrageDelete,
    BarrageName,
    BarrageShapeSelect,
    BarrageAimSelect,
    BarrageCount,
    BarrageDamage,
    BarrageGroup,
    BarragePalette,
    BarrageTerrain,
    BarrageSpeed,
    BarrageAcceleration,
    BarrageMaxSpeed,
    BarrageLife,
    BarrageDirection,
    BarrageSpread,
    BarrageTurn,
    BarrageTurnSeconds,
    BarrageVisualRadius,
    BarrageHitRadius,
    BarrageWobble,
    BarrageFrequency,
    BarrageGrowth,
    BarrageRotation,
    BarrageRelease,
    BarrageJitter,
    BarragePatternApply,
    BarrageOverflow,
    BarrageEmitters,
    BarrageEmitterNew,
    BarrageEmitterCopy,
    BarrageEmitterDelete,
    BarrageEmitterName,
    BarrageMap,
    BarrageOriginSelect,
    BarrageObject,
    BarrageX,
    BarrageY,
    BarrageGate,
    BarrageSeed,
    BarrageDelay,
    BarrageLoopDelay,
    BarrageEnabled,
    BarrageLoop,
    BarragePersistent,
    BarrageEmitterApply,
    BarrageWaves,
    BarrageWaveNew,
    BarrageWaveDelete,
    BarrageWaveUp,
    BarrageWaveDown,
    BarrageWavePattern,
    BarrageWaveRepeat,
    BarrageWaveDelay,
    BarrageWarning,
    BarrageInterval,
    BarrageAngleStep,
    BarrageWaveApply,
    BarragePreview,
    BarragePreviewPlay,
    BarragePreviewReset,
    BarragePreviewInfo,
    BarrageTargetEditor,
    BarrageTargetSelect,
    BarrageTargetObject,
    BarrageTargetFilter,
    BarrageTargetFaction,
    BarrageTargetRange,
    BarrageTargetX,
    BarrageTargetY,
    BarrageTargetExclude,
    BarrageTargetApply,
    BarrageTargetHint,
    BarrageMapPlace,
    BarrageUndo,
    BarrageRedo,
    FocusEditor = 900,
    FocusEnabled,
    FocusIndicator,
    FocusActivation,
    FocusCondition,
    FocusMap,
    FocusSwitch,
    FocusRatio,
    FocusKey,
    FocusGround,
    FocusAir,
    FocusInstant,
    HurtSourceControl,
    HurtMethodControl,
    HurtTouching,
    HurtMirror,
    HurtProjectileBox,
    HurtPreset,
    HurtPresetApply,
    FocusApply,
    HurtRegionList,
    HurtRegionNew,
    HurtRegionCopy,
    HurtRegionDelete,
    HurtShapeControl,
    HurtRegionEnabled,
    HurtRegionFocusOnly,
    HurtMaskBullet,
    HurtMaskContact,
    HurtMaskMelee,
    HurtAnchorX,
    HurtAnchorY,
    HurtOffsetX,
    HurtOffsetY,
    HurtRadius,
    HurtWidth,
    HurtHeight,
    HurtAngle,
    HurtVertices,
    HurtRegionApply,
    RenderTexture,
    RenderUpscale,
    RenderAA,
    RenderSharpness,
    HurtShowControl,
    HurtStyleControl,
    HurtDisplayScale,
    HurtOpacity,
    HurtOutline,
    HurtShowKey,
    HurtRed,
    HurtGreen,
    HurtBlue,
    HurtPreview,
    FrameRateEditor = 980,
    FrameRatePreset,
    FrameRateCustom,
    FrameVsync,
    FrameRateApply,
    SnapshotCompareEditor = 1000,
    SnapshotSourceA,
    SnapshotSourceB,
    SnapshotPinA,
    SnapshotCompare,
    SnapshotFilter,
    SnapshotRows,
    SnapshotSummary,
    SnapshotBaseline,
    SnapshotExport,
    CollisionEditor = 1100,
    CollisionApply,
    CollisionDefault,
    CollisionSafe,
    CollisionCell = 1120,
    CollisionLast = 1183,
    WorkspaceFile = 1200,
    WorkspaceEdit,
    WorkspaceView,
    WorkspaceDraw,
    WorkspaceGame,
    WorkspaceTools,
    WorkspaceHelp,
    WorkspaceInspector,
    WorkspacePencil,
    WorkspaceRectangle,
    WorkspaceBucket,
    WorkspaceSelection,
    WorkspacePick,
    WorkspaceObjects,
    WorkspaceBarrage,
    WorkspaceMaps,
    WorkspaceItems,
    WorkspaceItemEdit,
    WorkspaceGrid,
    WorkspaceCollisions,
    WorkspaceTerrain,
    WorkspaceOneWay,
    WorkspaceSpike,
    WorkspaceErase,
    WorkspaceLightTheme,
    WorkspaceDarkTheme,
    WorkspaceGuide,
    WorkspaceReset,
    WorkspaceTileFirst = 1300,
    WorkspaceTileLast = 1315,
    InputEditor = 1550,
    InputKeyFirst,
    InputKeyLast = InputKeyFirst + 7,
    InputArrows,
    InputReset,
    InputApply,
    RewindRulesEditor = 1580,
    RewindNearEnabled,
    RewindMargin,
    RewindRulesApply,
    DebugMuteMusic = 1620,
    RenderPixelPreset,
    AnimationNativePixels = 1640,
    EventChoiceOpen = 1650,
    ChoiceList,
    ChoiceLabel,
    ChoiceConditionType,
    ChoiceTarget,
    ChoiceComparison,
    ChoiceThreshold,
    ChoiceHide,
    ChoiceLua,
    ChoiceNew,
    ChoiceDelete,
    ChoiceApply,
    MobilityEditor = 1700,
    MobilityDash,
    MobilityWall,
    MobilityCrush,
    MobilityFirst,
    MobilityLast = MobilityFirst + 11,
    MobilityDashKey,
    MobilityUpKey,
    MobilityApply,
    FeatureList,
    FeatureKindControl,
    FeatureX,
    FeatureY,
    FeatureW,
    FeatureH,
    FeatureSpeed,
    FeatureDelay,
    FeatureReset,
    FeatureEnabled,
    FeatureDraw,
    FeatureApply,
    FeatureDelete,
    FeatureTravelX,
    FeatureTravelY,
    FeatureRepeat,
    FeatureSpeedLabel,
    FeatureDelayLabel,
    FeatureResetLabel,
    FeatureSelectTool
};
class App {
  public:
    HINSTANCE instance{};
    HWND tooltips{}; // native tooltips for the remaining dialog controls
    bool refreshingWorkspace{}, splittingWorkspace{}, chromeTracking{};
    int workspaceMode{}, workspaceWidth = 272, openMenu = -1;
    // Main window chrome is drawn by echo::ui (workspace_view.cpp); only the canvas is a child window.
    float uiScale = 1;
    ui::Fonts uiFonts;
    ui::View chrome;
    ui::Painter chromeSurface;
    ui::Tooltip chromeTip;
    WorkspaceLayout chromeLayout{};
    std::wstring statusText;
    std::vector<int> dockItems; // dock row -> event / template / emitter / feature index
    bool inspectorOpen{};       // drawn scene-settings panel
    ui::Key sliderDrag{};
    float sliderDragValue{};
    std::vector<Property> sceneProperties();
    void applyProperty(int id, float value = 0);
    HWND window{}, canvas{}, status{}, side{}, dialogWindow{}, dialogEditor{}, audioEditor{},
        mapEditor{}, layerEditor{}, eventEditor{}, tilePalette{}, transitionEditor{}, databaseEditor{},
        mapToolsEditor{}, debuggerEditor{}, healthEditor{}, objectsEditor{}, assetsEditor{},
        animationsEditor{}, barrageEditor{}, barrageTargetEditor{}, focusEditor{}, frameRateEditor{},
        snapshotCompareEditor{}, collisionEditor{}, inputEditor{}, rewindRulesEditor{};
    CollisionRules collisionDraft{};
    std::unique_ptr<WorldState> snapshotA, snapshotB;
    std::unique_ptr<SnapshotDiff> snapshotReport;
    std::string snapshotFilterText;
    HurtConfig hurtDraft{};
    FocusConfig focusDraft{};
    int hurtRegionIndex{};
    bool loadingFocus{};
    HBITMAP tileAtlas{}, assetThumbnail{}, animationBitmap{};
    int animationBitmapW{}, animationBitmapH{}, animationClipIndex{}, animationFrameIndex{};
    bool loadingAnimations{}, animationPreviewPlaying{}, animationPreviewFacing{};
    std::uint64_t animationPreviewStarted{};
    std::vector<std::uint64_t> visibleAssets;
    std::uint64_t selectedAsset{};
    bool loadingAssets{};
    int tileAtlasW{}, tileAtlasH{};
    HFONT font{}, strongFont{}, smallFont{};
    HBRUSH windowBrush{}, panelBrush{}, fieldBrush{};
    int sideScroll{}, chromeY = 110, canvasTop = 186, selectedAudioSlot{}, layerEditorIndex{};
    std::map<int, HWND> controls;
    std::map<std::string, std::tuple<HBITMAP, int, int>> portraits;
    std::string dialogueRasterKey;
    Resources resources;
    ProjectConfig config;
    Database database;
    std::vector<Database> databaseUndo, databaseRedo;
    ObjectLibrary objectLibrary;
    AssetCatalog assetLibrary;
    AnimationLibrary animationLibrary;
    BarrageLibrary barragePreviewLibrary;
    std::unique_ptr<WorldState> barragePreviewWorld;
    int barragePatternIndex = -1, barrageEmitterIndex = -1, barrageWaveIndex{};
    bool loadingBarrage{}, barragePreviewPlaying{};
    float barragePreviewX{}, barragePreviewY{};
    SimulationStats barragePreviewStats{};
    struct BarrageTrace {
        std::uint32_t id{}, count{}, head{};
        std::array<Vec2, 128> points{};
    };
    std::array<BarrageTrace, 512> barrageTraces{};
    int objectTemplateIndex{}, objectInstanceIndex = -1;
    int dataCategory{}, dataSlot{};
    bool debugPaused{}, debugStepRequested{}, debugWasPaused{};
    double debugRefreshAt{}, autosaveAt{};
    int debugMap = -1;
    bool editorGrid = true, editorCollisions = true;
    std::array<bool, 3> editorHidden{};
    std::vector<LevelState> maps;
    std::vector<EventMap> eventMaps;
    EventMap commonEvents;
    EventDefinition eventDraft;
    std::bitset<maxEventCommands> eventCollapsed;
    std::vector<EventCommandRow> eventRows;
    std::vector<EventCommand> eventClipboard;
    std::vector<EventDefinition> eventDraftUndo, eventDraftRedo;
    HWND choiceEditor{}, mobilityEditor{};
    bool loadingMobility{};
    void mobilityCommand(int, int = 0);
    void featureControlState();
    void mobilityForm();
    std::vector<ChoiceDefinition> eventChoiceDraft;
    int choiceIndex{};
    bool loadingChoice{};
    void showChoiceEditor();
    void choiceForm();
    void choiceRead();
    void choiceCommand(int, int = 0);
    bool eventCommon{}, loadingEventForm{};
    bool transitionPreview{};
    double transitionPreviewStart{};
    TransitionConfig transitionPreviewConfig{};
    int transitionScope{}, transitionPortal = -1;
    Rect transitionPortalRegion{};
    int eventPage{}, eventId = -1;
    int editMap{};
    std::filesystem::path executable;
    std::unique_ptr<ScriptRuntime> scripts;
    std::unique_ptr<Simulation> simulation;
    std::unique_ptr<WorldState> initial_ = std::make_unique<WorldState>(),
                                visual = std::make_unique<WorldState>();
    Renderer renderer;
    CameraState camera;
    LevelEditor editor;
    Audio audio;
    bool previewAudio{}, verifyMode{};
    bool debugMuteMusic = true; // Editor-only preference, excluded from project/export/snapshots.
    void updateAudioOutputPolicy();
    std::unique_ptr<WorldState> audioPreview = std::make_unique<WorldState>();
    bool darkTheme = false;
    bool editing{}, dragging{}, erasing{}, focused = true, initialized{}, smoke{}, stress{};
    std::string message;
    double tickMS{}, renderMS{};
    int smokeFrames{};
    std::filesystem::path smokePath = "smoke.ppm";
    float brushX{}, brushY{};
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    HWND control(const wchar_t*, const wchar_t*, int, int, int, int, int, DWORD style = 0,
                 HWND parent = nullptr);
    void label(const wchar_t*, int, int);
    void combo(int, const std::vector<std::wstring>&, int, int, int, int width = 226);
    int selection(int);
    float number(int);
    void setNumber(int, float);
    std::filesystem::path choose(bool, const wchar_t*, const wchar_t*, const wchar_t*);
    void themeControl(HWND, int, const wchar_t*);
    void themeNativeControl(HWND);
    void applyTitleBarTheme(HWND) const;
    bool dialogKey(const MSG&);
    std::vector<RECT> cardRects(HWND) const;
    bool onCard(HWND control) const;
    ui::Color surfaceBehind(HWND control) const;
    void drawFieldFrame(HWND);
    void drawControl(const DRAWITEMSTRUCT&);
    LRESULT colorControl(HDC, HWND);
    void paintChrome(HWND, HDC dc = nullptr);
    void scrollSide(int);
    void refreshChrome();
    void setTheme(bool dark);
    void printUi(HDC, HWND target = nullptr);
    void inputCommand(int);
    void rewindRulesCommand(int);
    std::wstring keyName(int) const;
    void dialogueUi();
    void paintDialogue(HWND, HDC dc = nullptr);
    void showDialogueEditor();
    void dialogueCommand(int);
    void audioCommand(int);
    void showAudioEditor();
    void mapCommand(int);
    void showEventEditor(bool fromCanvas = false);
    void showTilePalette();
    void loadTileAtlas();
    void initWorkspace();
    void refreshWorkspace(bool mapsChanged = false);
    void workspaceCommand(int, int);
    WorkspaceModel workspaceModel();
    void paintWorkspace(HDC, const RECT* area = nullptr);
    bool workspaceMouse(UINT, WPARAM, LPARAM, LRESULT&);
    bool workspaceWheel(POINT screen, int delta);
    void workspaceAction(int action, int value, bool doubleClick = false);
    std::vector<ui::MenuItem> workspaceMenu(int index);
    void openWorkspaceMenu(int index, bool keyboard = false);
    void selectDockItem(int row, bool open);
    void setEditLayer(int);
    void setStatusText(const std::wstring&);
    void switchEditMap(int);
    void showSceneInspector();
    void saveWorkspaceSettings();
    void loadWorkspaceSettings();
    void workspaceTooltip(HWND, const wchar_t*);
    void tileCommand(int);
    void eventCommand(int, int notification = 0);
    void eventLoad(int);
    void eventPageForm(bool resetView = true);
    std::wstring eventCommandLabel(const EventCommand&) const;
    void eventCatalog(int op = -1, bool reveal = false);
    void eventRefreshCommands(int keep = -1);
    std::vector<int> eventSelectedCommands() const;
    int eventSelectedSource() const;
    void eventSetSelection(const std::vector<int>&);
    void eventDraftCommit(std::vector<EventCommand>, int keep);
    void eventDraftHistory(bool undo);
    bool eventShortcut(UINT key, bool control);
    void eventCommandForm();
    void eventReadPage();
    void eventRefreshList();
    EventCommand eventReadCommand();
    void showTransitionEditor();
    void transitionCommand(int);
    void transitionForm();
    TransitionConfig readTransitionForm();
    std::string importResource(const std::filesystem::path&, std::string_view folder, AssetKind);
    void frameRateCommand(int);
    void showFocusEditor();
    void focusForm(bool all = true);
    void focusCommand(int, int notification = 0);
    void collisionCommand(int);
    HurtRegion readHurtRegion();
    void paintFocus(HDC, RECT);
    void showBarrageEditor();
    void showBarrageTargetEditor();
    void barrageTargetForm(bool load = true);
    void barrageCommand(int, int notification = 0);
    void barrageForm();
    void resetBarragePreview();
    void tickBarragePreview();
    void paintBarrage(HDC, RECT);
    void showAnimationsEditor();
    void animationCommand(int, int notification = 0);
    void animationForm(bool rebuild = true);
    void animationImage();
    void paintAnimation(HDC, RECT);
    void showAssetsEditor();
    void assetCommand(int, int notification = 0);
    void assetForm(bool rebuild = true);
    void showObjectsEditor();
    void objectCommand(int, int notification = 0);
    void objectForm(bool rebuild = true);
    void showDatabaseEditor();
    void databaseCommand(int);
    void databaseForm();
    void databaseCommit(Database next, bool history = true);
    void refreshDataLabels();
    void showMapTools();
    void mapToolsCommand(int);
    void snapshotCompareCommand(int);
    void exportSnapshotReport(const std::filesystem::path&);
    void showDebugger();
    void debuggerCommand(int);
    void refreshDebugger();
    void refreshHistoryDiagnostics();
    void showProjectHealth();
    void healthCommand(int);
    void autosave();
    std::string projectText();
    void showMapEditor();
    void showLayerEditor();
    void layerCommand(int);
    void layout();
    void ui();
    void loadProject(HistoryClearReason reason = HistoryClearReason::ProjectLoad);
    void save();
    void brush(int, int, bool);
    void finishBrush();
    void refreshAuthoringHistory();
    void command(int, int notification = 0);
    void render(float dt = fixedDt);
    void run();
    void captureUi(const std::filesystem::path&, HWND target = nullptr);
    void verify(const std::filesystem::path&);
};
// Developer screenshots of every window (`--ui-tour <folder>`, ui_tour.cpp).
void runUiTour(App&, const std::filesystem::path&);
} // namespace echo
