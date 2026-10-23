#pragma once
#include "object_state.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
namespace echo {
inline constexpr float fixedDt = 1.0f / 60.0f, pixelsPerUnit = .25f;
inline constexpr int virtualWidth = 320, virtualHeight = 180, maxBullets = 2048, maxZones = 64;
inline constexpr int mapWidth = 96, mapHeight = 24;
inline constexpr float tileSize = 32, playerWidth = 28, playerHeight = 48;
template <class T, std::size_t N> struct FixedList {
    std::array<T, N> values{};
    std::uint32_t count{};
    bool push(const T& v) {
        if (count == N)
            return false;
        values[count++] = v;
        return true;
    }
    void erase(std::size_t i) {
        values[i] = values[--count];
        values[count] = T{};
    }
    T* begin() {
        return values.data();
    }
    T* end() {
        return begin() + count;
    }
    const T* begin() const {
        return values.data();
    }
    const T* end() const {
        return begin() + count;
    }
    T& operator[](std::size_t i) {
        return values[i];
    }
    const T& operator[](std::size_t i) const {
        return values[i];
    }
    void clear() {
        for (auto& v : values)
            v = T{};
        count = 0;
    }
};
template <std::size_t N> struct Text {
    std::array<char, N> data{};
    void set(std::string_view s) {
        data.fill(0);
        s.copy(data.data(), s.size() < N - 1 ? s.size() : N - 1);
    }
    bool empty() const {
        return data[0] == 0;
    }
    const char* c_str() const {
        return data.data();
    }
};
struct Vec2 {
    float x{}, y{};
};
struct Rect {
    float x{}, y{}, w{}, h{};
};
// Gameplay transition progress/commit state MUST be snapshotted. GPU masks are visual only.
enum class TransitionKind : std::uint8_t {
    None,
    Fade,
    Iris,
    Diamond,
    Curtain,
    Diagonal,
    Pixels,
    Custom
};
enum class TransitionEase : std::uint8_t { Linear, Smooth, In, Out };
enum class TransitionPhase : std::uint8_t { Idle, Closing, Covered, Opening };
enum class TransitionReason : std::uint8_t { Map, Restart };
struct TransitionConfig {
    TransitionKind kind = TransitionKind::None;
    TransitionEase ease = TransitionEase::Smooth;
    std::uint8_t direction{};
    bool playerCenter = true;
    float closeSeconds = .18f, holdSeconds = .04f, openSeconds = .18f;
    std::array<float, 3> color{0, 0, 0};
    float detail = 12;
};
struct TransitionState {
    TransitionConfig config{};
    TransitionPhase phase = TransitionPhase::Idle;
    TransitionReason reason{};
    float elapsed{};
    int targetMap = -1;
    Vec2 destination{};
    bool useSpawn = true;
};
// Pair filters are snapshot truth. They restrict existing interactions; they do not
// turn a sensor into a solid or bypass faction / individual collision masks.
enum class CollisionClass : std::uint8_t {
    Terrain,
    Player,
    Enemy,
    NPC,
    Prop,
    Projectile,
    Attack,
    Trigger
};
struct CollisionRules {
    std::array<std::uint8_t, 8> masks{255, 255, 255, 255, 255, 255, 255, 255};
};
struct JumpConfig {
    float move_speed = 180, move_speed_down = 80, ground_acceleration = 900, ground_friction = 1200;
    float air_acceleration = 600, air_friction = 240, jump_velocity = -380, min_jump_velocity = -120;
    float coyote_time = .08f, jump_buffer_time = .12f, fall_gravity_multiplier = 1.25f;
    float low_jump_gravity_multiplier = 1.8f, gravity = 980;
    float apex_gravity_multiplier = .65f, apex_velocity = 40, max_fall_speed = 700;
};
// Gameplay hit geometry and focus input edges are snapshot truth; terrain bodies stay separate.
enum class HurtSource : std::uint8_t { Animation, Whole, Custom };
enum class HurtShape : std::uint8_t { Circle, Rectangle, Ellipse, Capsule, Polygon };
enum class HurtMethod : std::uint8_t { Swept, Overlap };
enum class HurtShow : std::uint8_t { Never, Always, Focus, Key, Danger };
enum class FocusMode : std::uint8_t { Hold, Toggle, Automatic };
enum class FocusWhen : std::uint8_t { Always, Bullets, Boss, Switch };
inline constexpr std::uint8_t hurtBullets = 1, hurtContact = 2, hurtMelee = 4;
struct HurtRegion {
    HurtShape shape = HurtShape::Circle;
    bool enabled = true, focusOnly = false;
    std::uint8_t mask = 7;
    float anchorX = .5f, anchorY = 2.f / 3, offsetX{}, offsetY{};
    float radius = 3, width = 6, height = 6, angle{};
    FixedList<Vec2, 16> vertices;
};
struct HurtConfig {
    HurtSource source = HurtSource::Animation;
    HurtMethod method = HurtMethod::Swept;
    HurtShow show = HurtShow::Focus;
    bool touching = true, mirror = true;
    // Circle projectiles use radius; box projectiles use a square with half-size hitR.
    bool projectileBox = false;
    std::uint32_t count = 1;
    std::array<HurtRegion, 8> regions{};
    int showKey = 0x11, style = 0; // Ctrl; outline / cross / filled / outline + cross
    float displayScale = 1, opacity = .9f, outline = 1;
    std::array<float, 3> color{1, .95f, .65f};
};
struct FocusConfig {
    bool enabled = false, ground = true, air = true, instant = true, showIndicator = true;
    FocusMode mode = FocusMode::Hold;
    FocusWhen when = FocusWhen::Always;
    float speedRatio = .444444f;
    int key = 0x10, gateSwitch = -1, map = -1; // Shift; optional additional map/switch gates
};
struct RewindConfig {
    float history_seconds = 6;
    int record_hz = 60;
    bool enabled = true;
    float fast_rate = 5, trace_rate = 1;
    bool fast_stop_near_bullets = true;
    float fast_bullet_margin = 2; // logical pixels; proximity only, never changes bullet hitR
};
enum class TimeState : std::uint8_t { Idle, Free, Fast, Trace, Hit, Forward, Suppressed, Escape };
enum class KinoAnimState : std::uint8_t { Idle, Run, Jump, JumpFall };
enum class BossAnimState : std::uint8_t { Idle, Walk, Cast };
struct MobilityConfig {
    bool dashEnabled{}, wallJumpEnabled{}, crushKills = true;
    float climbSpeed = 160, swimSpeed = 100, waterGravity = .3f, swimJump = 240, waterFallSpeed = 150;
    float dashSpeed = 600, dashDuration = .15f, dashCooldown = .5f;
    float wallSlideSpeed = 100, wallJumpX = 230, wallJumpY = 380, wallLock = .12f;
    int dashKey = 88, climbKey = 87; // X / W; Down uses the project's down binding.
};
struct KinoState {
    AnimationPlayback animation{};
    float width = playerWidth, height = playerHeight;
    float x = 160, y = 592, vx{}, vy{};
    bool grounded = true;
    int facing = 1;
    KinoAnimState animState{};
    float animTime{};
    int standingPlatformId = -1;
    float dropThroughTimer{}, coyoteTimer = .08f, jumpBufferTimer{};
    // HARD CONTRACT: BOTH affect gravity and input edges and must enter snapshots.
    bool jumpHeld{}, jumpWasCut{};
    bool focusActive{}, focusLatched{}, previousFocus{}, showHurtHeld{};
    bool climbing{}, inWater{}, previousDash{}, airDashAvailable = true, crushed{};
    int ladderId = -1, waterId = -1, wallDirection{}, dashDirection = 1;
    float dashRemaining{}, dashCooldown{}, wallLockRemaining{};
    float hitFlash{};
    int hp = 1;
    float invulnTimer{};
};
struct Volley {
    float at{}, angle{}, speed = 140;
    std::uint16_t count = 12;
    bool aimed{}, fired{}, locked{};
    std::uint8_t pattern{};
    Vec2 origin{};
};
struct Mark {
    Vec2 pos{};
    float at{}, width{};
};
struct Zone {
    float x{}, y{}, w{}, h{}, start{}, end{}, lead = .85f;
    Text<32> label{};
    bool brake{}, fired{}, entered{};
};
struct AttackState {
    float t{}, warn = .85f, end = 7;
    int variant{}, phase = 1;
    FixedList<Volley, 32> volleys;
    FixedList<Mark, 16> marks;
    FixedList<Zone, 16> zones;
};
struct AsterState {
    AnimationPlayback animation{};
    float width = 48, height = 64;
    float x = 880, y = 576, vx = 48, vy{};
    int hp = 500, maxHp = 500, phaseId = 1;
    BossAnimState animState{};
    float animTime{}, invulnTimer{};
    bool knockbackActive{};
    float knockbackTime{}, knockbackDuration{}, knockbackStartX{}, knockbackTargetX{};
    AttackState attack{};
    float specialCastTimer{}, specialCastElapsed{};
    bool specialCastFired{};
    int castBloomType{};
    float specialCooldown = 1.2f;
    bool resting = true;
    float attackTimer = 1.2f;
    Text<128> dialogue{};
    int lastTauntCategory{}, lastTauntIndex = -1, attackCycle{}, walkDir = 1;
    bool enabled = true;
};
struct Formation {
    float cx{}, cy{}, lx{}, ly{}, scale = 1, growth{}, driftX{}, driftY{}, rotation{}, age{};
    std::uint8_t type{};
    bool active{};
};
struct Motion {
    float phase{}, amplitude{}, frequency = 5;
    std::uint8_t type{};
};
enum class BulletOverflowPolicy : std::uint8_t { RejectNewest, RecycleOldest, RecycleGroup };
inline constexpr int maxBarrageEmitters = 64;
struct BarrageRuntime {
    std::uint64_t id{};
    std::uint32_t seed = 1, initialSeed = 1, wave{}, shot{}, cycles{};
    float initialDelay{};
    float elapsed{}, angle{}, warningAngle{}, warningSpread{};
    std::uint16_t warningCount{};
    std::uint8_t warningShape{};
    Vec2 origin{}, target{};
    ObjectHandle targetHandle{};
    std::uint64_t targetId{};
    bool targetValid{}, waitingForTarget{};
    bool enabled{}, finished{}, locked{}, warning{};
};
struct BarrageWorld {
    std::array<BarrageRuntime, maxBarrageEmitters> emitters{};
    int map{};
    std::uint32_t attemptBudget = 4096, budgetRejected{}, missingParents{}, invalidDefinitions{},
                  volleys{}, missingTargets{}, targetQueries{}, targetCandidates{};
};
struct Bullet {
    std::uint64_t source{};
    std::uint16_t group{};
    std::uint8_t terrain{};
    int damage = 1;
    float acceleration{}, maxSpeed = 1000000000, rotationRate{}, releaseAge{};
    std::uint32_t id{};
    // HARD CONTRACT: swept collision reads px/py. They MUST survive restore.
    float x{}, y{}, px{}, py{}, vx{}, vy{}, angle{}, visualR = 6, hitR = 3;
    std::uint8_t style{}, palette{};
    float turnRate{}, turnTime{}, age{}, expireAge = 10;
    Formation formation{};
    Motion motion{};
    bool brake{}, roomSeal{}, approachGuard{};
    float preludeLife{};
};
enum class Tile : std::uint8_t { Empty, Solid, OneWay, Spike, Moving };
struct MovingPlatform {
    Rect base{};
    Vec2 pos{}, previous{};
    float amplitude = 96, speed = 1, phase{};
    int id{};
};
enum class PlatformKind : std::uint8_t { Ladder, Water, Conveyor, Spring, Falling, Lift };
struct PlatformFeature {
    std::uint32_t id{};
    PlatformKind kind{};
    Rect base{}, bounds{};
    Vec2 previous{};
    float speed = 100, delay = .35f, resetSeconds = 2.5f;
    bool enabled = true;
    int stage{}; // Falling: 0 supported, 1 dropping, 2 hidden. Lift: 0 moving, 1 dwell, 2 done.
    float elapsed{}, velocity{}, cooldown{};
    // HARD CONTRACT: route definition AND traversal state are snapshotted; no hidden mover timer.
    Vec2 travel{0, -192};
    float progress{};
    int direction = 1;
    bool repeat = true;
};
struct TileLayer {
    Text<256> image;
    Rect imageBounds{0, 0, 1280, 720};
    std::array<std::uint8_t, mapWidth * mapHeight> cells{};
    float parallax = 1;
    bool visible = true;
};
enum class ColliderKind : std::uint8_t { Circle, Triangle, Square, Rectangle, Polygon };
struct StaticCollider {
    ColliderKind kind = ColliderKind::Rectangle;
    Rect bounds{};
    FixedList<Vec2, 16> points;
};
struct SceneLight {
    Vec2 pos{};
    float radius = 160, intensity = 1.2f;
    Vec2 warmColor{1, .65f};
    float blue = .3f, flicker = .06f;
};
struct DialogueDefinition {
    Rect region{};
    Text<64> speaker;
    Text<256> portrait;
    Text<1024> text;
    bool once = true, triggered{}, entered{};
};
// Runtime choice labels, availability, selection and key edges are snapshot truth.
struct DialogueOption {
    Text<128> label;
    int result{};
    bool enabled{};
};
struct DialogueState {
    int active = -1, page{};
    float revealed{};
    bool previousConfirm{};
    bool choosing{};
    FixedList<DialogueOption, 6> options;
    int selected{}, resultVariable = -1, previousNavigation{};
};
struct MapPortal {
    bool transitionOverride{};
    TransitionConfig transition{};
    Rect region{};
    int targetMap{};
    Vec2 destination{};
    bool useSpawn = true, entered{};
};
struct EventPlacement {
    int id = 1;
    Rect region{};
    bool solid{}, visible = true, erased{};
    int graphic{};
};
// Event-owned restrictions are value state. A runner may release only its own request.
inline constexpr std::uint16_t allPlayerControls = 511;
struct ControlInputState {
    std::uint16_t physicalHeld{}, heldBlocked{};
    bool dashBlocked{}, climbBlocked{}; // re-arm edge actions only after physical key release
};
// Loop counters and block endpoints affect execution and MUST be restored with the PC.
inline constexpr int maxEventLoopDepth = 8;
struct EventLoopFrame {
    int begin{}, end{}, remaining{}; // remaining == 0: repeat until BreakLoop
};
struct EventRunner {
    int page = -1, pc{}, waitCommon = -1;
    std::array<EventLoopFrame, maxEventLoopDepth> loops{};
    int loopDepth{};
    float wait{}, moveRemaining{};
    Vec2 moveVelocity{};
    // Contact edges, queued activation and route response MUST survive restore.
    Rect previousContact{}, previousRegion{};
    bool contactInitialized{}, triggerPending{}, moveCollide{}, moveBlocked{};
    bool running{}, requested{}, called{}, entered{}, waitDialogue{};
    std::uint64_t callGeneration{}, waitCommonGeneration{};
    int caller = -1;
    int pageConditionFault = -1; // Snapshot latch: never silently fall back to a lower page on error.
    bool controlActive{}, scriptFault{}; // Fault latch must restore; no automatic retry loop.
    std::uint16_t controlAllowed = allPlayerControls;
};
struct EventState {
    std::array<std::uint8_t, 128> switches{};
    std::array<int, 64> variables{};
    std::array<std::array<std::uint8_t, 32>, 9> self{};
    std::array<EventRunner, 32> local{};
    std::array<EventRunner, 16> common{};
    int foreground = -1, dialogueOwner = -1, transferMap = -1;
    bool previousInteract{}, transferSpawn = true;
    Vec2 transferPoint{};
    DialogueDefinition dialogue;
    std::uint32_t errors{}, budgetYields{}, scheduleCursor{}, pageConditionDeferrals{};
};
struct LevelState {
    std::uint64_t nextObjectId = 1;
    std::uint32_t nextFeatureId = 1;
    FixedList<PlatformFeature, 64> features;
    std::array<Tile, mapWidth * mapHeight> tiles{};
    Vec2 spawn{160, 592}, bossSpawn{880, 576};
    int background{};
    FixedList<MovingPlatform, 16> platforms;
    std::array<TileLayer, 3> layers{};
    FixedList<StaticCollider, 64> colliders;
    FixedList<SceneLight, 32> lights;
    FixedList<DialogueDefinition, 8> dialogues;
    FixedList<MapPortal, 16> portals;
    FixedList<EventPlacement, 32> events;
    FixedList<ObjectPlacement, maxObjectPlacements> objects;
    bool cameraClamp = true;
    Tile at(int x, int y) const {
        return x < 0 || y < 0 || x >= mapWidth || y >= mapHeight ? Tile::Empty : tiles[y * mapWidth + x];
    }
};
struct HeldRewind {
    TimeState mode{}, previousState{};
    int startCursor{};
    float heldSeconds{};
    double startTime{}, targetTime{};
};
struct FastState {
    Vec2 A{}, B{};
    int target{}, startCursor{};
    float maxRewindSeconds{};
    bool hold{};
    float t{}, dur{};
};
enum class OpCode : std::uint8_t { Fire, Wait, Aim, Incremental, SetDirection };
struct Instruction {
    OpCode op{};
    float value{}, speed = 140;
    int count = 12, pattern{};
};
struct ScriptState {
    FixedList<Instruction, 256> program;
    int pc{};
    float wait{}, direction{};
    bool aimPlayer{}, loop = true;
    std::array<double, 64> persistent{};
    Text<4096> serialized{};
    std::uint32_t serializedLength{};
    int instructionsDropped{};
};
// HARD CONTRACT: ANY field read by next-frame gameplay MUST live here. No gameplay
// in render objects, closures, particle systems, static singletons or Lua upvalues.
// Fixed containers make snapshots complete value copies without tick allocations.
struct SoundVoice {
    std::uint32_t id{};
    int clip{};
    float age{}, duration{}, gain = 1, pitch = 1;
};
struct AudioState {
    double playSeconds{};
    float musicLength{}, loopStart{}, loopEnd{}, musicVolume = .7f, seVolume = .8f;
    bool playing = true, paused{}, loop = true;
    float fadeStart{}, fadeTarget{}, fadeDuration{}, fadeElapsed{};
    std::array<float, 32> lengths{};
    FixedList<SoundVoice, 16> voices;
    std::uint32_t nextVoiceId = 1, voicesDropped{};
    std::uint64_t transportGeneration{}, seenSoundEvent{};
};
enum class QuestStatus : std::uint8_t { Inactive, Active, Completed, Failed };
enum class ProgressResult : std::uint8_t { Success, Missing, Full, Insufficient, InactiveQuest };
struct ItemState {
    std::uint32_t id{};
    int count{}, limit = 99;
};
struct QuestState {
    std::uint32_t id{};
    QuestStatus status{};
    int progress{}, target = 1;
    bool autoComplete = true;
};
struct ProgressionState {
    // HARD CONTRACT: quantities, limits, quest status/progress/target and failure diagnostics
    // are gameplay state. Full restore MUST replace all of them, including unused slots.
    FixedList<ItemState, 64> items;
    FixedList<QuestState, 64> quests;
    std::uint32_t rejected{};
    ProgressResult lastResult{};
};
struct WorldState {
    ObjectWorld objects{};
    BarrageWorld barrage{};
    BulletOverflowPolicy bulletOverflow{};
    std::uint32_t bulletsRecycled{};
    AnimationWorld animations{};
    TransitionConfig mapTransition{}, deathTransition{};
    TransitionState transition{};
    CollisionRules collisions{};
    EventState events{};
    ProgressionState progression{};
    ControlInputState controlInput{};
    AudioState audio{};
    JumpConfig jump{};
    MobilityConfig mobility{};
    HurtConfig hurt{};
    FocusConfig focus{};
    RewindConfig rewind{};
    KinoState kino{};
    AsterState aster{};
    FixedList<Bullet, maxBullets> bullets;
    FixedList<Zone, maxZones> zones;
    TimeState state = TimeState::Free;
    HeldRewind heldRewind{};
    FastState fast{};
    int timelineCursor{}, traceTarget{}, traceStartCursor{};
    TimeState resumeAfterRewind = TimeState::Free;
    std::uint64_t timelineGenerationId{};
    bool gameplayClockPaused{};
    float rewindExitGrace{}, screenShake{};
    int activeMap{};
    float transitionCooldown{};
    LevelState level{};
    DialogueState dialogue{};
    int jumpPreset = -1, bulletPreset = -1;
    ScriptState script{};
    double clock{}, recordAccumulator{};
    std::uint64_t tick{};
    std::uint32_t randomSeed = 0x4b494e4f, nextBulletId = 1, bulletsDropped{};
    bool previousR{}, previousT{}, previousDown{}, previousRestart{}, rewindInputLatched{};
    bool recordingEnabled = true;
    std::uint64_t soundEvent{};
    int soundKind{};
};
static_assert(std::is_trivially_copyable_v<WorldState>);
WorldState makeWorld();
inline WorldState snapshot(const WorldState& w) {
    return w;
}
// Full assignment deliberately replaces EVERY field, including fixed array tails.
inline void restore(WorldState& w, const WorldState& s) {
    w = s;
}
} // namespace echo
