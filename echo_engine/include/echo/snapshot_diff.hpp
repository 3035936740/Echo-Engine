#pragma once
#include "world.hpp"
#include <string_view>
namespace echo {
inline constexpr std::size_t maxSnapshotDifferences = 512;
struct SnapshotDifference {
    Text<192> field;
    Text<80> before, after;
};
struct SnapshotDiff {
    FixedList<SnapshotDifference, maxSnapshotDifferences> entries;
    std::uint64_t inspected{}, differences{}, matching{};
    bool pathsTruncated{};
    bool equal() const { return differences == 0; }
    bool truncated() const { return matching > entries.count || pathsTruncated; }
};
// All named fields, including inactive fixed slots and text tails. Padding is ignored.
// Floating values compare their exact bits (+0/-0 and NaN payloads are visible).
// Read-only; caller owns a bounded output buffer. No heap allocation during scanning.
void compareSnapshots(const WorldState&, const WorldState&, SnapshotDiff&,
                      std::string_view fieldFilter = {}) noexcept;
} // namespace echo
