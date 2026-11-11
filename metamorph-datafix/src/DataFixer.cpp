#include "FixSupport.hpp"
#include "ItemWalk.hpp"

#include <basalt/metamorph/datafix/DataFixer.hpp>
#include <basalt/metamorph/datafix/ProgressData.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <tuple>

namespace basalt::metamorph::datafix {
namespace {

/// A fix may stand for several vanilla steps: its name then lists them separated by commas.
[[nodiscard]] bool nameMatches(std::string_view fixName, std::string_view step) {
    while (!fixName.empty()) {
        const std::size_t comma = fixName.find(',');
        if (fixName.substr(0, comma) == step) {
            return true;
        }
        fixName = comma == std::string_view::npos ? std::string_view{} : fixName.substr(comma + 1);
    }
    return false;
}

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

struct DomainName {
    Domain domain;
    std::string_view name;
};

constexpr std::array kDomainNames{
    DomainName{Domain::Chunk, "chunk"},
    DomainName{Domain::Player, "player"},
    DomainName{Domain::Entities, "entity_chunk"},
    DomainName{Domain::Level, "level"},
    DomainName{Domain::Structure, "structure"},
    DomainName{Domain::PoiChunk, "poi_chunk"},
    DomainName{Domain::Options, "options"},
    DomainName{Domain::Hotbar, "hotbar"},
    DomainName{Domain::WorldGenSettings, "world_gen_settings"},
    DomainName{Domain::DebugProfile, "debug_profile"},
    DomainName{Domain::SavedData, "saved_data"},
    DomainName{Domain::Stats, "stats"},
    DomainName{Domain::Advancements, "advancements"},
};

void apply(const Fix& fix, Domain domain, NbtCompound& root, const Context& context) {
    switch (fix.target) {
    case Target::Chunk:
        if (domain == Domain::Chunk) {
            fix.apply(root, context);
        }
        break;
    case Target::BlockEntity:
        if (domain == Domain::Chunk) {
            const auto each = [&](NbtCompound& holder, std::string_view key) {
                forEachCompound(holder, key, [&](NbtCompound& entity) {
                    if (fix.blockEntityId.empty() || entity.getString("id") == fix.blockEntityId) {
                        fix.apply(entity, context);
                    }
                });
            };
            each(root, "block_entities");
            if (NbtTag* level = root.find("Level"); level != nullptr && level->get<NbtCompound>() != nullptr) {
                each(*level->get<NbtCompound>(), "TileEntities");
            }
        }
        break;
    case Target::Palette:
        if (domain == Domain::Chunk) {
            forEachCompound(root, "sections", [&](NbtCompound& section) {
                NbtTag* states = section.find("block_states");
                NbtCompound* held = states == nullptr ? nullptr : states->get<NbtCompound>();
                if (held != nullptr) {
                    forEachCompound(*held, "palette", [&](NbtCompound& entry) { fix.apply(entry, context); });
                }
            });
        }
        break;
    case Target::Entity:
        forEachEntity(domain, root, [&](NbtCompound& entity) {
            if (fix.blockEntityId.empty() || namespaced(entity.getString("id")) == fix.blockEntityId) {
                fix.apply(entity, context);
            }
        });
        break;
    case Target::Root:
        fix.apply(root, context);
        break;
    case Target::ItemStack:
        forEachStack(root, [&](NbtCompound& stack) { fix.apply(stack, context); });
        break;
    case Target::LegacyStack:
        forEachLegacyStack(root, [&](NbtCompound& stack) { fix.apply(stack, context); });
        break;
    case Target::Components:
        forEachComponents(root, [&](NbtCompound& components) { fix.apply(components, context); });
        break;
    }
}

[[nodiscard]] bool isTreeFix(const Fix& fix) { return (fix.domains & kTreeDomains) != 0; }

[[nodiscard]] bool isItemFix(const Fix& fix) {
    return fix.target == Target::ItemStack || fix.target == Target::LegacyStack || fix.target == Target::Components ||
           fix.target == Target::Root;
}

[[nodiscard]] NbtCompound* compoundNamed(NbtCompound& holder, std::string_view key) {
    NbtTag* found = holder.find(key);
    return found == nullptr ? nullptr : found->get<NbtCompound>();
}

void applyToLevel(const Fix& fix, NbtCompound& root, const Context& context) {
    if ((fix.domains & domainBit(Domain::Level)) != 0) {
        fix.apply(root, context);
    }
    if ((fix.domains & domainBit(Domain::WorldGenSettings)) != 0) {
        for (const std::string_view key : {"WorldGenSettings", "world_gen_settings"}) {
            if (NbtCompound* settings = compoundNamed(root, key); settings != nullptr) {
                fix.apply(*settings, context);
            }
        }
    }
    if (isTreeFix(fix)) {
        if (NbtCompound* player = compoundNamed(root, "Player"); player != nullptr) {
            apply(fix, Domain::Player, *player, context);
        }
    }
}

void applyTo(const Fix& fix, Domain domain, NbtCompound& root, const Context& context) {
    switch (domain) {
    case Domain::Chunk:
    case Domain::Player:
    case Domain::Entities:
        if ((fix.domains & domainBit(domain)) != 0) {
            apply(fix, domain, root, context);
        }
        break;
    case Domain::Level:
        applyToLevel(fix, root, context);
        break;
    case Domain::Hotbar:
        if (isTreeFix(fix) && isItemFix(fix)) {
            apply(fix, Domain::Player, root, context);
        }
        if ((fix.domains & domainBit(domain)) != 0) {
            fix.apply(root, context);
        }
        break;
    case Domain::SavedData:
        if ((fix.domains & domainBit(domain)) != 0) {
            fix.apply(root, context);
        }
        if ((fix.domains & domainBit(Domain::WorldGenSettings)) != 0 && context.savedData == "world_gen_settings") {
            if (NbtCompound* settings = compoundNamed(root, "data"); settings != nullptr) {
                fix.apply(*settings, context);
            }
        }
        break;
    default:
        if ((fix.domains & domainBit(domain)) != 0) {
            fix.apply(root, context);
        }
        break;
    }
}

/// A fix may turn the elements of a list into another type; the list has to say what they are now.
void settleListTypes(NbtTag& tag) {
    if (NbtCompound* compound = tag.get<NbtCompound>(); compound != nullptr) {
        for (const std::string& name : std::vector<std::string>{compound->names()}) {
            if (NbtTag* child = compound->find(name); child != nullptr) {
                settleListTypes(*child);
            }
        }
    } else if (NbtList* list = tag.get<NbtList>(); list != nullptr) {
        std::vector<NbtTag> elements(list->begin(), list->end());
        for (NbtTag& element : elements) {
            settleListTypes(element);
        }
        const NbtType type = elements.empty() ? NbtType::End : elements.front().type();
        *list = NbtList{type, std::move(elements)};
    }
}

void settle(NbtCompound& root) {
    NbtTag wrapped = NbtTag::ofCompound(std::move(root));
    settleListTypes(wrapped);
    root = std::move(*wrapped.get<NbtCompound>());
}

void runRange(Domain domain, NbtCompound& root, std::int32_t fromVersion, std::int32_t toVersion,
              const Context& context, std::string_view only = {}, std::int32_t exact = -1) {
    for (const Fix& fix : allFixes()) {
        if (fix.version <= fromVersion || fix.version > toVersion || (exact >= 0 && fix.version != exact)) {
            continue;
        }
        if (!only.empty() && !nameMatches(fix.name, only)) {
            continue;
        }
        applyTo(fix, domain, root, context);
    }
}

void runStructure(NbtCompound& root, std::int32_t fromVersion, std::int32_t toVersion, const Context& context) {
    NbtCompound working = structureAsChunk(root);
    for (const Fix& fix : allFixes()) {
        if (fix.version <= fromVersion || fix.version > toVersion) {
            continue;
        }
        if ((fix.domains & domainBit(Domain::Structure)) != 0) {
            fix.apply(working, context);
        } else if ((fix.domains & domainBit(Domain::Chunk)) != 0) {
            apply(fix, Domain::Chunk, working, context);
        }
    }
    settle(working);
    chunkIntoStructure(working, root);
}

[[nodiscard]] bool coveredBy(const Fix& fix, const StepInfo& step, Domain domain) {
    if (fix.version != step.version) {
        return false;
    }
    bool applies = (fix.domains & domainBit(domain)) != 0;
    if (!applies && isTreeFix(fix)) {
        applies =
            domain == Domain::Level || domain == Domain::Structure || (domain == Domain::Hotbar && isItemFix(fix));
    }
    if (!applies) {
        return false;
    }
    if (fix.name.empty() || nameMatches(fix.name, step.name)) {
        return true;
    }
    return fix.name.find(',') == std::string_view::npos && std::ranges::none_of(steps(), [&](const StepInfo& other) {
               return other.version == step.version && other.name == fix.name;
           });
}

} // namespace

std::string_view domainName(Domain domain) {
    for (const DomainName& entry : kDomainNames) {
        if (entry.domain == domain) {
            return entry.name;
        }
    }
    return {};
}

bool domainFromName(std::string_view name, Domain& domain) {
    for (const DomainName& entry : kDomainNames) {
        if (entry.name == name) {
            domain = entry.domain;
            return true;
        }
    }
    return false;
}

const std::vector<Fix>& allFixes() {
    static const std::vector<Fix> all = [] {
        std::vector<Fix> built;
        addFixesFrom0100(built);
        addFixesFrom4000(built);
        addTextFixes(built);
        addEntityFixes(built);
        addFixesFrom1200(built);
        addFlatteningFixes(built);
        addFixesFrom1400(built);
        addFixesFrom2600(built);
        addFixesFrom3000(built);
        addFixesFrom3400(built);
        addFixesFrom3800(built);
        addFixesFrom4300(built);
        addFixesFrom4996(built);
        addLevelFixes(built);
        addWorldGenFixes(built);
        addStructureFixes(built);
        addOptionsFixes(built);
        addLegacyGapFixes(built);
        addSavedDataFixes(built);
        std::ranges::stable_sort(built, {}, &Fix::version);
        return built;
    }();
    return all;
}

namespace {

std::string cachedMissingStep(Domain domain, std::int32_t from, std::int32_t to, std::string_view savedData) {
    static std::mutex lock;
    static std::map<std::tuple<Domain, std::int32_t, std::int32_t, std::string>, std::string> known;
    const auto key = std::make_tuple(domain, from, to, std::string{savedData});
    {
        const std::lock_guard guard{lock};
        if (const auto found = known.find(key); found != known.end()) {
            return found->second;
        }
    }
    std::string gap = missingStep(domain, from, to, savedData);
    const std::lock_guard guard{lock};
    return known.emplace(key, std::move(gap)).first->second;
}

} // namespace

std::string upgrade(Domain domain, NbtCompound& root, std::int32_t fromVersion, const Context& given,
                    std::int32_t toVersion) {
    std::string failure;
    Context context = given;
    context.failure = &failure;
    if (const std::string gap = cachedMissingStep(domain, fromVersion, toVersion, given.savedData); !gap.empty()) {
        return "no fix for " + gap;
    }
    if (domain == Domain::Stats) {
        upgradeStatsTree(root, fromVersion);
    } else if (domain == Domain::Advancements) {
        upgradeAdvancementsTree(root, fromVersion);
    } else if (domain == Domain::Structure) {
        runStructure(root, fromVersion, toVersion, context);
        return failure;
    } else {
        runRange(domain, root, fromVersion, toVersion, context);
    }
    settle(root);
    return failure;
}

std::string missingStep(Domain domain, std::int32_t fromVersion, std::int32_t toVersion, std::string_view savedData) {
    if (domain == Domain::Stats || domain == Domain::Advancements) {
        return {};
    }
    if (domain == Domain::SavedData && savedData == "world_gen_settings") {
        return missingStep(Domain::WorldGenSettings, fromVersion, toVersion);
    }
    for (const StepInfo& step : steps()) {
        if (step.version <= fromVersion || step.version > toVersion || (step.domains & domainBit(domain)) == 0) {
            continue;
        }
        if (domain == Domain::SavedData && !savedData.empty() && !savedDataStepFits(step.name, savedData)) {
            continue;
        }
        const bool covered =
            std::ranges::any_of(allFixes(), [&](const Fix& fix) { return coveredBy(fix, step, domain); });
        if (!covered) {
            return std::string{step.name} + "@" + std::to_string(step.version);
        }
    }
    return {};
}

bool applyStep(std::string_view name, std::int32_t key, Domain domain, NbtCompound& root, const Context& context) {
    const auto known = std::ranges::find_if(steps(), [&](const StepInfo& step) {
        return step.name == name && versionKey(step.version, step.subVersion) == key;
    });
    if (known == steps().end() || (known->domains & domainBit(domain)) == 0) {
        return false;
    }
    const std::int32_t version = known->version;
    const bool named = std::ranges::any_of(
        allFixes(), [&](const Fix& fix) { return fix.version == version && nameMatches(fix.name, name); });
    if (domain == Domain::Structure) {
        runStructure(root, version - 1, version, context);
        return true;
    }
    runRange(domain, root, version - 1, version, context, named ? name : std::string_view{}, version);
    settle(root);
    return true;
}

} // namespace basalt::metamorph::datafix
