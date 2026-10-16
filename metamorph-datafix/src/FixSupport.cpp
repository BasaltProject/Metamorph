#include "FixSupport.hpp"

#include <cstddef>
#include <functional>

namespace basalt::metamorph::datafix {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;

std::string namespaced(std::string_view text) {
    return text.find(':') == std::string_view::npos ? "minecraft:" + std::string{text} : std::string{text};
}

void rename(NbtCompound& compound, std::string_view from, std::string_view to) {
    const NbtTag* found = compound.find(from);
    if (found == nullptr) {
        return;
    }
    NbtTag moved = *found;
    compound.remove(from);
    compound.set(to, std::move(moved));
}

void forEachCompound(NbtCompound& root, std::string_view listKey, const std::function<void(NbtCompound&)>& edit) {
    NbtTag* held = root.find(listKey);
    NbtList* list = held == nullptr ? nullptr : held->get<NbtList>();
    if (list == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < list->size(); ++i) {
        if (NbtCompound* entry = (*list)[i].get<NbtCompound>(); entry != nullptr) {
            edit(*entry);
        }
    }
}

std::string_view plainTranslationKey(const NbtTag* component) {
    const NbtCompound* fields = component == nullptr ? nullptr : component->get<NbtCompound>();
    if (fields == nullptr || fields->size() != 1) {
        return {};
    }
    return fields->getString("translate");
}

namespace {

void withPassengers(NbtCompound& entity, const std::function<void(NbtCompound&)>& edit) {
    edit(entity);
    NbtTag* passengers = entity.find("Passengers");
    NbtList* list = passengers == nullptr ? nullptr : passengers->get<NbtList>();
    if (list == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < list->size(); ++i) {
        if (NbtCompound* rider = (*list)[i].get<NbtCompound>(); rider != nullptr) {
            withPassengers(*rider, edit);
        }
    }
}

} // namespace

void forEachEntity(Domain domain, NbtCompound& root, const std::function<void(NbtCompound&)>& edit) {
    if (domain == Domain::Player) {
        withPassengers(root, edit);
        return;
    }
    // parity: Basalt keeps the entities of a chunk in its own key, shaped as vanilla's entity files shape them.
    const std::string_view key = domain == Domain::Entities ? "Entities" : "basalt_entities";
    NbtTag* held = root.find(key);
    NbtList* list = held == nullptr ? nullptr : held->get<NbtList>();
    if (list == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < list->size(); ++i) {
        if (NbtCompound* entity = (*list)[i].get<NbtCompound>(); entity != nullptr) {
            withPassengers(*entity, edit);
        }
    }
}

} // namespace basalt::metamorph::datafix
