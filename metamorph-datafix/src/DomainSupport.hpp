#pragma once

#include "FixSupport.hpp"

#include <basalt/wire/common/json/Json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace basalt::metamorph::datafix::support {

[[nodiscard]] std::optional<std::int64_t> whole(const wire::nbt::NbtTag* tag);

[[nodiscard]] std::optional<double> real(const wire::nbt::NbtTag* tag);

[[nodiscard]] wire::nbt::NbtCompound* compoundAt(wire::nbt::NbtCompound& holder, std::string_view key);

[[nodiscard]] wire::nbt::NbtCompound* childAt(wire::nbt::NbtCompound& holder, std::size_t index);

[[nodiscard]] wire::nbt::NbtList* listAt(wire::nbt::NbtCompound& holder, std::string_view key);

[[nodiscard]] wire::nbt::NbtTag compoundList(std::vector<wire::nbt::NbtCompound> entries);

[[nodiscard]] wire::nbt::NbtTag stringList(std::span<const std::string_view> entries);

/// parity: `Dynamic.renameAndFixField`, the value of `from` moved to `to` and rewritten.
void renameAndFix(wire::nbt::NbtCompound& holder, std::string_view from, std::string_view to,
                  const std::function<void(wire::nbt::NbtTag&)>& fix);

/// parity: `JsonOps.convertTo(NbtOps)`, a JSON value as the smallest NBT number that holds it.
[[nodiscard]] wire::nbt::NbtTag jsonAsNbt(const wire::common::JsonValue& json);

/// parity: `AbstractUUIDFix.replaceUUIDLeastMost`.
bool replaceUuidLeastMost(wire::nbt::NbtCompound& tag, std::string_view oldKey, std::string_view newKey);

/// parity: `AbstractUUIDFix.replaceUUIDString`.
bool replaceUuidString(wire::nbt::NbtCompound& tag, std::string_view oldKey, std::string_view newKey);

/// parity: `AbstractUUIDFix.createUUIDFromML`, a `{M, L}` compound as one int array.
[[nodiscard]] std::optional<wire::nbt::NbtTag> uuidFromMl(const wire::nbt::NbtCompound& tag);

/// parity: `AbstractUUIDFix.createUUIDFromLongs`.
[[nodiscard]] std::optional<wire::nbt::NbtTag> uuidFromLongs(const wire::nbt::NbtCompound& tag, std::string_view most,
                                                             std::string_view least);

/// parity: `LegacyComponentDataFixUtils.createTextComponentJson`, `{"text":...}` as Gson writes it.
[[nodiscard]] std::string plainTextJson(std::string_view text);

/// Records why a fix cannot go on, when the caller asked to hear about it.
void refuse(const Context& context, std::string_view reason);

/// The value of a string tag, or nothing for any other kind.
[[nodiscard]] std::optional<std::string> stringOf(const wire::nbt::NbtTag* tag);

/// A text component stored as a JSON string, as `jsonAsNbt` reads it; the tag is left alone when it is not JSON.
void unflattenTextTag(wire::nbt::NbtTag& text);

} // namespace basalt::metamorph::datafix::support
