#include <basalt/metamorph/datafix/LevelData.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

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

} // namespace basalt::metamorph::datafix
