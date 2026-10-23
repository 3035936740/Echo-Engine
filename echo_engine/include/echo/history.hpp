#pragma once
#include "world.hpp"
#include <memory>
namespace echo {
// Diagnostic metadata never drives gameplay and is deliberately outside WorldState.
enum class HistoryClearReason : std::uint8_t {
    Startup,
    Manual,
    Reset,
    Restart,
    EventDefinitions,
    Enabled,
    Disabled,
    ScriptEnabled,
    ScriptDisabled,
    ProjectLoad,
    Play,
    FocusSettings,
    MapSettings,
    ResourceReload,
    ScriptReload,
    Recovery,
    CollisionSettings,
    MobilitySettings,
    DatabaseDefinitions
};
struct HistoryDiagnostics {
    bool enabled{}, budgetLimited{}, building{}, rewinding{}, recordingPaused{}, windowReduced{};
    double requestedSeconds{}, retainedSeconds{}, availableSeconds{}, nominalCapacitySeconds{};
    std::size_t usedBytes{}, reservedBytes{}, budgetBytes{};
    int frames{}, recordHz{};
    HistoryClearReason lastClear = HistoryClearReason::Startup;
    std::uint64_t clears{}, budgetEvictions{}, windowEvictions{}, futureDiscarded{};
};
class History {
    std::unique_ptr<WorldState[]> slots_;
    std::size_t capacity_{}, head_{}, count_{}, budget_;
    HistoryClearReason lastClear_ = HistoryClearReason::Startup;
    std::uint64_t clears_{}, budgetEvictions_{}, windowEvictions_{}, futureDiscarded_{};
    double lastWindow_{};
    bool windowReduced_{};

  public:
    explicit History(std::size_t budget = 512ull * 1024 * 1024) : budget_(budget) {}
    void prepare(bool enabled); // startup/control boundary ONLY, never record()
    void clear(HistoryClearReason reason = HistoryClearReason::Manual) noexcept;
    void record(const WorldState&);
    void replace(int, const WorldState&);
    void truncateToCursor(int);
    const WorldState& at(int) const;
    int cursorAtTime(double) const;
    int size() const {
        return static_cast<int>(count_);
    }
    std::size_t capacity() const {
        return capacity_;
    }
    std::size_t allocatedBytes() const {
        return capacity_ * sizeof(WorldState);
    }
    std::size_t usedBytes() const {
        return count_ * sizeof(WorldState);
    }
    std::size_t budgetBytes() const {
        return budget_;
    }
    double seconds() const {
        return count_ > 1 ? at(size() - 1).clock - at(0).clock : 0;
    }
    HistoryDiagnostics diagnostics(const WorldState&) const noexcept;
};
} // namespace echo
