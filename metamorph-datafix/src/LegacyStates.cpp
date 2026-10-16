#include "LegacyStates.hpp"

#include "LegacyBlocks.hpp"

#include <array>
#include <vector>

namespace basalt::metamorph::datafix::legacy {
namespace {

std::map<std::string, std::string> parseProperties(std::string_view text) {
    std::map<std::string, std::string> properties;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t comma = text.find(',', start);
        comma = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view pair = text.substr(start, comma - start);
        const std::size_t equals = pair.find('=');
        properties[std::string{pair.substr(0, equals)}] = std::string{pair.substr(equals + 1)};
        start = comma + 1;
    }
    return properties;
}

struct Tables {
    std::array<State, 4096> states;
    std::array<const State*, 4096> byId{};
    std::map<std::pair<std::string, std::map<std::string, std::string>>, std::int32_t> idByOld;
    std::map<std::string, std::int32_t, std::less<>> idByOldName;

    Tables() {
        std::array<const State*, 256> blockDefaults{};
        for (const StateRow& row : kStates) {
            const auto id = static_cast<std::size_t>(row.id);
            if (byId[id] == nullptr) {
                states[id] = State{std::string{row.name}, parseProperties(row.properties)};
                byId[id] = &states[id];
                if (blockDefaults[id >> 4] == nullptr) {
                    blockDefaults[id >> 4] = byId[id];
                }
            }
            if (!row.oldName.empty()) {
                idByOldName.emplace(std::string{row.oldName}, row.id);
                idByOld[{std::string{row.oldName}, parseProperties(row.oldProperties)}] = row.id;
            }
        }
        for (std::size_t i = 0; i < byId.size(); ++i) {
            if (byId[i] == nullptr) {
                byId[i] = blockDefaults[i >> 4];
            }
        }
    }
};

const Tables& tables() {
    static const Tables kTables;
    return kTables;
}

} // namespace

const State& stateOf(std::int32_t id) {
    const Tables& table = tables();
    const State* found = id >= 0 && id < 4096 ? table.byId[static_cast<std::size_t>(id)] : nullptr;
    return found != nullptr ? *found : *table.byId[0];
}

std::optional<State> upgradeState(std::string_view oldName, const std::map<std::string, std::string>& oldProperties) {
    const Tables& table = tables();
    const auto found = table.idByOld.find({std::string{oldName}, oldProperties});
    if (found == table.idByOld.end()) {
        return std::nullopt;
    }
    const State* state = table.byId[static_cast<std::size_t>(found->second)];
    return state == nullptr ? std::nullopt : std::optional{*state};
}

std::optional<std::string> upgradeBlockName(std::string_view oldName) {
    const Tables& table = tables();
    const auto found = table.idByOldName.find(oldName);
    if (found == table.idByOldName.end()) {
        return std::nullopt;
    }
    const State* state = table.byId[static_cast<std::size_t>(found->second)];
    return state == nullptr ? std::nullopt : std::optional{state->name};
}

} // namespace basalt::metamorph::datafix::legacy
