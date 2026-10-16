#include "FixSupport.hpp"
#include "ItemWalk.hpp"

#include <basalt/metamorph/datafix/DataFixer.hpp>

#include <algorithm>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

[[nodiscard]] const std::vector<Fix>& fixes() {
    static const std::vector<Fix> all = [] {
        std::vector<Fix> built;
        addFixesFrom4000(built);
        addTextFixes(built);
        addEntityFixes(built);
        addFixesFrom2600(built);
        addFixesFrom3000(built);
        addFixesFrom3400(built);
        addFixesFrom3800(built);
        addFixesFrom4300(built);
        addFixesFrom4996(built);
        std::ranges::stable_sort(built, {}, &Fix::version);
        return built;
    }();
    return all;
}

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

} // namespace

void upgrade(Domain domain, NbtCompound& root, std::int32_t fromVersion, const Context& context,
             std::int32_t toVersion) {
    for (const Fix& fix : fixes()) {
        if (fix.version > fromVersion && fix.version <= toVersion) {
            apply(fix, domain, root, context);
        }
    }
    NbtTag wrapped = NbtTag::ofCompound(std::move(root));
    settleListTypes(wrapped);
    root = std::move(*wrapped.get<NbtCompound>());
}

} // namespace basalt::metamorph::datafix
