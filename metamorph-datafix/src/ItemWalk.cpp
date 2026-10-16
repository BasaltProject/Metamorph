#include "ItemWalk.hpp"

#include <cstddef>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;

constexpr int kMaxDepth = 48;

[[nodiscard]] bool numeric(const NbtTag* tag) {
    return tag != nullptr && (tag->get<std::int8_t>() != nullptr || tag->get<std::int16_t>() != nullptr ||
                              tag->get<std::int32_t>() != nullptr);
}

[[nodiscard]] bool isStack(const NbtCompound& tag) {
    const NbtTag* id = tag.find("id");
    return id != nullptr && id->get<std::string>() != nullptr && numeric(tag.find("count"));
}

void walk(NbtTag& tag, const Edit& edit, int depth);

void walkCompound(NbtCompound& compound, const Edit& edit, int depth) {
    if (depth > kMaxDepth) {
        return;
    }
    if (isStack(compound)) {
        edit(compound);
    }
    const std::vector<std::string> names = compound.names();
    for (const std::string& name : names) {
        if (NbtTag* child = compound.find(name); child != nullptr) {
            walk(*child, edit, depth + 1);
        }
    }
}

void walk(NbtTag& tag, const Edit& edit, int depth) {
    if (NbtCompound* compound = tag.get<NbtCompound>(); compound != nullptr) {
        walkCompound(*compound, edit, depth);
    } else if (NbtList* list = tag.get<NbtList>(); list != nullptr) {
        for (std::size_t i = 0; i < list->size(); ++i) {
            walk((*list)[i], edit, depth + 1);
        }
    }
}

} // namespace

void forEachStack(NbtCompound& root, const Edit& edit) { walkCompound(root, edit, 0); }

void forEachComponents(NbtCompound& root, const Edit& edit) {
    forEachStack(root, [&edit](NbtCompound& stack) {
        if (NbtTag* components = stack.find("components"); components != nullptr) {
            if (NbtCompound* held = components->get<NbtCompound>(); held != nullptr) {
                edit(*held);
            }
        }
    });
}

} // namespace basalt::metamorph::datafix
