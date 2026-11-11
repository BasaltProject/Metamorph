// Generated from the vanilla sources. Do not edit.
#pragma once

#include <array>
#include <string_view>

namespace basalt::metamorph::datafix::structures {

struct TemplateRename {
    std::string_view from;
    std::string_view to;
};

struct PieceRenames {
    std::string_view structure;
    std::string_view piece;
    const TemplateRename* renames;
    std::size_t count;
};

inline constexpr TemplateRename kEndCityRenames[]{
    {"second_floor", "second_floor_1"},
    {"third_floor", "third_floor_1"},
    {"third_floor_c", "third_floor_2"},
};

inline constexpr TemplateRename kMansionRenames[]{
    {"carpet_south", "carpet_south_1"},
    {"carpet_west", "carpet_west_1"},
    {"indoors_door", "indoors_door_1"},
    {"indoors_wall", "indoors_wall_1"},
};

inline constexpr TemplateRename kIglooRenames[]{
    {"minecraft:igloo/igloo_bottom", "minecraft:igloo/bottom"},
    {"minecraft:igloo/igloo_middle", "minecraft:igloo/middle"},
    {"minecraft:igloo/igloo_top", "minecraft:igloo/top"},
};

inline constexpr TemplateRename kOceanRuinRenames[]{
    {"minecraft:ruin/big_ruin1_brick", "minecraft:underwater_ruin/big_brick_1"},
    {"minecraft:ruin/big_ruin2_brick", "minecraft:underwater_ruin/big_brick_2"},
    {"minecraft:ruin/big_ruin3_brick", "minecraft:underwater_ruin/big_brick_3"},
    {"minecraft:ruin/big_ruin8_brick", "minecraft:underwater_ruin/big_brick_8"},
    {"minecraft:ruin/big_ruin1_cracked", "minecraft:underwater_ruin/big_cracked_1"},
    {"minecraft:ruin/big_ruin2_cracked", "minecraft:underwater_ruin/big_cracked_2"},
    {"minecraft:ruin/big_ruin3_cracked", "minecraft:underwater_ruin/big_cracked_3"},
    {"minecraft:ruin/big_ruin8_cracked", "minecraft:underwater_ruin/big_cracked_8"},
    {"minecraft:ruin/big_ruin1_mossy", "minecraft:underwater_ruin/big_mossy_1"},
    {"minecraft:ruin/big_ruin2_mossy", "minecraft:underwater_ruin/big_mossy_2"},
    {"minecraft:ruin/big_ruin3_mossy", "minecraft:underwater_ruin/big_mossy_3"},
    {"minecraft:ruin/big_ruin8_mossy", "minecraft:underwater_ruin/big_mossy_8"},
    {"minecraft:ruin/big_ruin_warm4", "minecraft:underwater_ruin/big_warm_4"},
    {"minecraft:ruin/big_ruin_warm5", "minecraft:underwater_ruin/big_warm_5"},
    {"minecraft:ruin/big_ruin_warm6", "minecraft:underwater_ruin/big_warm_6"},
    {"minecraft:ruin/big_ruin_warm7", "minecraft:underwater_ruin/big_warm_7"},
    {"minecraft:ruin/ruin1_brick", "minecraft:underwater_ruin/brick_1"},
    {"minecraft:ruin/ruin2_brick", "minecraft:underwater_ruin/brick_2"},
    {"minecraft:ruin/ruin3_brick", "minecraft:underwater_ruin/brick_3"},
    {"minecraft:ruin/ruin4_brick", "minecraft:underwater_ruin/brick_4"},
    {"minecraft:ruin/ruin5_brick", "minecraft:underwater_ruin/brick_5"},
    {"minecraft:ruin/ruin6_brick", "minecraft:underwater_ruin/brick_6"},
    {"minecraft:ruin/ruin7_brick", "minecraft:underwater_ruin/brick_7"},
    {"minecraft:ruin/ruin8_brick", "minecraft:underwater_ruin/brick_8"},
    {"minecraft:ruin/ruin1_cracked", "minecraft:underwater_ruin/cracked_1"},
    {"minecraft:ruin/ruin2_cracked", "minecraft:underwater_ruin/cracked_2"},
    {"minecraft:ruin/ruin3_cracked", "minecraft:underwater_ruin/cracked_3"},
    {"minecraft:ruin/ruin4_cracked", "minecraft:underwater_ruin/cracked_4"},
    {"minecraft:ruin/ruin5_cracked", "minecraft:underwater_ruin/cracked_5"},
    {"minecraft:ruin/ruin6_cracked", "minecraft:underwater_ruin/cracked_6"},
    {"minecraft:ruin/ruin7_cracked", "minecraft:underwater_ruin/cracked_7"},
    {"minecraft:ruin/ruin8_cracked", "minecraft:underwater_ruin/cracked_8"},
    {"minecraft:ruin/ruin1_mossy", "minecraft:underwater_ruin/mossy_1"},
    {"minecraft:ruin/ruin2_mossy", "minecraft:underwater_ruin/mossy_2"},
    {"minecraft:ruin/ruin3_mossy", "minecraft:underwater_ruin/mossy_3"},
    {"minecraft:ruin/ruin4_mossy", "minecraft:underwater_ruin/mossy_4"},
    {"minecraft:ruin/ruin5_mossy", "minecraft:underwater_ruin/mossy_5"},
    {"minecraft:ruin/ruin6_mossy", "minecraft:underwater_ruin/mossy_6"},
    {"minecraft:ruin/ruin7_mossy", "minecraft:underwater_ruin/mossy_7"},
    {"minecraft:ruin/ruin8_mossy", "minecraft:underwater_ruin/mossy_8"},
    {"minecraft:ruin/ruin_warm1", "minecraft:underwater_ruin/warm_1"},
    {"minecraft:ruin/ruin_warm2", "minecraft:underwater_ruin/warm_2"},
    {"minecraft:ruin/ruin_warm3", "minecraft:underwater_ruin/warm_3"},
    {"minecraft:ruin/ruin_warm4", "minecraft:underwater_ruin/warm_4"},
    {"minecraft:ruin/ruin_warm5", "minecraft:underwater_ruin/warm_5"},
    {"minecraft:ruin/ruin_warm6", "minecraft:underwater_ruin/warm_6"},
    {"minecraft:ruin/ruin_warm7", "minecraft:underwater_ruin/warm_7"},
    {"minecraft:ruin/ruin_warm8", "minecraft:underwater_ruin/warm_8"},
    {"minecraft:ruin/big_brick_1", "minecraft:underwater_ruin/big_brick_1"},
    {"minecraft:ruin/big_brick_2", "minecraft:underwater_ruin/big_brick_2"},
    {"minecraft:ruin/big_brick_3", "minecraft:underwater_ruin/big_brick_3"},
    {"minecraft:ruin/big_brick_8", "minecraft:underwater_ruin/big_brick_8"},
    {"minecraft:ruin/big_mossy_1", "minecraft:underwater_ruin/big_mossy_1"},
    {"minecraft:ruin/big_mossy_2", "minecraft:underwater_ruin/big_mossy_2"},
    {"minecraft:ruin/big_mossy_3", "minecraft:underwater_ruin/big_mossy_3"},
    {"minecraft:ruin/big_mossy_8", "minecraft:underwater_ruin/big_mossy_8"},
    {"minecraft:ruin/big_cracked_1", "minecraft:underwater_ruin/big_cracked_1"},
    {"minecraft:ruin/big_cracked_2", "minecraft:underwater_ruin/big_cracked_2"},
    {"minecraft:ruin/big_cracked_3", "minecraft:underwater_ruin/big_cracked_3"},
    {"minecraft:ruin/big_cracked_8", "minecraft:underwater_ruin/big_cracked_8"},
    {"minecraft:ruin/big_warm_4", "minecraft:underwater_ruin/big_warm_4"},
    {"minecraft:ruin/big_warm_5", "minecraft:underwater_ruin/big_warm_5"},
    {"minecraft:ruin/big_warm_6", "minecraft:underwater_ruin/big_warm_6"},
    {"minecraft:ruin/big_warm_7", "minecraft:underwater_ruin/big_warm_7"},
};

inline constexpr PieceRenames kPieceRenames[]{
    {"EndCity", "ECP", kEndCityRenames, 3},
    {"Mansion", "WMP", kMansionRenames, 4},
    {"Igloo", "Iglu", kIglooRenames, 3},
    {"Ocean_Ruin", "ORP", kOceanRuinRenames, 64},
};

} // namespace basalt::metamorph::datafix::structures
