#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace basalt::metamorph::datafix::legacy {

/// A block state as the game names it since 1.13: an id and its properties.
struct State {
    std::string name;
    std::map<std::string, std::string> properties;
};

/// parity: `BlockStateData.getTag`, the 1.13 state of `block << 4 | data`; an unmapped one is the block's first state.
[[nodiscard]] const State& stateOf(std::int32_t id);

/// parity: `BlockStateData.upgradeBlockStateTag`, the 1.13 state a pre-1.13 state became.
[[nodiscard]] std::optional<State> upgradeState(std::string_view oldName,
                                                const std::map<std::string, std::string>& oldProperties);

/// parity: `BlockStateData.upgradeBlock`, the 1.13 name of a pre-1.13 block name.
[[nodiscard]] std::optional<std::string> upgradeBlockName(std::string_view oldName);

} // namespace basalt::metamorph::datafix::legacy
