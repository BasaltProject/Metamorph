#include "FixSupport.hpp"
#include "ItemWalk.hpp"

#include <basalt/metamorph/datafix/DataFixer.hpp>

#include <algorithm>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtTag;

[[nodiscard]] const std::vector<Fix>& fixes() {
    static const std::vector<Fix> all = [] {
        std::vector<Fix> built;
        addFixesFrom4000(built);
        addTextFixes(built);
        addEntityFixes(built);
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
            forEachCompound(root, "block_entities", [&](NbtCompound& entity) {
                if (fix.blockEntityId.empty() || entity.getString("id") == fix.blockEntityId) {
                    fix.apply(entity, context);
                }
            });
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
    case Target::Components:
        forEachComponents(root, [&](NbtCompound& components) { fix.apply(components, context); });
        break;
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
}

} // namespace basalt::metamorph::datafix
