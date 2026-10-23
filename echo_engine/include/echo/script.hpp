#pragma once
#include "resources.hpp"
#include "simulation.hpp"
#include <memory>
namespace echo {
class ScriptRuntime : public Hooks {
  public:
    struct Impl;

  private:
    std::unique_ptr<Impl> p_;
    bool runEvent(WorldState&, const EventScriptContext&, bool* condition);

  public:
    ScriptRuntime();
    ~ScriptRuntime();
    void load(Resources&, WorldState&);
    void prepareEventScripts(const std::vector<EventMap>&, const EventMap&) override;
    void validateEventScripts(const EventMap&, bool common = false, int map = 0);
    void beginEventStep(WorldState&) override;
    bool eventScript(WorldState&, const EventScriptContext&) override;
    bool eventConditionAvailable() const override;
    bool eventCondition(WorldState&, const EventScriptContext&, bool& result) override;
    const std::string& eventError() const;
    void setObjects(const ObjectLibrary&);
    void setAnimations(const AnimationLibrary&);
    void collision(WorldState&, std::uint32_t) override;
    void region(WorldState&, int) override;
    void key(WorldState&, const char*) override;
    void stateChange(WorldState&, TimeState, TimeState) override;
    void planAttack(WorldState&) override;
    void beforeSnapshot(WorldState&) override;
    void afterRestore(WorldState&) override;
};
} // namespace echo
