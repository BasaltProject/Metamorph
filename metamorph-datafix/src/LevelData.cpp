#include <basalt/metamorph/datafix/LevelData.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtTag;

enum class Kind : std::uint8_t { Boolean, Inverted, Integer };

struct Rename {
    std::string_view from;
    std::string_view to;
    Kind kind;
    std::int32_t min = INT32_MIN;
    std::int32_t max = INT32_MAX;
};

constexpr std::array kRenames{
    Rename{"allowEnteringNetherUsingPortals", "minecraft:allow_entering_nether_using_portals", Kind::Boolean},
    Rename{"announceAdvancements", "minecraft:show_advancement_messages", Kind::Boolean},
    Rename{"blockExplosionDropDecay", "minecraft:block_explosion_drop_decay", Kind::Boolean},
    Rename{"commandBlockOutput", "minecraft:command_block_output", Kind::Boolean},
    Rename{"enableCommandBlocks", "minecraft:command_blocks_work", Kind::Boolean},
    Rename{"commandBlocksEnabled", "minecraft:command_blocks_work", Kind::Boolean},
    Rename{"commandModificationBlockLimit", "minecraft:max_block_modifications", Kind::Integer, 1},
    Rename{"disableElytraMovementCheck", "minecraft:elytra_movement_check", Kind::Inverted},
    Rename{"disablePlayerMovementCheck", "minecraft:player_movement_check", Kind::Inverted},
    Rename{"disableRaids", "minecraft:raids", Kind::Inverted},
    Rename{"doDaylightCycle", "minecraft:advance_time", Kind::Boolean},
    Rename{"doEntityDrops", "minecraft:entity_drops", Kind::Boolean},
    Rename{"doImmediateRespawn", "minecraft:immediate_respawn", Kind::Boolean},
    Rename{"doInsomnia", "minecraft:spawn_phantoms", Kind::Boolean},
    Rename{"doLimitedCrafting", "minecraft:limited_crafting", Kind::Boolean},
    Rename{"doMobLoot", "minecraft:mob_drops", Kind::Boolean},
    Rename{"doMobSpawning", "minecraft:spawn_mobs", Kind::Boolean},
    Rename{"doPatrolSpawning", "minecraft:spawn_patrols", Kind::Boolean},
    Rename{"doTileDrops", "minecraft:block_drops", Kind::Boolean},
    Rename{"doTraderSpawning", "minecraft:spawn_wandering_traders", Kind::Boolean},
    Rename{"doVinesSpread", "minecraft:spread_vines", Kind::Boolean},
    Rename{"doWardenSpawning", "minecraft:spawn_wardens", Kind::Boolean},
    Rename{"doWeatherCycle", "minecraft:advance_weather", Kind::Boolean},
    Rename{"drowningDamage", "minecraft:drowning_damage", Kind::Boolean},
    Rename{"enderPearlsVanishOnDeath", "minecraft:ender_pearls_vanish_on_death", Kind::Boolean},
    Rename{"fallDamage", "minecraft:fall_damage", Kind::Boolean},
    Rename{"fireDamage", "minecraft:fire_damage", Kind::Boolean},
    Rename{"forgiveDeadPlayers", "minecraft:forgive_dead_players", Kind::Boolean},
    Rename{"freezeDamage", "minecraft:freeze_damage", Kind::Boolean},
    Rename{"globalSoundEvents", "minecraft:global_sound_events", Kind::Boolean},
    Rename{"keepInventory", "minecraft:keep_inventory", Kind::Boolean},
    Rename{"lavaSourceConversion", "minecraft:lava_source_conversion", Kind::Boolean},
    Rename{"locatorBar", "minecraft:locator_bar", Kind::Boolean},
    Rename{"logAdminCommands", "minecraft:log_admin_commands", Kind::Boolean},
    Rename{"maxCommandChainLength", "minecraft:max_command_sequence_length", Kind::Integer, 0},
    Rename{"maxCommandForkCount", "minecraft:max_command_forks", Kind::Integer, 0},
    Rename{"maxEntityCramming", "minecraft:max_entity_cramming", Kind::Integer, 0},
    Rename{"minecartMaxSpeed", "minecraft:max_minecart_speed", Kind::Integer},
    Rename{"mobExplosionDropDecay", "minecraft:mob_explosion_drop_decay", Kind::Boolean},
    Rename{"mobGriefing", "minecraft:mob_griefing", Kind::Boolean},
    Rename{"naturalRegeneration", "minecraft:natural_health_regeneration", Kind::Boolean},
    Rename{"playersNetherPortalCreativeDelay", "minecraft:players_nether_portal_creative_delay", Kind::Integer, 0},
    Rename{"playersNetherPortalDefaultDelay", "minecraft:players_nether_portal_default_delay", Kind::Integer, 0},
    Rename{"playersSleepingPercentage", "minecraft:players_sleeping_percentage", Kind::Integer, 0},
    Rename{"projectilesCanBreakBlocks", "minecraft:projectiles_can_break_blocks", Kind::Boolean},
    Rename{"pvp", "minecraft:pvp", Kind::Boolean},
    Rename{"randomTickSpeed", "minecraft:random_tick_speed", Kind::Integer, 0},
    Rename{"reducedDebugInfo", "minecraft:reduced_debug_info", Kind::Boolean},
    Rename{"sendCommandFeedback", "minecraft:send_command_feedback", Kind::Boolean},
    Rename{"showDeathMessages", "minecraft:show_death_messages", Kind::Boolean},
    Rename{"snowAccumulationHeight", "minecraft:max_snow_accumulation_height", Kind::Integer, 0, 8},
    Rename{"spawnMonsters", "minecraft:spawn_monsters", Kind::Boolean},
    Rename{"spawnRadius", "minecraft:respawn_radius", Kind::Integer},
    Rename{"spawnerBlocksEnabled", "minecraft:spawner_blocks_work", Kind::Boolean},
    Rename{"spectatorsGenerateChunks", "minecraft:spectators_generate_chunks", Kind::Boolean},
    Rename{"tntExplodes", "minecraft:tnt_explodes", Kind::Boolean},
    Rename{"tntExplosionDropDecay", "minecraft:tnt_explosion_drop_decay", Kind::Boolean},
    Rename{"universalAnger", "minecraft:universal_anger", Kind::Boolean},
    Rename{"waterSourceConversion", "minecraft:water_source_conversion", Kind::Boolean},
};

[[nodiscard]] std::string textOf(const NbtTag* tag) {
    const std::string* text = tag == nullptr ? nullptr : tag->get<std::string>();
    return text == nullptr ? std::string{} : *text;
}

[[nodiscard]] bool parseBoolean(const std::string& text) {
    std::string lowered = text;
    std::ranges::transform(lowered, lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered == "true";
}

} // namespace

NbtCompound gameRulesOfLegacy(const NbtCompound& legacy) {
    NbtCompound rules = legacy;
    const bool fireTick = !rules.contains("doFireTick") || parseBoolean(textOf(rules.find("doFireTick")));
    const bool awayFromPlayer = rules.contains("allowFireTicksAwayFromPlayer") &&
                                parseBoolean(textOf(rules.find("allowFireTicksAwayFromPlayer")));
    const std::int32_t fireSpreadRadius = !fireTick ? 0 : (!awayFromPlayer ? 128 : -1);
    for (const std::string_view gone : {"spawnChunkRadius", "entitiesWithPassengersCanUsePortals", "gameLoopFunction",
                                        "doFireTick", "allowFireTicksAwayFromPlayer"}) {
        rules.remove(gone);
    }
    for (const Rename& rename : kRenames) {
        const NbtTag* old = rules.find(rename.from);
        if (old == nullptr) {
            continue;
        }
        const std::string text = textOf(old);
        NbtTag converted = *old;
        if (rename.kind == Kind::Integer) {
            char* end = nullptr;
            const long parsed = std::strtol(text.c_str(), &end, 10);
            if (!text.empty() && end == text.c_str() + text.size()) {
                converted = NbtTag::ofInt(static_cast<std::int32_t>(std::clamp<long>(parsed, rename.min, rename.max)));
            }
        } else {
            const bool value = parseBoolean(text);
            converted = NbtTag::ofBoolean(rename.kind == Kind::Inverted ? !value : value);
        }
        rules.remove(rename.from);
        rules.set(rename.to, std::move(converted));
    }
    if (fireSpreadRadius != 128) {
        rules.set("minecraft:fire_spread_radius_around_player", NbtTag::ofInt(fireSpreadRadius));
    }
    return rules;
}

NbtCompound worldBorderOfLegacy(const NbtCompound& level) {
    const auto number = [&level](std::string_view key, double fallback) {
        const NbtTag* tag = level.find(key);
        if (tag == nullptr) {
            return fallback;
        }
        if (const auto* v = tag->get<double>(); v != nullptr) {
            return *v;
        }
        if (const auto* v = tag->get<float>(); v != nullptr) {
            return static_cast<double>(*v);
        }
        if (const auto* v = tag->get<std::int64_t>(); v != nullptr) {
            return static_cast<double>(*v);
        }
        if (const auto* v = tag->get<std::int32_t>(); v != nullptr) {
            return static_cast<double>(*v);
        }
        return fallback;
    };
    NbtCompound border;
    border.putDouble("center_x", number("BorderCenterX", 0.0));
    border.putDouble("center_z", number("BorderCenterZ", 0.0));
    border.putDouble("size", number("BorderSize", 59999968.0));
    border.putLong("lerp_time", static_cast<std::int64_t>(number("BorderSizeLerpTime", 0.0)));
    border.putDouble("lerp_target", number("BorderSizeLerpTarget", 0.0));
    border.putDouble("safe_zone", number("BorderSafeZone", 5.0));
    border.putDouble("damage_per_block", number("BorderDamagePerBlock", 0.2));
    border.putInt("warning_blocks", static_cast<std::int32_t>(number("BorderWarningBlocks", 5.0)));
    border.putInt("warning_time", static_cast<std::int32_t>(number("BorderWarningTime", 15.0)));
    return border;
}

namespace {

void renameTo(NbtCompound& compound, std::string_view from, std::string_view to);

} // namespace

NbtCompound dragonFightOfLegacy(const NbtCompound& legacy) {
    NbtCompound fight = legacy;
    renameTo(fight, "NeedsStateScanning", "needs_state_scanning");
    renameTo(fight, "DragonKilled", "dragon_killed");
    renameTo(fight, "PreviouslyKilled", "previously_killed");
    renameTo(fight, "Dragon", "dragon_uuid");
    renameTo(fight, "ExitPortalLocation", "exit_portal_location");
    renameTo(fight, "Gateways", "gateways");
    if (fight.getBoolean("IsRespawning")) {
        fight.putString("respawn_stage", "start");
        fight.putInt("respawn_time", 0);
    }
    fight.remove("IsRespawning");
    return fight;
}

namespace {

using wire::nbt::NbtList;

void renameTo(NbtCompound& compound, std::string_view from, std::string_view to) {
    if (const NbtTag* held = compound.find(from); held != nullptr) {
        NbtTag moved = *held;
        compound.remove(from);
        compound.set(to, std::move(moved));
    }
}

// parity: `RandomSequenceSettingsFix`.
void randomSequenceSettings(NbtCompound& data) {
    NbtCompound wrapped;
    wrapped.set("sequences", NbtTag::ofCompound(data));
    data = std::move(wrapped);
}

// parity: `RaidRenamesDataFix`.
void raidRenames(NbtCompound& data) {
    renameTo(data, "Raids", "raids");
    renameTo(data, "Tick", "tick");
    renameTo(data, "NextAvailableID", "next_id");
    NbtTag* held = data.find("raids");
    NbtList* raids = held == nullptr ? nullptr : held->get<NbtList>();
    if (raids == nullptr) {
        return;
    }
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 12> kFields{{
        {"Id", "id"},
        {"Started", "started"},
        {"Active", "active"},
        {"TicksActive", "ticks_active"},
        {"BadOmenLevel", "raid_omen_level"},
        {"GroupsSpawned", "groups_spawned"},
        {"PreRaidTicks", "cooldown_ticks"},
        {"PostRaidTicks", "post_raid_ticks"},
        {"TotalHealth", "total_health"},
        {"NumGroups", "group_count"},
        {"Status", "status"},
        {"HeroesOfTheVillage", "heroes_of_the_village"},
    }};
    for (std::size_t i = 0; i < raids->size(); ++i) {
        NbtCompound* raid = (*raids)[i].get<NbtCompound>();
        if (raid == nullptr) {
            continue;
        }
        const auto whole = [raid](std::string_view key) -> std::optional<std::int32_t> {
            const NbtTag* tag = raid->find(key);
            if (tag == nullptr) {
                return std::nullopt;
            }
            if (const auto* v = tag->get<std::int32_t>(); v != nullptr) {
                return *v;
            }
            if (const auto* v = tag->get<std::int8_t>(); v != nullptr) {
                return *v;
            }
            if (const auto* v = tag->get<std::int16_t>(); v != nullptr) {
                return *v;
            }
            if (const auto* v = tag->get<float>(); v != nullptr) {
                return static_cast<std::int32_t>(*v);
            }
            if (const auto* v = tag->get<double>(); v != nullptr) {
                return static_cast<std::int32_t>(*v);
            }
            return std::nullopt;
        };
        const auto x = whole("CX");
        const auto y = whole("CY");
        const auto z = whole("CZ");
        if (x.has_value() && y.has_value() && z.has_value()) {
            raid->remove("CX");
            raid->remove("CY");
            raid->remove("CZ");
            raid->putIntArray("center", {*x, *y, *z});
        }
        for (const auto& [from, to] : kFields) {
            renameTo(*raid, from, to);
        }
    }
}

// parity: `ScoreboardDisplaySlotFix`.
void scoreboardDisplaySlots(NbtCompound& data) {
    static constexpr std::array<std::string_view, 19> kSlots{
        "list",
        "sidebar",
        "below_name",
        "sidebar.team.black",
        "sidebar.team.dark_blue",
        "sidebar.team.dark_green",
        "sidebar.team.dark_aqua",
        "sidebar.team.dark_red",
        "sidebar.team.dark_purple",
        "sidebar.team.gold",
        "sidebar.team.gray",
        "sidebar.team.dark_gray",
        "sidebar.team.blue",
        "sidebar.team.green",
        "sidebar.team.aqua",
        "sidebar.team.red",
        "sidebar.team.light_purple",
        "sidebar.team.yellow",
        "sidebar.team.white",
    };
    NbtTag* held = data.find("DisplaySlots");
    NbtCompound* slots = held == nullptr ? nullptr : held->get<NbtCompound>();
    if (slots == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        renameTo(*slots, "slot_" + std::to_string(i), kSlots[i]);
    }
}

} // namespace

void upgradeSavedData(std::string_view name, NbtCompound& data, std::int32_t fromVersion) {
    if (name == "random_sequences" && fromVersion < 3565) {
        randomSequenceSettings(data);
    } else if (name == "raids" && fromVersion < 4309) {
        raidRenames(data);
    } else if (name == "scoreboard" && fromVersion < 3566) {
        scoreboardDisplaySlots(data);
    }
}

} // namespace basalt::metamorph::datafix
