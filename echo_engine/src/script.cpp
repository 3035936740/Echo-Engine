#include "echo/focus.hpp"
#include "echo/collision.hpp"
#include "echo/control.hpp"
#include "echo/script.hpp"
#include "echo/database.hpp"
#include "echo/platform_features.hpp"
#include "echo/audio_state.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <lua.hpp>
#include <numbers>
#include <stdexcept>
namespace echo {
struct Field {
    const char* name;
    std::size_t offset;
    int kind;
};
#define F(T, n)                                                                                         \
    Field {                                                                                             \
        #n, offsetof(T, n), 0                                                                           \
    }
#define I(T, n)                                                                                         \
    Field {                                                                                             \
        #n, offsetof(T, n), 1                                                                           \
    }
#define B(T, n)                                                                                         \
    Field {                                                                                             \
        #n, offsetof(T, n), 2                                                                           \
    }
static const Field jumpFields[] = {F(JumpConfig, move_speed),
                                   F(JumpConfig, move_speed_down),
                                   F(JumpConfig, ground_acceleration),
                                   F(JumpConfig, ground_friction),
                                   F(JumpConfig, air_acceleration),
                                   F(JumpConfig, air_friction),
                                   F(JumpConfig, jump_velocity),
                                   F(JumpConfig, min_jump_velocity),
                                   F(JumpConfig, coyote_time),
                                   F(JumpConfig, jump_buffer_time),
                                   F(JumpConfig, fall_gravity_multiplier),
                                   F(JumpConfig, low_jump_gravity_multiplier),
                                   F(JumpConfig, gravity),
                                   F(JumpConfig, apex_gravity_multiplier),
                                   F(JumpConfig, apex_velocity),
                                   F(JumpConfig, max_fall_speed)};
static const Field rewindFields[] = {
    F(RewindConfig, history_seconds),   I(RewindConfig, record_hz),
    B(RewindConfig, enabled),           F(RewindConfig, fast_rate),
    F(RewindConfig, trace_rate),        B(RewindConfig, fast_stop_near_bullets),
    F(RewindConfig, fast_bullet_margin)};
static const Field mobilityFields[] = {
    B(MobilityConfig, dashEnabled),  B(MobilityConfig, wallJumpEnabled),
    B(MobilityConfig, crushKills),   F(MobilityConfig, climbSpeed),
    F(MobilityConfig, swimSpeed),    F(MobilityConfig, waterGravity),
    F(MobilityConfig, swimJump),     F(MobilityConfig, waterFallSpeed),
    F(MobilityConfig, dashSpeed),    F(MobilityConfig, dashDuration),
    F(MobilityConfig, dashCooldown), F(MobilityConfig, wallSlideSpeed),
    F(MobilityConfig, wallJumpX),    F(MobilityConfig, wallJumpY),
    F(MobilityConfig, wallLock),     I(MobilityConfig, dashKey),
    I(MobilityConfig, climbKey)};
static const Field musicFields[] = {F(AudioState, musicVolume), F(AudioState, seVolume),
                                    F(AudioState, loopStart),   F(AudioState, loopEnd),
                                    B(AudioState, playing),     B(AudioState, paused),
                                    B(AudioState, loop)};
static const Field bossFields[] = {F(AsterState, x),
                                   F(AsterState, y),
                                   F(AsterState, vx),
                                   F(AsterState, vy),
                                   I(AsterState, hp),
                                   I(AsterState, maxHp),
                                   I(AsterState, phaseId),
                                   F(AsterState, invulnTimer),
                                   B(AsterState, enabled),
                                   F(AsterState, attackTimer),
                                   F(AsterState, specialCooldown)};
static const Field playerFields[] = {F(KinoState, x),
                                     F(KinoState, y),
                                     F(KinoState, vx),
                                     F(KinoState, vy),
                                     I(KinoState, hp),
                                     F(KinoState, invulnTimer),
                                     B(KinoState, climbing),
                                     B(KinoState, inWater),
                                     B(KinoState, airDashAvailable),
                                     F(KinoState, dashRemaining),
                                     I(KinoState, wallDirection)};
#undef F
#undef I
#undef B
struct ScriptRuntime::Impl {
    lua_State* L{};
    WorldState* world{};
    std::size_t allocated{}, limit = 8 * 1024 * 1024;
    int instructionBudget{};
    bool loaded{};
    ObjectLibrary objects;
    AnimationLibrary animations;
    int mapCount = 1;
    static constexpr int scriptSlots = 9 * 33 * maxEventPages * (maxEventCommands * 7 + 1);
    struct ScriptEntry {
        int ref = LUA_NOREF;
        bool condition{};
    };
    using ScriptRefs = std::array<ScriptEntry, scriptSlots>;
    std::unique_ptr<ScriptRefs> eventScripts = std::make_unique<ScriptRefs>();
    std::unique_ptr<WorldState> eventBackup = std::make_unique<WorldState>();
    int eventEnvironment = LUA_NOREF, conditionEnvironment = LUA_NOREF, eventCompiler = LUA_NOREF;
    EventScriptContext currentEvent;
    bool executingEvent{}, executingCondition{};
    int eventInstructionsRemaining = 100000, eventCallsRemaining = 32;
    std::string lastEventError;
    static int eventSlot(const EventScriptContext& c) {
        int bank = c.common ? 8 : c.map;
        if (bank < 0 || bank > 8 || (!c.common && bank == 8) || c.event < 1 ||
            c.event > (c.common ? maxCommonEvents : maxMapEvents) || c.page < 0 ||
            c.page >= maxEventPages || c.command < -1 || c.command >= maxEventCommands ||
            c.option < -1 || c.option >= 6 || (c.command < 0 && c.option >= 0))
            return -1;
        return ((bank * 33 + c.event) * maxEventPages + c.page) * (maxEventCommands * 7 + 1) +
               (c.command < 0 ? 0 : 1 + c.command * 7 + c.option + 1);
    }
    static std::string eventLocation(const EventScriptContext& c) {
        return (c.common ? "common" : "map " + std::to_string(c.map + 1)) + std::string(" event ") +
               std::to_string(c.event) + " page " + std::to_string(c.page + 1) +
               (c.command < 0 ? " page condition" : " command " + std::to_string(c.command + 1)) +
               (c.option < 0 ? "" : " choice " + std::to_string(c.option + 1));
    }
    void releaseRefs(ScriptRefs& refs) {
        for (auto& entry : refs) {
            if (entry.ref != LUA_NOREF)
                luaL_unref(L, LUA_REGISTRYINDEX, entry.ref);
            entry = {};
        }
    }
    static Impl& eventOwner(lua_State* L) {
        auto& r = owner(L);
        if (!r.executingEvent)
            luaL_error(L, "This API requires an event Lua command");
        return r;
    }
    static int selfSwitch(lua_State* L) {
        static const char* const names[] = {"A", "B", "C", "D", nullptr};
        return luaL_checkoption(L, 1, nullptr, names);
    }
    static int getSelfSwitch(lua_State* L) {
        auto& r = eventOwner(L);
        int key = selfSwitch(L);
        int bank = r.currentEvent.common ? 8 : r.currentEvent.map;
        lua_pushboolean(L, (r.world->events.self[bank][r.currentEvent.event - 1] & (1 << key)) != 0);
        return 1;
    }
    static int setSelfSwitch(lua_State* L) {
        auto& r = eventOwner(L);
        int key = selfSwitch(L);
        luaL_checktype(L, 2, LUA_TBOOLEAN);
        int bank = r.currentEvent.common ? 8 : r.currentEvent.map;
        auto& bits = r.world->events.self[bank][r.currentEvent.event - 1];
        if (lua_toboolean(L, 2))
            bits |= 1 << key;
        else
            bits &= ~(1 << key);
        return 0;
    }
    static int eventControl(lua_State* L) {
        auto& r = eventOwner(L);
        auto profile = luaL_checkinteger(L, 1), mask = luaL_optinteger(L, 2, allPlayerControls);
        if (profile < 0 || profile > 5 || mask < 0 || mask > allPlayerControls)
            return luaL_error(L, "Event control: profile 0..5, permission mask 0..511");
        auto& runner = r.currentEvent.common ? r.world->events.common[r.currentEvent.event - 1]
                                             : r.world->events.local[r.currentEvent.event - 1];
        runner.controlActive = profile != 0;
        runner.controlAllowed = controlProfile(int(profile), std::uint16_t(mask));
        return 0;
    }
    static int transfer(lua_State* L) {
        auto& r = owner(L);
        auto map = luaL_checkinteger(L, 1);
        float x = float(luaL_optnumber(L, 2, 0)), y = float(luaL_optnumber(L, 3, 0));
        bool spawn = lua_isnone(L, 4) || lua_toboolean(L, 4);
        if (map < 1 || map > r.mapCount || !std::isfinite(x) || !std::isfinite(y) ||
            std::abs(x) > 8192 || std::abs(y) > 8192)
            return luaL_error(L, "Transfer: existing map 1..8 and finite coordinates within 8192");
        if (r.world->events.transferMap >= 0)
            return luaL_error(L, "A transfer is already requested");
        r.world->events.transferMap = int(map) - 1;
        r.world->events.transferPoint = {x, y};
        r.world->events.transferSpawn = spawn;
        return 0;
    }
    static int readOnly(lua_State* L) {
        return luaL_error(L, "Event Lua definitions are read-only; use locals or engine.state");
    }
    // Wrap a hidden backing table, including its C API functions, without exposing metatables.
    void readOnlyTable(int backing) {
        backing = lua_absindex(L, backing);
        lua_newtable(L);
        lua_newtable(L);
        lua_pushvalue(L, backing);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, readOnly);
        lua_setfield(L, -2, "__newindex");
        lua_pushboolean(L, false);
        lua_setfield(L, -2, "__metatable");
        lua_setmetatable(L, -2);
    }
    void setupEventEnvironment(bool condition = false) {
        lua_newtable(L);
        int env = lua_gettop(L);
        for (auto name : {"engine", "math", "string"}) {
            lua_getglobal(L, name);
            // Conditions expose only getters. Never run mutating native APIs speculatively.
            if (condition && !std::strcmp(name, "engine")) {
                int source = lua_gettop(L);
                lua_newtable(L);
                for (auto field : {"state", "get_switch", "get_variable", "get_platform",
                                   "get_self_switch", "event_context", "object_valid", "get_object",
                                   "get_item_count", "get_item", "get_quest", "collision_pair"}) {
                    lua_getfield(L, source, field);
                    lua_setfield(L, -2, field);
                }
                readOnlyTable(-1);
                lua_setfield(L, env, name);
                lua_pop(L, 2);
                continue;
            }
            // Copy native bindings before project scripts can replace their table entries.
            lua_newtable(L);
            lua_pushnil(L);
            while (lua_next(L, -3)) {
                lua_pushvalue(L, -2);
                lua_pushvalue(L, -2);
                lua_rawset(L, -5);
                lua_pop(L, 1);
            }
            readOnlyTable(-1);
            lua_setfield(L, env, name);
            lua_pop(L, 2);
        }
        for (auto name :
             {"mobility",   "jump",     "rewind",      "boss",          "player",        "music",
              "persistent", "assert",   "error",       "ipairs",        "pairs",         "next",
              "select",     "tonumber", "tostring",    "type",          "barrage",       "fire",
              "wait",       "aim",      "incremental", "set_direction", "repeat_pattern"}) {
            if (condition && (!std::strcmp(name, "barrage") || !std::strcmp(name, "fire") ||
                              !std::strcmp(name, "wait") || !std::strcmp(name, "aim") ||
                              !std::strcmp(name, "incremental") || !std::strcmp(name, "set_direction") ||
                              !std::strcmp(name, "repeat_pattern")))
                continue;
            lua_getglobal(L, name);
            lua_setfield(L, env, name);
        }
        readOnlyTable(env);
        (condition ? conditionEnvironment : eventEnvironment) = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_pop(L, 1);
        if (!condition) {
            lua_pushlightuserdata(L, this);
            lua_pushcclosure(L, compileEventProtected, 1);
            eventCompiler = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    }
    static int compileEventProtected(lua_State* L) {
        auto& r = owner(L);
        auto* c = static_cast<const EventCommand*>(lua_touserdata(L, 1));
        auto* label = static_cast<const char*>(lua_touserdata(L, 2));
        if (luaL_loadbufferx(L, c->text.c_str(), std::strlen(c->text.c_str()), label, "t") != LUA_OK)
            return lua_error(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX,
                    c->op == EventOp::IfScript ? r.conditionEnvironment : r.eventEnvironment);
        lua_setupvalue(L, -2, 1);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_pushinteger(L, ref);
        return 1;
    }
    int compileEvent(const EventCommand& c, const EventScriptContext& location) {
        int top = lua_gettop(L);
        auto label = "@" + eventLocation(location);
        lua_rawgeti(L, LUA_REGISTRYINDEX, eventCompiler);
        lua_pushlightuserdata(L, const_cast<EventCommand*>(&c));
        lua_pushlightuserdata(L, const_cast<char*>(label.c_str()));
        try {
            protectedCall(2, 1);
        } catch (const std::exception& e) {
            lua_settop(L, top);
            throw std::runtime_error(eventLocation(location) + ": " + e.what());
        }
        int ref = int(lua_tointeger(L, -1));
        lua_pop(L, 1);
        return ref;
    }
    void compileMap(const EventMap& map, bool common, int index, ScriptRefs& refs) {
        validateEvents(map, common);
        for (const auto& e : map.events)
            for (int page = 0; page < int(e.pages.size()); ++page) {
                if (e.pages[page].scriptConditionEnabled) {
                    EventCommand condition;
                    condition.op = EventOp::IfScript;
                    condition.text = e.pages[page].scriptCondition;
                    EventScriptContext location{index, e.id, page, -1, common};
                    int slot = eventSlot(location);
                    if (slot < 0)
                        throw std::runtime_error("Event Lua map out of range");
                    refs[slot] = {compileEvent(condition, location), true};
                }
                for (int pc = 0; pc < int(e.pages[page].commands.size()); ++pc) {
                    const auto& c = e.pages[page].commands[pc];
                    if (c.op == EventOp::Choices) {
                        for (int option = 0; option < int(c.choices.size()); ++option) {
                            if (c.choices[option].condition != ChoiceCondition::Lua)
                                continue;
                            EventCommand condition;
                            condition.op = EventOp::IfScript;
                            condition.text = c.choices[option].script;
                            EventScriptContext location{index, e.id, page, pc, common, option};
                            int slot = eventSlot(location);
                            if (slot < 0)
                                throw std::runtime_error("Choice Lua map out of range");
                            refs[slot] = {compileEvent(condition, location), true};
                        }
                    }
                    if (c.op != EventOp::Script && c.op != EventOp::IfScript)
                        continue;
                    EventScriptContext location{index, e.id, page, pc, common};
                    int slot = eventSlot(location);
                    if (slot < 0)
                        throw std::runtime_error("Event Lua map out of range");
                    refs[slot] = {compileEvent(c, location), c.op == EventOp::IfScript};
                }
            }
    }
    static int eventContext(lua_State* L) {
        auto& r = owner(L);
        if (!r.executingEvent)
            return luaL_error(L, "event_context requires an event Lua command");
        lua_pushinteger(L, r.currentEvent.map + 1);
        lua_pushboolean(L, r.currentEvent.common);
        lua_pushinteger(L, r.currentEvent.event);
        lua_pushinteger(L, r.currentEvent.page + 1);
        lua_pushinteger(L, r.currentEvent.command + 1);
        return 5;
    }
    struct Proxy {
        Impl* owner;
        int kind;
    };
    static void* allocator(void* user, void* ptr, size_t oldSize, size_t size) {
        auto& r = *static_cast<Impl*>(user);
        if (!ptr)
            oldSize = 0;
        if (size == 0) {
            r.allocated -= oldSize;
            std::free(ptr);
            return nullptr;
        }
        if (size > r.limit || r.allocated - oldSize > r.limit - size)
            return nullptr;
        void* result = std::realloc(ptr, size);
        if (result)
            r.allocated = r.allocated - oldSize + size;
        return result;
    }
    static void watchdog(lua_State* L, lua_Debug*) {
        auto** owner = static_cast<Impl**>(lua_getextraspace(L));
        if (((*owner)->instructionBudget -= 1000) <= 0)
            luaL_error(L, "Script instruction budget exhausted");
    }
    Impl() {
        eventScripts->fill({});
        L = lua_newstate(allocator, this);
        if (!L)
            throw std::runtime_error("Lua allocation failed");
        *static_cast<Impl**>(lua_getextraspace(L)) = this;
        luaL_requiref(L, "_G", luaopen_base, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "math", luaopen_math, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "string", luaopen_string, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "table", luaopen_table, 1);
        lua_pop(L, 1);
        const char* disabled[] = {"dofile", "loadfile", "load", "collectgarbage", "rawset"};
        for (auto n : disabled) {
            lua_pushnil(L);
            lua_setglobal(L, n);
        }
        lua_getglobal(L, "math");
        lua_pushnil(L);
        lua_setfield(L, -2, "random");
        lua_pushnil(L);
        lua_setfield(L, -2, "randomseed");
        lua_pop(L, 1);
    }
    ~Impl() {
        if (L)
            lua_close(L);
    }
    void protectedCall(int nargs, int nresults, int budget = 100000) {
        instructionBudget = budget;
        lua_sethook(L, watchdog, LUA_MASKCOUNT, 1000);
        int status = lua_pcall(L, nargs, nresults, 0);
        lua_sethook(L, nullptr, 0, 0);
        if (status != LUA_OK) {
            const char* detail = lua_tostring(L, -1);
            std::string error = detail ? detail : "Lua error (non-string value)";
            lua_pop(L, 1);
            throw std::runtime_error(error);
        }
    }
    template <class A> static const Field* find(const A& fields, const char* key) {
        for (auto& f : fields)
            if (std::strcmp(f.name, key) == 0)
                return &f;
        return nullptr;
    }
    static const Field* field(int kind, const char* key) {
        switch (kind) {
        case 0:
            return find(jumpFields, key);
        case 1:
            return find(rewindFields, key);
        case 2:
            return find(bossFields, key);
        case 3:
            return find(playerFields, key);
        case 5:
            return find(musicFields, key);
        case 6:
            return find(mobilityFields, key);
        default:
            return nullptr;
        }
    }
    static unsigned char* address(Proxy* p) {
        auto& w = *p->owner->world;
        switch (p->kind) {
        case 0:
            return reinterpret_cast<unsigned char*>(&w.jump);
        case 1:
            return reinterpret_cast<unsigned char*>(&w.rewind);
        case 2:
            return reinterpret_cast<unsigned char*>(&w.aster);
        case 5:
            return reinterpret_cast<unsigned char*>(&w.audio);
        case 6:
            return reinterpret_cast<unsigned char*>(&w.mobility);
        default:
            return reinterpret_cast<unsigned char*>(&w.kino);
        }
    }
    static int index(lua_State* L) {
        auto p = static_cast<Proxy*>(luaL_checkudata(L, 1, "echo.proxy"));
        if (p->kind == 4) {
            int i = int(luaL_checkinteger(L, 2));
            if (i < 1 || i > 64)
                return luaL_error(L, "state index must be 1..64");
            lua_pushnumber(L, p->owner->world->script.persistent[i - 1]);
            return 1;
        }
        auto f = field(p->kind, luaL_checkstring(L, 2));
        if (!f)
            return luaL_error(L, "unknown engine property");
        auto ptr = address(p) + f->offset;
        if (f->kind == 0)
            lua_pushnumber(L, *reinterpret_cast<float*>(ptr));
        else if (f->kind == 1)
            lua_pushinteger(L, *reinterpret_cast<int*>(ptr));
        else
            lua_pushboolean(L, *reinterpret_cast<bool*>(ptr));
        return 1;
    }
    static int newindex(lua_State* L) {
        auto p = static_cast<Proxy*>(luaL_checkudata(L, 1, "echo.proxy"));
        if (p->owner->executingCondition)
            return luaL_error(L, "Lua conditions cannot write WorldState");

        if (p->kind == 4) {
            int i = int(luaL_checkinteger(L, 2));
            double value = luaL_checknumber(L, 3);
            if (i < 1 || i > 64 || !std::isfinite(value))
                return luaL_error(L, "invalid persistent state");
            p->owner->world->script.persistent[i - 1] = value;
            return 0;
        }
        auto key = luaL_checkstring(L, 2);
        auto f = field(p->kind, key);
        if (!f)
            return luaL_error(L, "unknown engine property");
        auto ptr = address(p) + f->offset;
        if (p->kind == 6) {
            auto candidate = p->owner->world->mobility;
            auto target = reinterpret_cast<unsigned char*>(&candidate) + f->offset;
            if (f->kind == 2) {
                luaL_checktype(L, 3, LUA_TBOOLEAN);
                *reinterpret_cast<bool*>(target) = lua_toboolean(L, 3);
            } else {
                double v = luaL_checknumber(L, 3);
                if (!std::isfinite(v) || std::abs(v) > 4000 || (f->kind == 1 && std::floor(v) != v))
                    return luaL_error(L, "Invalid mobility parameter");
                if (f->kind == 1)
                    *reinterpret_cast<int*>(target) = int(v);
                else
                    *reinterpret_cast<float*>(target) = float(v);
            }
            bool valid = true;
            try {
                validateMobility(candidate);
            } catch (...) {
                valid = false;
            }
            if (!valid)
                return luaL_error(L, "Mobility outside supported range");
            p->owner->world->mobility = candidate;
            return 0;
        }
        if (f->kind == 2) {
            *reinterpret_cast<bool*>(ptr) = lua_toboolean(L, 3);
            return 0;
        }
        double v = luaL_checknumber(L, 3);
        if (!std::isfinite(v) || std::abs(v) > 100000)
            return luaL_error(L, "engine property outside numeric range");
        if (p->kind == 1) {
            if (!std::strcmp(key, "history_seconds") && (v < 1 || v > 30))
                return luaL_error(L, "history_seconds must be 1..30");
            if (!std::strcmp(key, "fast_bullet_margin") && (v < 0 || v > 64))
                return luaL_error(L, "fast_bullet_margin must be 0..64 logical pixels");
            if (!std::strcmp(key, "record_hz") && v != 30 && v != 60 && v != 120)
                return luaL_error(L, "record_hz must be 30,60,120");
            if ((!std::strcmp(key, "fast_rate") || !std::strcmp(key, "trace_rate")) &&
                (v < .1 || v > 20))
                return luaL_error(L, "invalid rewind rate");
        }
        if (f->kind == 0)
            *reinterpret_cast<float*>(ptr) = float(v);
        else
            *reinterpret_cast<int*>(ptr) = int(v);
        return 0;
    }
    static float number(lua_State* L, int table, const char* name, float fallback) {
        lua_getfield(L, table, name);
        float v = lua_isnil(L, -1) ? fallback : float(luaL_checknumber(L, -1));
        lua_pop(L, 1);
        if (!std::isfinite(v))
            luaL_error(L, "nonfinite DSL value");
        return v;
    }
    static bool boolean(lua_State* L, int table, const char* name, bool fallback) {
        lua_getfield(L, table, name);
        bool v = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1);
        lua_pop(L, 1);
        return v;
    }
    static int pattern(lua_State* L, int table) {
        lua_getfield(L, table, "pattern");
        const char* s = lua_isnil(L, -1) ? "ring" : luaL_checkstring(L, -1);
        const char* names[] = {"ring", "fan", "spiral", "wave", "formation"};
        int n = -1;
        for (int i = 0; i < 5; ++i)
            if (!std::strcmp(s, names[i]))
                n = i;
        lua_pop(L, 1);
        if (n < 0)
            luaL_error(L, "unknown bullet pattern");
        return n;
    }
    static int constructor(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_setfield(L, 1, "op");
        lua_settop(L, 1);
        return 1;
    }
    static Impl& owner(lua_State* L) {
        return *static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
    }
    static int volley(lua_State* L) {
        auto& r = owner(L);
        auto& w = *r.world;
        luaL_checktype(L, 1, LUA_TTABLE);
        Volley v;
        v.at = number(L, 1, "at", 1);
        if (v.at < .85f)
            return luaL_error(L, "volley telegraph must be at least .85 seconds");
        v.origin = {number(L, 1, "x", w.aster.x + 24), number(L, 1, "y", w.aster.y + 24)};
        v.angle = number(L, 1, "angle", 180) * std::numbers::pi_v<float> / 180;
        v.speed = number(L, 1, "speed", 140);
        v.count = std::uint16_t(std::clamp(int(number(L, 1, "count", 12)), 1, maxBullets));
        v.pattern = std::uint8_t(pattern(L, 1));
        v.aimed = boolean(L, 1, "aim", false);
        if (!w.aster.attack.volleys.push(v))
            return luaL_error(L, "volley capacity exceeded");
        return 0;
    }
    static int zone(lua_State* L) {
        auto& r = owner(L);
        Zone z;
        z.x = number(L, 1, "x", 0);
        z.y = number(L, 1, "y", 0);
        z.w = number(L, 1, "w", 32);
        z.h = number(L, 1, "h", 32);
        z.start = number(L, 1, "start", 1);
        z.end = number(L, 1, "end", 5);
        z.lead = std::max(.85f, number(L, 1, "lead", .85f));
        z.brake = boolean(L, 1, "brake", false);
        if (z.start < z.lead || z.end <= z.start)
            return luaL_error(L, "zone requires .85s warning and end > start");
        lua_getfield(L, 1, "label");
        if (lua_isstring(L, -1))
            z.label.set(lua_tostring(L, -1));
        lua_pop(L, 1);
        if (!r.world->aster.attack.zones.push(z))
            return luaL_error(L, "zone capacity exceeded");
        return 0;
    }
    static int random(lua_State* L) {
        auto& r = owner(L);
        auto& seed = r.world->randomSeed;
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        lua_pushnumber(L, double(seed) / 4294967296.0);
        return 1;
    }
    static ObjectHandle objectHandle(lua_State* L, int first = 1) {
        auto slot = luaL_checkinteger(L, first), generation = luaL_checkinteger(L, first + 1);
        if (slot < 0 || slot >= maxObjects || generation < 1 || generation > UINT32_MAX)
            luaL_error(L, "Invalid object handle");
        return {std::uint16_t(slot), std::uint32_t(generation)};
    }
    static int create(lua_State* L) {
        auto& r = owner(L);
        auto id = luaL_checkinteger(L, 1);
        float x = float(luaL_checknumber(L, 2)), y = float(luaL_checknumber(L, 3));
        const auto* t = findObjectTemplate(r.objects, std::uint64_t(id));
        if (!t || !std::isfinite(x) || !std::isfinite(y))
            return luaL_error(L, "Invalid object template or position");
        auto h = createObject(*r.world, *t, {x, y}, r.world->activeMap);
        if (h.slot == 65535) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, h.slot);
        lua_pushinteger(L, h.generation);
        return 2;
    }
    static int destroy(lua_State* L) {
        auto& r = owner(L);
        lua_pushboolean(L, destroyObject(*r.world, objectHandle(L)));
        return 1;
    }
    static int validObject(lua_State* L) {
        lua_pushboolean(L, resolveObject(*owner(L).world, objectHandle(L)) != nullptr);
        return 1;
    }
    static std::uint32_t dataId(lua_State* L) {
        auto id = luaL_checkinteger(L, 1);
        if (id < 1 || id > 1000000)
            luaL_error(L, "Database ID must be 1..1000000");
        return std::uint32_t(id);
    }
    static int dataDelta(lua_State* L) {
        auto delta = luaL_checkinteger(L, 2);
        if (delta < -1000000 || delta > 1000000)
            luaL_error(L, "Delta must be an integer within 1000000");
        return int(delta);
    }
    static int itemGet(lua_State* L) {
        auto* item = findItem(*owner(L).world, dataId(L));
        if (!item) {
            lua_pushnil(L);
            return 1;
        }
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, item->id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, item->count);
        lua_setfield(L, -2, "count");
        lua_pushinteger(L, item->limit);
        lua_setfield(L, -2, "limit");
        return 1;
    }
    static int itemCount(lua_State* L) {
        auto* item = findItem(*owner(L).world, dataId(L));
        if (item)
            lua_pushinteger(L, item->count);
        else
            lua_pushnil(L);
        return 1;
    }
    static int itemChange(lua_State* L) {
        auto& r = owner(L);
        if (r.executingCondition)
            return luaL_error(L, "Conditions cannot change inventory");
        auto result = changeItem(*r.world, dataId(L), dataDelta(L));
        lua_pushinteger(L, int(result));
        return 1;
    }
    static int questGet(lua_State* L) {
        auto* q = findQuest(*owner(L).world, dataId(L));
        if (!q) {
            lua_pushnil(L);
            return 1;
        }
        lua_createtable(L, 0, 5);
        auto field = [&](const char* name, int v) {
            lua_pushinteger(L, v);
            lua_setfield(L, -2, name);
        };
        field("id", q->id);
        field("status", int(q->status));
        field("progress", q->progress);
        field("target", q->target);
        lua_pushboolean(L, q->autoComplete);
        lua_setfield(L, -2, "auto_complete");
        return 1;
    }
    static int questSet(lua_State* L) {
        auto& r = owner(L);
        if (r.executingCondition)
            return luaL_error(L, "Conditions cannot change quests");
        auto status = luaL_checkinteger(L, 2);
        if (status < 0 || status > 3)
            return luaL_error(L, "Quest status: 0 inactive / 1 active / 2 completed / 3 failed");
        lua_pushinteger(L, int(setQuestStatus(*r.world, dataId(L), QuestStatus(status))));
        return 1;
    }
    static int questProgress(lua_State* L) {
        auto& r = owner(L);
        if (r.executingCondition)
            return luaL_error(L, "Conditions cannot change quests");
        lua_pushinteger(L, int(addQuestProgress(*r.world, dataId(L), dataDelta(L))));
        return 1;
    }
    static int getObject(lua_State* L) {
        auto* o = resolveObject(*owner(L).world, objectHandle(L));
        if (!o) {
            lua_pushnil(L);
            return 1;
        }
        lua_createtable(L, 0, 17);
        auto numeric = [&](const char* field, double value) {
            lua_pushnumber(L, value);
            lua_setfield(L, -2, field);
        };
        auto flag = [&](const char* field, bool value) {
            lua_pushboolean(L, value);
            lua_setfield(L, -2, field);
        };
        // Query is a copy; conditions can inspect it without mutating gameplay.
        numeric("x", o->x);
        numeric("y", o->y);
        numeric("px", o->px);
        numeric("py", o->py);
        numeric("vx", o->vx);
        numeric("vy", o->vy);
        numeric("hp", o->hp);
        numeric("map", o->map + 1);
        numeric("kind", int(o->kind));
        numeric("standing_platform", o->standingPlatformId);
        numeric("spring_cooldown", o->springCooldown);
        flag("grounded", o->grounded);
        flag("rides_platforms", o->ridesPlatforms);
        flag("crush_kills", o->crushKills);
        flag("crushed", o->crushed);
        return 1;
    }
    static bool strictBoolean(lua_State* L, int table, const char* key, bool fallback) {
        lua_getfield(L, table, key);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return fallback;
        }
        luaL_checktype(L, -1, LUA_TBOOLEAN);
        bool result = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return result;
    }
    static int configureObjectMovement(lua_State* L) {
        auto& r = owner(L);
        if (r.executingCondition)
            return luaL_error(L, "Lua conditions cannot configure objects");
        auto* o = resolveObject(*r.world, objectHandle(L));
        luaL_checktype(L, 3, LUA_TTABLE);
        if (!o)
            return luaL_error(L, "Stale or missing object handle");
        if (o->kind == ObjectKind::Player || o->kind == ObjectKind::Boss)
            return luaL_error(L, "Player and Boss use their dedicated movement settings");
        auto next = *o;
        next.ridesPlatforms = strictBoolean(L, 3, "rides_platforms", next.ridesPlatforms);
        next.crushKills = strictBoolean(L, 3, "crush_kills", next.crushKills);
        if (!next.ridesPlatforms) {
            next.standingPlatformId = -1;
            next.grounded = false;
        }
        *o = next;
        return 0;
    }
    static int damage(lua_State* L) {
        auto& r = owner(L);
        auto h = objectHandle(L);
        auto amount = luaL_checkinteger(L, 3);
        if (amount < 0 || amount > 1000000)
            return luaL_error(L, "Damage must be 0..1000000");
        damageObject(*r.world, h, int(amount));
        return 0;
    }
    static int playAnimation(lua_State* L) {
        auto& r = owner(L);
        auto handle = objectHandle(L);
        auto clip = luaL_checkinteger(L, 3);
        if (clip < 0)
            return luaL_error(L, "Animation ID must be nonnegative");
        lua_pushboolean(L, requestAnimation(*r.world, r.animations, handle, std::uint64_t(clip)));
        return 1;
    }
    static int clearGroup(lua_State* L) {
        auto group = luaL_checkinteger(L, 1);
        if (group < 0 || group > 65535)
            return luaL_error(L, "Bullet group: 0..65535");
        lua_pushinteger(L, clearBulletGroup(*owner(L).world, std::uint16_t(group)));
        return 1;
    }
    static int emitter(lua_State* L) {
        auto id = luaL_checkinteger(L, 1);
        if (id <= 0)
            return luaL_error(L, "Emitter ID must be positive");
        lua_pushboolean(L, setBarrageEmitter(*owner(L).world, std::uint64_t(id), lua_toboolean(L, 2),
                                             lua_toboolean(L, 3)));
        return 1;
    }
    static int playSE(lua_State* L) {
        auto& r = owner(L);
        int id = int(luaL_checkinteger(L, 1)) - 1;
        float gain = float(luaL_optnumber(L, 2, 1)), pitch = float(luaL_optnumber(L, 3, 1));
        if (id < 0 || id >= 32 || !std::isfinite(gain) || !std::isfinite(pitch))
            return luaL_error(L, "invalid SE");
        lua_pushboolean(L, playSound(*r.world, id, gain, pitch));
        return 1;
    }
    static int getSwitch(lua_State* L) {
        auto id = luaL_checkinteger(L, 1) - 1;
        if (id < 0 || id >= 128)
            return luaL_error(L, "switch ID must be 1..128");
        lua_pushboolean(L, owner(L).world->events.switches[id]);
        return 1;
    }
    static int setSwitch(lua_State* L) {
        auto id = luaL_checkinteger(L, 1) - 1;
        if (id < 0 || id >= 128)
            return luaL_error(L, "switch ID must be 1..128");
        owner(L).world->events.switches[id] = lua_toboolean(L, 2);
        return 0;
    }
    static int getVariable(lua_State* L) {
        auto id = luaL_checkinteger(L, 1) - 1;
        if (id < 0 || id >= 64)
            return luaL_error(L, "variable ID must be 1..64");
        lua_pushinteger(L, owner(L).world->events.variables[id]);
        return 1;
    }
    static int setVariable(lua_State* L) {
        auto id = luaL_checkinteger(L, 1) - 1, value = luaL_checkinteger(L, 2);
        if (id < 0 || id >= 64 || value < INT32_MIN || value > INT32_MAX)
            return luaL_error(L, "invalid event variable");
        owner(L).world->events.variables[id] = int(value);
        return 0;
    }
    static int fadeBGM(lua_State* L) {
        auto& r = owner(L);
        float seconds = float(luaL_checknumber(L, 1)), target = float(luaL_optnumber(L, 2, 0));
        if (!std::isfinite(seconds) || !std::isfinite(target) || seconds < 0 || seconds > 120)
            return luaL_error(L, "invalid BGM fade");
        fadeMusic(*r.world, target, seconds);
        return 0;
    }
    static int playBGM(lua_State* L) {
        startMusic(*owner(L).world, lua_isnone(L, 1) || lua_toboolean(L, 1));
        return 0;
    }
    static int stopBGM(lua_State* L) {
        stopMusic(*owner(L).world);
        return 0;
    }
    static int barrage(lua_State* L) {
        auto& r = owner(L);
        r.world->script.program.clear();
        r.parseList(1, 0);
        r.world->script.pc = 0;
        r.world->script.wait = 0;
        return 0;
    }
    void parseList(int index, int depth) {
        index = lua_absindex(L, index);
        luaL_checktype(L, index, LUA_TTABLE);
        if (depth > 8)
            luaL_error(L, "DSL nesting too deep");
        auto n = lua_rawlen(L, index);
        if (n > 256)
            luaL_error(L, "DSL program too long");
        for (std::size_t i = 1; i <= n; ++i) {
            lua_geti(L, index, i);
            int table = lua_gettop(L);
            luaL_checktype(L, table, LUA_TTABLE);
            lua_getfield(L, table, "op");
            const char* name = luaL_checkstring(L, -1);
            const char* op = name;
            lua_pop(L, 1);
            if (!std::strcmp(op, "repeat")) {
                int times = std::clamp(int(number(L, table, "times", 1)), 0, 256);
                lua_getfield(L, table, "body");
                for (int repeat = 0; repeat < times; ++repeat)
                    parseList(-1, depth + 1);
                lua_pop(L, 1);
            } else {
                Instruction ins;
                if (!std::strcmp(op, "fire")) {
                    ins.op = OpCode::Fire;
                    ins.pattern = pattern(L, table);
                    ins.count = std::clamp(int(number(L, table, "count", 12)), 1, maxBullets);
                    ins.speed = std::clamp(number(L, table, "speed", 140), 0.0f, 20000.0f);
                } else if (!std::strcmp(op, "wait")) {
                    ins.op = OpCode::Wait;
                    ins.value = number(L, table, "seconds", 1);
                } else if (!std::strcmp(op, "aim"))
                    ins.op = OpCode::Aim;
                else if (!std::strcmp(op, "incremental")) {
                    ins.op = OpCode::Incremental;
                    ins.value = number(L, table, "angle", 0) * std::numbers::pi_v<float> / 180;
                } else if (!std::strcmp(op, "set_direction")) {
                    ins.op = OpCode::SetDirection;
                    ins.value = number(L, table, "angle", 0) * std::numbers::pi_v<float> / 180;
                } else
                    luaL_error(L, "Unknown DSL primitive");
                if (!world->script.program.push(ins))
                    luaL_error(L, "DSL flattened capacity exceeds 256");
            }
            lua_pop(L, 1);
        }
    }
    static int choice(lua_State* L, int table, const char* key, std::initializer_list<const char*> names,
                      int fallback) {
        lua_getfield(L, table, key);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return fallback;
        }
        const char* v = luaL_checkstring(L, -1);
        int i = 0;
        for (auto name : names) {
            if (!std::strcmp(v, name)) {
                lua_pop(L, 1);
                return i;
            }
            ++i;
        }
        return luaL_error(L, "Invalid option %s", key);
    }
    static int configureFocus(lua_State* L) {
        auto* self = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
        luaL_checktype(L, 1, LUA_TTABLE);
        auto c = self->world->focus;
        c.enabled = boolean(L, 1, "enabled", c.enabled);
        c.mode = FocusMode(choice(L, 1, "mode", {"hold", "toggle", "automatic"}, int(c.mode)));
        c.when = FocusWhen(choice(L, 1, "when", {"always", "bullets", "boss", "switch"}, int(c.when)));
        c.ground = boolean(L, 1, "ground", c.ground);
        c.air = boolean(L, 1, "air", c.air);
        c.instant = boolean(L, 1, "instant", c.instant);
        c.showIndicator = boolean(L, 1, "indicator", c.showIndicator);
        c.speedRatio = number(L, 1, "speed_ratio", c.speedRatio);
        auto integral = [&](const char* key, int old) {
            float v = number(L, 1, key, float(old));
            if (v != std::floor(v) || v < -1 || v > 254)
                luaL_error(L, "Integer required: %s", key);
            return int(v);
        };
        c.key = integral("key", c.key);
        c.gateSwitch = integral("switch", c.gateSwitch + 1) - 1;
        c.map = integral("map", c.map + 1) - 1;
        try {
            validateFocus(c);
            if (c.map >= self->mapCount)
                throw std::runtime_error("Focus map does not exist");
            self->world->focus = c;
        } catch (const std::exception& e) {
            lua_pushstring(L, e.what());
            return lua_error(L);
        }
        return 0;
    }
    static int configureHurt(lua_State* L) {
        auto* self = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
        luaL_checktype(L, 1, LUA_TTABLE);
        auto c = self->world->hurt;
        c.source = HurtSource(choice(L, 1, "source", {"animation", "whole", "custom"}, int(c.source)));
        c.method = HurtMethod(choice(L, 1, "method", {"swept", "overlap"}, int(c.method)));
        c.show =
            HurtShow(choice(L, 1, "show", {"never", "always", "focus", "key", "danger"}, int(c.show)));
        c.touching = boolean(L, 1, "touching", c.touching);
        c.mirror = boolean(L, 1, "mirror", c.mirror);
        c.projectileBox = boolean(L, 1, "projectile_box", c.projectileBox);
        c.opacity = number(L, 1, "opacity", c.opacity);
        c.displayScale = number(L, 1, "display_scale", c.displayScale);
        try {
            validateHurt(c);
            self->world->hurt = c;
        } catch (const std::exception& e) {
            lua_pushstring(L, e.what());
            return lua_error(L);
        }
        return 0;
    }
    static int setHurtRegion(lua_State* L) {
        auto* self = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
        auto i = luaL_checkinteger(L, 1);
        luaL_checktype(L, 2, LUA_TTABLE);
        auto c = self->world->hurt;
        if (i < 1 || i > 8 || i > lua_Integer(c.count) + 1)
            return luaL_error(L, "Regions must be added consecutively, 1..8");
        auto r = c.regions[i - 1];
        r.shape = HurtShape(choice(
            L, 2, "shape", {"circle", "rectangle", "ellipse", "capsule", "polygon"}, int(r.shape)));
        r.enabled = boolean(L, 2, "enabled", r.enabled);
        r.focusOnly = boolean(L, 2, "focus_only", r.focusOnly);
        float mask = number(L, 2, "mask", r.mask);
        if (mask < 0 || mask > 7 || mask != std::floor(mask))
            return luaL_error(L, "mask 0..7: bullets=1 contact=2 melee=4");
        r.mask = std::uint8_t(mask);
        r.anchorX = number(L, 2, "anchor_x", r.anchorX);
        r.anchorY = number(L, 2, "anchor_y", r.anchorY);
        r.offsetX = number(L, 2, "offset_x", r.offsetX);
        r.offsetY = number(L, 2, "offset_y", r.offsetY);
        r.radius = number(L, 2, "radius", r.radius);
        r.width = number(L, 2, "width", r.width);
        r.height = number(L, 2, "height", r.height);
        r.angle = number(L, 2, "angle_degrees", r.angle * 180 / std::numbers::pi_v<float>) *
                  std::numbers::pi_v<float> / 180;
        lua_getfield(L, 2, "vertices");
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TTABLE);
            auto n = lua_rawlen(L, -1);
            if (n > 16)
                return luaL_error(L, "maximum 16 vertices");
            r.vertices.clear();
            for (std::size_t j = 1; j <= n; ++j) {
                lua_rawgeti(L, -1, j);
                luaL_checktype(L, -1, LUA_TTABLE);
                lua_rawgeti(L, -1, 1);
                float x = float(luaL_checknumber(L, -1));
                lua_pop(L, 1);
                lua_rawgeti(L, -1, 2);
                float y = float(luaL_checknumber(L, -1));
                lua_pop(L, 2);
                r.vertices.push({x, y});
            }
        }
        lua_pop(L, 1);
        c.count = std::max(c.count, std::uint32_t(i));
        c.regions[i - 1] = r;
        try {
            validateHurt(c);
            self->world->hurt = c;
        } catch (const std::exception& e) {
            lua_pushstring(L, e.what());
            return lua_error(L);
        }
        return 0;
    }
    static CollisionClass collisionClass(lua_State* L, int index) {
        static const char* const names[] = {"terrain",    "player", "enemy",   "npc",  "prop",
                                            "projectile", "attack", "trigger", nullptr};
        return CollisionClass(luaL_checkoption(L, index, nullptr, names));
    }
    static int collisionPair(lua_State* L) {
        auto a = collisionClass(L, 1), b = collisionClass(L, 2);
        lua_pushboolean(L, collisionAllowed(*owner(L).world, a, b));
        return 1;
    }
    static int setPair(lua_State* L) {
        auto a = collisionClass(L, 1), b = collisionClass(L, 2);
        luaL_checktype(L, 3, LUA_TBOOLEAN);
        setCollisionPair(owner(L).world->collisions, a, b, lua_toboolean(L, 3));
        return 0;
    }
    static int getPlatform(lua_State* L) {
        auto& r = owner(L);
        auto id = luaL_checkinteger(L, 1);
        const PlatformFeature* found = nullptr;
        for (const auto& f : r.world->level.features)
            if (f.id == id) {
                found = &f;
                break;
            }
        if (!found) {
            lua_pushnil(L);
            return 1;
        }
        lua_createtable(L, 0, 15);
        auto numberField = [&](const char* name, double value) {
            lua_pushnumber(L, value);
            lua_setfield(L, -2, name);
        };
        numberField("id", found->id);
        numberField("kind", int(found->kind));
        numberField("stage", found->stage);
        numberField("x", found->bounds.x);
        numberField("y", found->bounds.y);
        numberField("elapsed", found->elapsed);
        numberField("speed", found->speed);
        numberField("delay", found->delay);
        numberField("reset_seconds", found->resetSeconds);
        numberField("travel_x", found->travel.x);
        numberField("travel_y", found->travel.y);
        numberField("progress", found->progress);
        numberField("direction", found->direction);
        lua_pushboolean(L, found->repeat);
        lua_setfield(L, -2, "repeat_route");
        lua_pushboolean(L, found->enabled);
        lua_setfield(L, -2, "enabled");
        return 1;
    }
    static int configurePlatform(lua_State* L) {
        auto& r = owner(L);
        if (r.executingCondition)
            return luaL_error(L, "Lua conditions cannot configure platforms");
        auto id = luaL_checkinteger(L, 1);
        luaL_checktype(L, 2, LUA_TTABLE);
        PlatformFeature* found = nullptr;
        for (auto& f : r.world->level.features)
            if (f.id == id) {
                found = &f;
                break;
            }
        if (!found)
            return luaL_error(L, "Unknown platform feature ID in active map");
        auto next = *found;
        next.enabled = boolean(L, 2, "enabled", next.enabled);
        next.speed = number(L, 2, "speed", next.speed);
        next.delay = number(L, 2, "delay", next.delay);
        next.resetSeconds = number(L, 2, "reset_seconds", next.resetSeconds);
        next.travel.x = number(L, 2, "travel_x", next.travel.x);
        next.travel.y = number(L, 2, "travel_y", next.travel.y);
        next.repeat = boolean(L, 2, "repeat_route", next.repeat);
        if (next.kind == PlatformKind::Lift) {
            // Editing a running route preserves its current position and progress.
            // Rebase its runtime origin instead of teleporting / carrying through obstacles.
            next.base.x = next.bounds.x - next.travel.x * next.progress;
            next.base.y = next.bounds.y - next.travel.y * next.progress;
            if (next.stage == 2 && next.repeat) {
                next.stage = 1;
                next.elapsed = 0;
            }
        }
        bool valid = true;
        try {
            validateFeature(next);
        } catch (...) {
            valid = false;
        }
        if (!valid)
            return luaL_error(L, "Platform parameters outside range");
        *found = next;
        return 0;
    }
    void proxy(const char* name, int kind) {
        auto p = static_cast<Proxy*>(lua_newuserdatauv(L, sizeof(Proxy), 0));
        *p = {this, kind};
        luaL_setmetatable(L, "echo.proxy");
        lua_setglobal(L, name);
    }
    void setup() {
        luaL_newmetatable(L, "echo.proxy");
        lua_pushcfunction(L, index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pop(L, 1);
        proxy("jump", 0);
        proxy("rewind", 1);
        proxy("boss", 2);
        proxy("player", 3);
        proxy("persistent", 4);
        proxy("music", 5);
        proxy("mobility", 6);
        lua_newtable(L);
        const char* fields[] = {"volley",
                                "zone",
                                "random",
                                "play_se",
                                "fade_bgm",
                                "play_bgm",
                                "stop_bgm",
                                "get_switch",
                                "set_switch",
                                "get_variable",
                                "set_variable",
                                "create_object",
                                "destroy_object",
                                "object_valid",
                                "damage_object",
                                "play_animation",
                                "clear_bullet_group",
                                "set_emitter",
                                "configure_focus",
                                "configure_hurt",
                                "set_hurt_region",
                                "collision_pair",
                                "set_collision_pair",
                                "event_context",
                                "get_self_switch",
                                "set_self_switch",
                                "set_event_control",
                                "request_transfer",
                                "configure_platform",
                                "get_platform",
                                "get_object",
                                "configure_object_movement",
                                "get_item_count",
                                "change_item",
                                "get_quest",
                                "set_quest_status",
                                "add_quest_progress",
                                "get_item"};
        lua_CFunction funcs[] = {volley,
                                 zone,
                                 random,
                                 playSE,
                                 fadeBGM,
                                 playBGM,
                                 stopBGM,
                                 getSwitch,
                                 setSwitch,
                                 getVariable,
                                 setVariable,
                                 create,
                                 destroy,
                                 validObject,
                                 damage,
                                 playAnimation,
                                 clearGroup,
                                 emitter,
                                 configureFocus,
                                 configureHurt,
                                 setHurtRegion,
                                 collisionPair,
                                 setPair,
                                 eventContext,
                                 getSelfSwitch,
                                 setSelfSwitch,
                                 eventControl,
                                 transfer,
                                 configurePlatform,
                                 getPlatform,
                                 getObject,
                                 configureObjectMovement,
                                 itemCount,
                                 itemChange,
                                 questGet,
                                 questSet,
                                 questProgress,
                                 itemGet};
        static_assert(std::size(fields) == std::size(funcs));
        for (std::size_t i = 0; i < std::size(fields); ++i) {
            lua_pushlightuserdata(L, this);
            lua_pushcclosure(L, funcs[i], 1);
            lua_setfield(L, -2, fields[i]);
        }
        lua_getglobal(L, "persistent");
        lua_setfield(L, -2, "state");
        lua_setglobal(L, "engine");
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, barrage, 1);
        lua_setglobal(L, "barrage");
        const char* ops[] = {"fire", "wait", "aim", "incremental", "set_direction", "repeat"};
        for (auto op : ops) {
            lua_pushstring(L, op);
            lua_pushcclosure(L, constructor, 1);
            lua_setglobal(L, !std::strcmp(op, "repeat") ? "repeat_pattern" : op);
        }
    }
    bool hook(const char* name) {
        lua_getglobal(L, name);
        if (lua_isfunction(L, -1))
            return true;
        lua_pop(L, 1);
        return false;
    }
};
ScriptRuntime::ScriptRuntime() : p_(std::make_unique<Impl>()) {}
ScriptRuntime::~ScriptRuntime() = default;
const std::string& ScriptRuntime::eventError() const {
    return p_->lastEventError;
}
void ScriptRuntime::validateEventScripts(const EventMap& map, bool common, int index) {
    auto refs = std::make_unique<Impl::ScriptRefs>();
    refs->fill({});
    try {
        p_->compileMap(map, common, index, *refs);
    } catch (...) {
        p_->releaseRefs(*refs);
        throw;
    }
    p_->releaseRefs(*refs);
}
void ScriptRuntime::prepareEventScripts(const std::vector<EventMap>& maps, const EventMap& common) {
    if (maps.size() > 8)
        throw std::runtime_error("Event Lua supports at most eight maps");
    auto refs = std::make_unique<Impl::ScriptRefs>();
    refs->fill({});
    try {
        for (int map = 0; map < int(maps.size()); ++map)
            p_->compileMap(maps[map], false, map, *refs);
        p_->compileMap(common, true, 0, *refs);
    } catch (...) {
        p_->releaseRefs(*refs);
        throw;
    }
    p_->releaseRefs(*p_->eventScripts);
    p_->eventScripts.swap(refs);
    p_->eventInstructionsRemaining = 100000;
    p_->eventCallsRemaining = 32;
    p_->lastEventError.clear();
}
void ScriptRuntime::beginEventStep(WorldState&) {
    // Derived intra-step budget, reset before scheduling; never persistent across frames.
    p_->eventInstructionsRemaining = 100000;
    p_->eventCallsRemaining = 32;
}
bool ScriptRuntime::eventScript(WorldState& w, const EventScriptContext& c) {
    return runEvent(w, c, nullptr);
}
bool ScriptRuntime::eventConditionAvailable() const {
    return p_->eventCallsRemaining > 0 && p_->eventInstructionsRemaining > 0;
}
bool ScriptRuntime::eventCondition(WorldState& w, const EventScriptContext& c, bool& result) {
    result = false;
    return runEvent(w, c, &result);
}
bool ScriptRuntime::runEvent(WorldState& w, const EventScriptContext& c, bool* condition) {
    p_->world = &w;
    int slot = Impl::eventSlot(c);
    if (slot < 0 || (*p_->eventScripts)[slot].ref == LUA_NOREF ||
        (*p_->eventScripts)[slot].condition != (condition != nullptr)) {
        p_->lastEventError = Impl::eventLocation(c) + ": no compiled Lua command";
        return false;
    }
    if (p_->eventCallsRemaining <= 0 || p_->eventInstructionsRemaining <= 0) {
        p_->lastEventError = Impl::eventLocation(c) + ": Event Lua step budget exhausted";
        return false;
    }
    --p_->eventCallsRemaining;
    int callBudget = p_->eventInstructionsRemaining;
    // Persistent data belongs only to WorldState. An invalid command cannot commit partial writes.
    std::memcpy(p_->eventBackup.get(), &w, sizeof(w));
    p_->currentEvent = c;
    p_->executingEvent = true;
    p_->executingCondition = condition != nullptr;
    int top = lua_gettop(p_->L);
    lua_rawgeti(p_->L, LUA_REGISTRYINDEX, (*p_->eventScripts)[slot].ref);
    lua_rawgeti(p_->L, LUA_REGISTRYINDEX, condition ? p_->conditionEnvironment : p_->eventEnvironment);
    lua_setupvalue(p_->L, -2, 1); // Reset even if the previous invocation rebound _ENV.
    try {
        p_->protectedCall(0, condition ? LUA_MULTRET : 0, callBudget);
        if (condition) {
            if (lua_gettop(p_->L) != top + 1 || !lua_isboolean(p_->L, -1))
                throw std::runtime_error("Lua condition must return exactly one boolean");
            if (std::memcmp(&w, p_->eventBackup.get(), sizeof(w)) != 0)
                throw std::runtime_error("Lua condition changed WorldState");
            *condition = lua_toboolean(p_->L, -1) != 0;
            // Read-only conditions cannot leak effects, including future getter regressions.
            std::memcpy(&w, p_->eventBackup.get(), sizeof(w));
            lua_settop(p_->L, top);
        }
        p_->eventInstructionsRemaining = std::max(0, p_->instructionBudget);
        p_->executingEvent = false;
        p_->executingCondition = false;
        return true;
    } catch (const std::exception& e) {
        p_->eventInstructionsRemaining = std::max(0, p_->instructionBudget);
        std::memcpy(&w, p_->eventBackup.get(), sizeof(w));
        p_->lastEventError = Impl::eventLocation(c) + ": " + e.what();
        lua_settop(p_->L, top);
        p_->executingEvent = false;
        p_->executingCondition = false;
        return false;
    }
}
void ScriptRuntime::setObjects(const ObjectLibrary& library) {
    validateObjects(library);
    p_->objects = library;
}
void ScriptRuntime::setAnimations(const AnimationLibrary& library) {
    validateAnimations(library);
    p_->animations = library;
}
void ScriptRuntime::load(Resources& resources, WorldState& w) {
    p_->world = &w;
    p_->objects = resources.contains("objects.echo") ? loadObjects(resources.text("objects.echo"))
                                                     : ObjectLibrary{};
    p_->animations = resources.contains("animations.echo")
                         ? loadAnimations(resources.text("animations.echo"))
                         : AnimationLibrary{};
    auto cfg = resources.config();
    p_->mapCount = int(cfg.maps.size());
    w.collisions = cfg.collisions;
    w.hurt = cfg.hurt;
    w.focus = cfg.focus;
    p_->setup();
    p_->setupEventEnvironment();
    p_->setupEventEnvironment(true);
    for (auto& name : resources.config().scripts) {
        auto source = resources.text(name);
        if (luaL_loadbuffer(p_->L, source.data(), source.size(), name.c_str()) != LUA_OK)
            throw std::runtime_error(lua_tostring(p_->L, -1));
        p_->protectedCall(0, 0);
    }
    // Captured persistent Lua state must supply explicit synchronization hooks.
    const char* callbacks[] = {"on_collision", "on_enter_region", "on_key_press", "on_state_change",
                               "on_boss_attack"};
    bool serializers = true;
    for (auto name : {"on_snapshot", "on_restore"}) {
        lua_getglobal(p_->L, name);
        serializers &= lua_isfunction(p_->L, -1);
        lua_pop(p_->L, 1);
    }
    for (auto name : callbacks) {
        lua_getglobal(p_->L, name);
        if (lua_isfunction(p_->L, -1))
            for (int i = 1;; ++i) {
                const char* up = lua_getupvalue(p_->L, -1, i);
                if (!up)
                    break;
                bool captured = std::strcmp(up, "_ENV") != 0;
                lua_pop(p_->L, 1);
                if (captured && !serializers)
                    throw std::runtime_error("Lua callback captures state: supply "
                                             "on_snapshot/on_restore or use engine.state");
            }
        lua_pop(p_->L, 1);
    }
    lua_gc(p_->L, LUA_GCGEN, 0, 0);
    p_->loaded = true;
    beforeSnapshot(w);
}
void ScriptRuntime::collision(WorldState& w, std::uint32_t id) {
    p_->world = &w;
    if (p_->hook("on_collision")) {
        lua_pushinteger(p_->L, id);
        lua_pushstring(p_->L, "player");
        p_->protectedCall(2, 0);
    }
}
void ScriptRuntime::region(WorldState& w, int zone) {
    p_->world = &w;
    if (p_->hook("on_enter_region")) {
        lua_pushinteger(p_->L, zone);
        p_->protectedCall(1, 0);
    }
}
void ScriptRuntime::key(WorldState& w, const char* key) {
    p_->world = &w;
    if (p_->hook("on_key_press")) {
        lua_pushstring(p_->L, key);
        p_->protectedCall(1, 0);
    }
}
void ScriptRuntime::stateChange(WorldState& w, TimeState from, TimeState to) {
    p_->world = &w;
    if (p_->hook("on_state_change")) {
        lua_pushinteger(p_->L, int(from));
        lua_pushinteger(p_->L, int(to));
        p_->protectedCall(2, 0);
    }
}
void ScriptRuntime::planAttack(WorldState& w) {
    p_->world = &w;
    if (p_->hook("on_boss_attack")) {
        lua_pushinteger(p_->L, w.aster.phaseId);
        lua_pushinteger(p_->L, w.aster.attack.variant);
        p_->protectedCall(2, 0);
    }
}
void ScriptRuntime::beforeSnapshot(WorldState& w) {
    p_->world = &w;
    if (p_->loaded && p_->hook("on_snapshot")) {
        p_->protectedCall(0, 1);
        std::size_t n;
        const char* text = lua_tolstring(p_->L, -1, &n);
        if (!text || n >= w.script.serialized.data.size())
            throw std::runtime_error("on_snapshot must return a string <=4095 bytes");
        w.script.serialized.set({text, n});
        w.script.serializedLength = n;
        lua_pop(p_->L, 1);
    }
}
void ScriptRuntime::afterRestore(WorldState& w) {
    p_->world = &w;
    if (p_->loaded && p_->hook("on_restore")) {
        lua_pushlstring(p_->L, w.script.serialized.c_str(), w.script.serializedLength);
        p_->protectedCall(1, 0);
    }
}
} // namespace echo
