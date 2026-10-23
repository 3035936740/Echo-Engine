#include "echo/history.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace echo {
void History::clear(HistoryClearReason reason) noexcept {
    head_ = count_ = 0;
    lastClear_ = reason;
    ++clears_;
    budgetEvictions_ = windowEvictions_ = futureDiscarded_ = 0;
    lastWindow_ = 0;
    windowReduced_ = false;
}
HistoryDiagnostics History::diagnostics(const WorldState& w) const noexcept {
    HistoryDiagnostics d;
    d.enabled = w.rewind.enabled;
    d.requestedSeconds = std::clamp(double(w.rewind.history_seconds), 1., 30.);
    d.retainedSeconds = seconds();
    d.availableSeconds = count_ ? std::clamp(w.clock - at(0).clock, 0., d.requestedSeconds) : 0;
    d.recordHz = std::max(1, w.rewind.record_hz);
    d.nominalCapacitySeconds = capacity_ > 1 ? double(capacity_ - 1) / d.recordHz : 0;
    d.usedBytes = usedBytes();
    d.reservedBytes = allocatedBytes();
    d.budgetBytes = budget_;
    d.frames = size();
    d.budgetLimited = d.enabled && std::ceil(d.requestedSeconds * d.recordHz) + 1 > capacity_;
    d.building = d.enabled && d.retainedSeconds + 1. / d.recordHz + 1e-6 <
                                  std::min(d.requestedSeconds, d.nominalCapacitySeconds);
    d.rewinding = w.state == TimeState::Fast || w.state == TimeState::Trace;
    d.recordingPaused = !d.rewinding && w.dialogue.active < 0 &&
                        w.transition.phase == TransitionPhase::Idle &&
                        (w.gameplayClockPaused || w.state == TimeState::Hit ||
                         w.state == TimeState::Suppressed || w.state == TimeState::Escape);
    d.lastClear = lastClear_;
    d.clears = clears_;
    d.budgetEvictions = budgetEvictions_;
    d.windowEvictions = windowEvictions_;
    d.futureDiscarded = futureDiscarded_;
    d.windowReduced = windowReduced_;
    return d;
}
void History::prepare(bool enabled) {
    if (!enabled || slots_)
        return;
    capacity_ = std::min<std::size_t>(3601, budget_ / sizeof(WorldState));
    if (capacity_ < 2)
        throw std::runtime_error("History budget cannot hold two full snapshots");
    slots_ = std::make_unique<WorldState[]>(capacity_);
}
const WorldState& History::at(int c) const {
    if (c < 0 || c >= size())
        throw std::out_of_range("history cursor");
    return slots_[(head_ + c) % capacity_];
}
void History::record(const WorldState& w) {
    if (!w.rewind.enabled)
        return;
    if (!slots_)
        throw std::logic_error("prepare history before record");
    if (count_ && w.clock < at(size() - 1).clock)
        throw std::logic_error("truncate old future before recording");
    if (count_ == capacity_) {
        ++budgetEvictions_;
        head_ = (head_ + 1) % capacity_;
        --count_;
    }
    slots_[(head_ + count_) % capacity_] = w;
    ++count_;
    double window = std::clamp(w.rewind.history_seconds, 1.0f, 30.0f);
    if (lastWindow_ > 0 && window < lastWindow_ - 1e-6)
        windowReduced_ = true;
    lastWindow_ = window;
    double cutoff = w.clock - window;
    while (count_ > 1 && at(1).clock <= cutoff + 1e-7) {
        ++windowEvictions_;
        head_ = (head_ + 1) % capacity_;
        --count_;
    }
}
void History::truncateToCursor(int c) {
    if (c < 0 || c >= size())
        throw std::out_of_range("truncate");
    futureDiscarded_ += count_ - std::size_t(c + 1);
    count_ = c + 1;
}
void History::replace(int c, const WorldState& w) {
    if (c < 0 || c >= size())
        throw std::out_of_range("replace");
    slots_[(head_ + c) % capacity_] = w;
}
int History::cursorAtTime(double t) const {
    if (!count_)
        return -1;
    int lo = 0, hi = size() - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (at(mid).clock <= t + 1e-7)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}
} // namespace echo
