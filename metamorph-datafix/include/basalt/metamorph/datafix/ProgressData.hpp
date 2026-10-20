#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace basalt::metamorph::datafix {

using StatCounters = std::map<std::string, std::map<std::string, std::int64_t>>;

/// parity: `StatsCounterFix`, the flat `stat.*` counters of a pre-1.13 statistics file as typed counters.
[[nodiscard]] StatCounters statsOfLegacy(const std::map<std::string, std::int64_t>& flat);

/// parity: `StatsRenameFix` for the swim and play time renames, from `fromVersion` on.
void upgradeStats(StatCounters& stats, std::int32_t fromVersion);

/// parity: the advancement renames of `AdvancementsFix` and the later `AdvancementsRenameFix` steps.
[[nodiscard]] std::string advancementRenamed(std::string_view id, std::int32_t fromVersion);

/// parity: `CriteriaRenameFix` for the cat variants of the catalogue advancement.
[[nodiscard]] std::string criterionRenamed(std::string_view advancement, std::string_view criterion,
                                           std::int32_t fromVersion);

} // namespace basalt::metamorph::datafix
