#pragma once
#include "history.hpp"
#include "events.hpp"
#include "objects.hpp"
#include "animations.hpp"
#include "barrage.hpp"
#include <functional>
#include <vector>
namespace echo {
struct Input {
    float move{};
    bool jump{}, down{}, fast{}, trace{}, restart{}, confirm{}, focus{}, showHurt{}, dash{}, up{};
};
struct SimulationStats {
    std::uint32_t collisionCandidates{}, updatedBullets{}, terrainCandidates{};
};
struct Hooks : EventScriptExecutor {
    virtual ~Hooks() = default;
    virtual void prepareEventScripts(const std::vector<EventMap>&, const EventMap&) {}
    virtual void collision(WorldState&, std::uint32_t) {}
    virtual void region(WorldState&, int) {}
    virtual void key(WorldState&, const char*) {}
    virtual void stateChange(WorldState&, TimeState, TimeState) {}
    virtual void planAttack(WorldState&) {}
    virtual void beforeSnapshot(WorldState&) {}
    virtual void afterRestore(WorldState&) {}
};
void stepPhysics(WorldState&, const Input&, float);
// Recompute support only after an R chord changes the restored player's position.
// T restores historical contact state verbatim.
void refreshGroundContact(WorldState&);
bool segmentRect(Vec2, Vec2, Rect, float* entry = nullptr);
bool spawnBullet(WorldState&, Bullet);
void firePattern(WorldState&, const Instruction&, Vec2, float, bool);
void stepBullets(WorldState&, float, Vec2 previousPlayer, SimulationStats&, Hooks*,
                 const Rect* previousHurt = nullptr, bool previousBox = false,
                 const HurtConfig* previousConfig = nullptr, const KinoState* previousKino = nullptr);
void stepBoss(WorldState&, float, Hooks*);
void stepProgram(WorldState&, float);
class Simulation {
    History history_;
    std::unique_ptr<WorldState> world_, initial_;
    std::vector<LevelState> maps_;
    std::vector<EventMap> eventMaps_;
    EventMap commonEvents_;
    ObjectLibrary objects_;
    AnimationLibrary animations_;
    BarrageLibrary barrage_;
    void advanceEvents(const Input&, float);
    bool transferMap(int, Vec2, bool);
    void requestTransfer(int, Vec2, bool, const TransitionConfig&);
    void advanceTransition(float);
    void recordElapsed(float);
    Hooks* hooks_{};
    void forward(const Input&, float);
    void rewindTick(const Input&, float);
    void rewindStep(const Input&, float heldSeconds, int cursor);
    void begin(TimeState);
    void finish(bool impact = false);
    void change(TimeState);

  public:
    Simulation(std::size_t budget = 512ull * 1024 * 1024);
    WorldState& world() {
        return *world_;
    }
    const WorldState& world() const {
        return *world_;
    }
    History& history() {
        return history_;
    }
    const History& history() const {
        return history_;
    }
    SimulationStats stats{};
    void setMaps(const std::vector<LevelState>& maps) {
        maps_ = maps;
    } // immutable during Play, only set before reset
    void setEvents(const std::vector<EventMap>& maps, const EventMap& common) {
        for (const auto& map : maps)
            validateEvents(map);
        validateEvents(common, true);
        auto nextMaps = maps;
        auto nextCommon = common;
        if (hooks_)
            hooks_->prepareEventScripts(nextMaps, nextCommon);
        eventMaps_ = std::move(nextMaps);
        commonEvents_ = std::move(nextCommon);
        // Program changes are an explicit control boundary, never an old timeline edit.
        cancelEvents(*world_, true);
        world_->events = {};
        world_->controlInput = {};
        history_.clear(HistoryClearReason::EventDefinitions);
    }
    void setObjects(const ObjectLibrary& library) {
        validateObjects(library);
        objects_ = library;
    } // Definitions are immutable during Play; instances are WorldState.
    void setAnimations(const AnimationLibrary& library) {
        validateAnimations(library);
        animations_ = library;
    }
    void setBarrage(const BarrageLibrary& library) {
        validateBarrage(library);
        barrage_ = library;
    }
    void setHooks(Hooks* h) {
        if (h)
            h->prepareEventScripts(eventMaps_, commonEvents_);
        hooks_ = h;
    }
    void reset(const WorldState&, HistoryClearReason reason = HistoryClearReason::Reset);
    void tick(const Input&, float dt = fixedDt);
    void record();
    WorldState snapshot() const {
        return echo::snapshot(*world_);
    }
    void restore(const WorldState& s) {
        echo::restore(*world_, s);
        if (hooks_)
            hooks_->afterRestore(*world_);
    }
    void truncateToCursor(int cursor) {
        history_.truncateToCursor(cursor);
    }
    void configureRewind(RewindConfig);
    void seekHistory(int cursor);
    void commitRuntimeEdit();
};
} // namespace echo
