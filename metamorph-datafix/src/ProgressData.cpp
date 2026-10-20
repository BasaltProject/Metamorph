#include "FlatteningTables.hpp"
#include "LegacyStates.hpp"
#include "ProgressTables.hpp"

#include <basalt/metamorph/datafix/ProgressData.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <set>

namespace basalt::metamorph::datafix {
namespace {

constexpr std::int32_t kSwimRenameVersion = 1510;
constexpr std::int32_t kPlayTimeVersion = 2710;
constexpr std::int32_t kAdvancementsVersion = 1501;
constexpr std::int32_t kSugarVersion = 2100;
constexpr std::int32_t kComposterVersion = 2503;
constexpr std::int32_t kCavesAdvancementsVersion = 2846;
constexpr std::int32_t kCatVariantsVersion = 3086;
constexpr std::int32_t kLodestoneVersion = 4311;
constexpr std::int32_t kStonecutterVersion = 4769;

struct StatType {
    std::string type;
    std::string key;
};

template <typename Table>
[[nodiscard]] std::string_view lookup(const Table& table, std::string_view from) {
    const auto found = std::ranges::find_if(table, [&](const auto& pair) { return pair.from == from; });
    return found == table.end() ? std::string_view{} : found->to;
}

[[nodiscard]] std::string dotted(std::string_view key) {
    std::string text{key};
    std::ranges::replace(text, '.', ':');
    return text;
}

[[nodiscard]] std::string upgradedItem(const std::string& name) {
    const std::string_view flat = lookup(flattening::kItems, name + ".0");
    return flat.empty() ? name : std::string{flat};
}

// parity: `StatsCounterFix.unpackLegacyKey`.
[[nodiscard]] std::optional<StatType> unpackLegacyKey(const std::string& key) {
    static const std::set<std::string, std::less<>> kSkipped{
        "stat.craftItem.minecraft.spawn_egg", "stat.useItem.minecraft.spawn_egg", "stat.breakItem.minecraft.spawn_egg",
        "stat.pickup.minecraft.spawn_egg", "stat.drop.minecraft.spawn_egg"};
    if (kSkipped.contains(key)) {
        return std::nullopt;
    }
    if (const std::string_view custom = lookup(progress::kCustomStats, key); !custom.empty()) {
        return StatType{"minecraft:custom", std::string{custom}};
    }
    std::size_t split = key.find('.');
    split = split == std::string::npos ? split : key.find('.', split + 1);
    if (split == std::string::npos) {
        return std::nullopt;
    }
    const std::string prefix = key.substr(0, split);
    const std::string rest = dotted(std::string_view{key}.substr(split + 1));
    if (prefix == "stat.mineBlock") {
        const std::optional<std::string> block = legacy::upgradeBlockName(rest);
        return StatType{"minecraft:mined", block.value_or(rest)};
    }
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> kItemTypes{{
        {"stat.craftItem", "minecraft:crafted"},
        {"stat.useItem", "minecraft:used"},
        {"stat.breakItem", "minecraft:broken"},
        {"stat.pickup", "minecraft:picked_up"},
        {"stat.drop", "minecraft:dropped"},
    }};
    for (const auto& [legacyPrefix, type] : kItemTypes) {
        if (prefix == legacyPrefix) {
            return StatType{std::string{type}, upgradedItem(rest)};
        }
    }
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 2> kEntityTypes{{
        {"stat.entityKilledBy", "minecraft:killed_by"},
        {"stat.killEntity", "minecraft:killed"},
    }};
    for (const auto& [legacyPrefix, type] : kEntityTypes) {
        if (prefix == legacyPrefix) {
            const std::string_view renamed = lookup(progress::kStatEntities, rest);
            return StatType{std::string{type}, renamed.empty() ? rest : std::string{renamed}};
        }
    }
    return std::nullopt;
}

void renameCustom(StatCounters& stats, std::string_view from, std::string_view to) {
    const auto custom = stats.find("minecraft:custom");
    if (custom == stats.end()) {
        return;
    }
    const auto found = custom->second.find(std::string{from});
    if (found == custom->second.end()) {
        return;
    }
    const std::int64_t value = found->second;
    custom->second.erase(found);
    custom->second[std::string{to}] = value;
}

} // namespace

StatCounters statsOfLegacy(const std::map<std::string, std::int64_t>& flat) {
    StatCounters stats;
    for (const auto& [key, value] : flat) {
        if (const std::optional<StatType> type = unpackLegacyKey(key); type.has_value()) {
            stats[type->type][type->key] = value;
        }
    }
    return stats;
}

void upgradeStats(StatCounters& stats, const std::int32_t fromVersion) {
    if (fromVersion < kSwimRenameVersion) {
        renameCustom(stats, "minecraft:swim_one_cm", "minecraft:walk_on_water_one_cm");
        renameCustom(stats, "minecraft:dive_one_cm", "minecraft:walk_under_water_one_cm");
    }
    if (fromVersion < kPlayTimeVersion) {
        renameCustom(stats, "minecraft:play_one_minute", "minecraft:play_time");
    }
}

std::string advancementRenamed(std::string_view id, const std::int32_t fromVersion) {
    std::string name{id};
    const auto rename = [&](const std::int32_t version, std::string_view from, std::string_view to) {
        if (fromVersion < version && name == from) {
            name = std::string{to};
        }
    };
    if (fromVersion < kAdvancementsVersion) {
        if (const std::string_view renamed = lookup(progress::kAdvancementRenames, name); !renamed.empty()) {
            name = std::string{renamed};
        }
    }
    rename(kSugarVersion, "minecraft:recipes/misc/sugar", "minecraft:recipes/misc/sugar_from_sugar_cane");
    rename(kComposterVersion, "minecraft:recipes/misc/composter", "minecraft:recipes/decorations/composter");
    rename(kCavesAdvancementsVersion, "minecraft:husbandry/play_jukebox_in_meadows",
           "minecraft:adventure/play_jukebox_in_meadows");
    rename(kCavesAdvancementsVersion, "minecraft:adventure/caves_and_cliff",
           "minecraft:adventure/fall_from_world_height");
    rename(kCavesAdvancementsVersion, "minecraft:adventure/ride_strider_in_overworld_lava",
           "minecraft:nether/ride_strider_in_overworld_lava");
    rename(kLodestoneVersion, "minecraft:nether/use_lodestone", "minecraft:adventure/use_lodestone");
    if (fromVersion < kStonecutterVersion) {
        if (const std::string_view renamed = lookup(progress::kStonecutterAdvancements, name); !renamed.empty()) {
            name = std::string{renamed};
        }
    }
    return name;
}

std::string criterionRenamed(std::string_view advancement, std::string_view criterion, const std::int32_t fromVersion) {
    if (fromVersion < kCatVariantsVersion && advancement == "minecraft:husbandry/complete_catalogue") {
        if (const std::string_view renamed = lookup(progress::kCatVariants, criterion); !renamed.empty()) {
            return std::string{renamed};
        }
    }
    return std::string{criterion};
}

} // namespace basalt::metamorph::datafix
