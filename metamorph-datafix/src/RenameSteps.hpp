// Generated from the vanilla sources by scripts/extract-renames.py. Do not edit.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace basalt::metamorph::datafix::renames {

struct Pair {
    std::string_view from;
    std::string_view to;
};

// Flowing fixer
inline const std::array<Pair, 2> kStep0{{
    {"minecraft:flowing_water", "minecraft:water"},
    {"minecraft:flowing_lava", "minecraft:lava"},
}};

// Rename coral blocks
inline const std::array<Pair, 20> kStep1{{
    {"minecraft:blue_coral", "minecraft:tube_coral_block"},
    {"minecraft:pink_coral", "minecraft:brain_coral_block"},
    {"minecraft:purple_coral", "minecraft:bubble_coral_block"},
    {"minecraft:red_coral", "minecraft:fire_coral_block"},
    {"minecraft:yellow_coral", "minecraft:horn_coral_block"},
    {"minecraft:blue_coral_plant", "minecraft:tube_coral"},
    {"minecraft:pink_coral_plant", "minecraft:brain_coral"},
    {"minecraft:purple_coral_plant", "minecraft:bubble_coral"},
    {"minecraft:red_coral_plant", "minecraft:fire_coral"},
    {"minecraft:yellow_coral_plant", "minecraft:horn_coral"},
    {"minecraft:blue_coral_fan", "minecraft:tube_coral_fan"},
    {"minecraft:pink_coral_fan", "minecraft:brain_coral_fan"},
    {"minecraft:purple_coral_fan", "minecraft:bubble_coral_fan"},
    {"minecraft:red_coral_fan", "minecraft:fire_coral_fan"},
    {"minecraft:yellow_coral_fan", "minecraft:horn_coral_fan"},
    {"minecraft:blue_dead_coral", "minecraft:dead_tube_coral"},
    {"minecraft:pink_dead_coral", "minecraft:dead_brain_coral"},
    {"minecraft:purple_dead_coral", "minecraft:dead_bubble_coral"},
    {"minecraft:red_dead_coral", "minecraft:dead_fire_coral"},
    {"minecraft:yellow_dead_coral", "minecraft:dead_horn_coral"},
}};

// Rename coral items
inline const std::array<Pair, 20> kStep2{{
    {"minecraft:blue_coral", "minecraft:tube_coral_block"},
    {"minecraft:pink_coral", "minecraft:brain_coral_block"},
    {"minecraft:purple_coral", "minecraft:bubble_coral_block"},
    {"minecraft:red_coral", "minecraft:fire_coral_block"},
    {"minecraft:yellow_coral", "minecraft:horn_coral_block"},
    {"minecraft:blue_coral_plant", "minecraft:tube_coral"},
    {"minecraft:pink_coral_plant", "minecraft:brain_coral"},
    {"minecraft:purple_coral_plant", "minecraft:bubble_coral"},
    {"minecraft:red_coral_plant", "minecraft:fire_coral"},
    {"minecraft:yellow_coral_plant", "minecraft:horn_coral"},
    {"minecraft:blue_coral_fan", "minecraft:tube_coral_fan"},
    {"minecraft:pink_coral_fan", "minecraft:brain_coral_fan"},
    {"minecraft:purple_coral_fan", "minecraft:bubble_coral_fan"},
    {"minecraft:red_coral_fan", "minecraft:fire_coral_fan"},
    {"minecraft:yellow_coral_fan", "minecraft:horn_coral_fan"},
    {"minecraft:blue_dead_coral", "minecraft:dead_tube_coral"},
    {"minecraft:pink_dead_coral", "minecraft:dead_brain_coral"},
    {"minecraft:purple_dead_coral", "minecraft:dead_bubble_coral"},
    {"minecraft:red_dead_coral", "minecraft:dead_fire_coral"},
    {"minecraft:yellow_dead_coral", "minecraft:dead_horn_coral"},
}};

// Rename pufferfish egg item
inline const std::array<Pair, 1> kStep3{{
    {"minecraft:puffer_fish_spawn_egg", "minecraft:pufferfish_spawn_egg"},
}};

// Rename seagrass items
inline const std::array<Pair, 2> kStep4{{
    {"minecraft:sea_grass", "minecraft:seagrass"},
    {"minecraft:tall_sea_grass", "minecraft:tall_seagrass"},
}};

// Rename seagrass blocks
inline const std::array<Pair, 2> kStep5{{
    {"minecraft:sea_grass", "minecraft:seagrass"},
    {"minecraft:tall_sea_grass", "minecraft:tall_seagrass"},
}};

// Rename cod/salmon egg items
inline const std::array<Pair, 2> kStep6{{
    {"minecraft:salmon_mob_spawn_egg", "minecraft:salmon_spawn_egg"},
    {"minecraft:cod_mob_spawn_egg", "minecraft:cod_spawn_egg"},
}};

// Rename prismarine_brick(s)_* blocks
inline const std::array<Pair, 2> kStep7{{
    {"minecraft:prismarine_bricks_slab", "minecraft:prismarine_brick_slab"},
    {"minecraft:prismarine_bricks_stairs", "minecraft:prismarine_brick_stairs"},
}};

// Rename prismarine_brick(s)_* items
inline const std::array<Pair, 2> kStep8{{
    {"minecraft:prismarine_bricks_slab", "minecraft:prismarine_brick_slab"},
    {"minecraft:prismarine_bricks_stairs", "minecraft:prismarine_brick_stairs"},
}};

// Rename kelp/kelptop
inline const std::array<Pair, 2> kStep9{{
    {"minecraft:kelp_top", "minecraft:kelp"},
    {"minecraft:kelp", "minecraft:kelp_plant"},
}};

// Rename kelptop
inline const std::array<Pair, 1> kStep10{{
    {"minecraft:kelp_top", "minecraft:kelp"},
}};

// Rename melon_block
inline const std::array<Pair, 1> kStep11{{
    {"minecraft:melon_block", "minecraft:melon"},
}};

// Rename melon_block/melon/speckled_melon
inline const std::array<Pair, 3> kStep12{{
    {"minecraft:melon_block", "minecraft:melon"},
    {"minecraft:melon", "minecraft:melon_slice"},
    {"minecraft:speckled_melon", "minecraft:glistering_melon_slice"},
}};

// Block renamening fix
inline const std::array<Pair, 14> kStep13{{
    {"minecraft:portal", "minecraft:nether_portal"},
    {"minecraft:oak_bark", "minecraft:oak_wood"},
    {"minecraft:spruce_bark", "minecraft:spruce_wood"},
    {"minecraft:birch_bark", "minecraft:birch_wood"},
    {"minecraft:jungle_bark", "minecraft:jungle_wood"},
    {"minecraft:acacia_bark", "minecraft:acacia_wood"},
    {"minecraft:dark_oak_bark", "minecraft:dark_oak_wood"},
    {"minecraft:stripped_oak_bark", "minecraft:stripped_oak_wood"},
    {"minecraft:stripped_spruce_bark", "minecraft:stripped_spruce_wood"},
    {"minecraft:stripped_birch_bark", "minecraft:stripped_birch_wood"},
    {"minecraft:stripped_jungle_bark", "minecraft:stripped_jungle_wood"},
    {"minecraft:stripped_acacia_bark", "minecraft:stripped_acacia_wood"},
    {"minecraft:stripped_dark_oak_bark", "minecraft:stripped_dark_oak_wood"},
    {"minecraft:mob_spawner", "minecraft:spawner"},
}};

// Item renamening fix
inline const std::array<Pair, 0> kStep14{{}};

// Rename coral fan blocks
inline const std::array<Pair, 5> kStep15{{
    {"minecraft:tube_coral_fan", "minecraft:tube_coral_wall_fan"},
    {"minecraft:brain_coral_fan", "minecraft:brain_coral_wall_fan"},
    {"minecraft:bubble_coral_fan", "minecraft:bubble_coral_wall_fan"},
    {"minecraft:fire_coral_fan", "minecraft:fire_coral_wall_fan"},
    {"minecraft:horn_coral_fan", "minecraft:horn_coral_wall_fan"},
}};

// Rename dye items
inline const std::array<Pair, 3> kStep16{{
    {"minecraft:cactus_green", "minecraft:green_dye"},
    {"minecraft:rose_red", "minecraft:red_dye"},
    {"minecraft:dandelion_yellow", "minecraft:yellow_dye"},
}};

// Rename sign blocks & stone slabs
inline const std::array<Pair, 3> kStep17{{
    {"minecraft:stone_slab", "minecraft:smooth_stone_slab"},
    {"minecraft:sign", "minecraft:oak_sign"},
    {"minecraft:wall_sign", "minecraft:oak_wall_sign"},
}};

// Rename sign item & stone slabs
inline const std::array<Pair, 2> kStep18{{
    {"minecraft:stone_slab", "minecraft:smooth_stone_slab"},
    {"minecraft:sign", "minecraft:oak_sign"},
}};

// Rename ravager egg item
inline const std::array<Pair, 1> kStep19{{
    {"minecraft:illager_beast_spawn_egg", "minecraft:ravager_spawn_egg"},
}};

// Rename bee_hive item to beehive
inline const std::array<Pair, 1> kStep20{{
    {"minecraft:bee_hive", "minecraft:beehive"},
}};

// Rename bee_hive block to beehive
inline const std::array<Pair, 1> kStep21{{
    {"minecraft:bee_hive", "minecraft:beehive"},
}};

struct Step {
    std::int32_t version;
    bool block;
    const Pair* pairs;
    std::size_t count;
};

inline const std::array<Step, 22> kSteps{{
    {1475, true, kStep0.data(), kStep0.size()},    {1480, true, kStep1.data(), kStep1.size()},
    {1480, false, kStep2.data(), kStep2.size()},   {1483, false, kStep3.data(), kStep3.size()},
    {1484, false, kStep4.data(), kStep4.size()},   {1484, true, kStep5.data(), kStep5.size()},
    {1486, false, kStep6.data(), kStep6.size()},   {1487, false, kStep7.data(), kStep7.size()},
    {1487, true, kStep8.data(), kStep8.size()},    {1488, true, kStep9.data(), kStep9.size()},
    {1488, false, kStep10.data(), kStep10.size()}, {1490, true, kStep11.data(), kStep11.size()},
    {1490, false, kStep12.data(), kStep12.size()}, {1510, true, kStep13.data(), kStep13.size()},
    {1510, false, kStep14.data(), kStep14.size()}, {1515, true, kStep15.data(), kStep15.size()},
    {1800, false, kStep16.data(), kStep16.size()}, {1802, true, kStep17.data(), kStep17.size()},
    {1802, false, kStep18.data(), kStep18.size()}, {1928, false, kStep19.data(), kStep19.size()},
    {2209, false, kStep20.data(), kStep20.size()}, {2209, true, kStep21.data(), kStep21.size()},
}};

} // namespace basalt::metamorph::datafix::renames
