#include "DomainSupport.hpp"

#include <algorithm>
#include <cmath>

namespace basalt::metamorph::datafix::support {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

std::string plainTextJson(std::string_view text) {
    std::string out = "{\"text\":\"";
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default:
            if (c < 0x20) {
                constexpr std::string_view kHex = "0123456789abcdef";
                out += "\\u00";
                out += kHex[c >> 4];
                out += kHex[c & 15];
            } else if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                       (static_cast<unsigned char>(text[i + 2]) == 0xA8 ||
                        static_cast<unsigned char>(text[i + 2]) == 0xA9)) {
                out += static_cast<unsigned char>(text[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
                i += 2;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"}";
}

std::optional<std::int64_t> whole(const NbtTag* tag) {
    if (tag == nullptr) {
        return std::nullopt;
    }
    if (const auto* v = tag->get<std::int8_t>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<std::int16_t>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<std::int32_t>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<std::int64_t>(); v != nullptr) {
        return *v;
    }
    return std::nullopt;
}

std::optional<double> real(const NbtTag* tag) {
    if (tag == nullptr) {
        return std::nullopt;
    }
    if (const auto* v = tag->get<double>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<float>(); v != nullptr) {
        return static_cast<double>(*v);
    }
    const std::optional<std::int64_t> integral = whole(tag);
    return integral.has_value() ? std::optional<double>{static_cast<double>(*integral)} : std::nullopt;
}

NbtCompound* compoundAt(NbtCompound& holder, std::string_view key) {
    NbtTag* found = holder.find(key);
    return found == nullptr ? nullptr : found->get<NbtCompound>();
}

NbtCompound* childAt(NbtCompound& holder, std::size_t index) {
    const std::string name = holder.names()[index];
    return compoundAt(holder, name);
}

NbtList* listAt(NbtCompound& holder, std::string_view key) {
    NbtTag* found = holder.find(key);
    return found == nullptr ? nullptr : found->get<NbtList>();
}

NbtTag compoundList(std::vector<NbtCompound> entries) {
    std::vector<NbtTag> elements;
    elements.reserve(entries.size());
    for (NbtCompound& entry : entries) {
        elements.push_back(NbtTag::ofCompound(std::move(entry)));
    }
    return NbtTag::ofList(NbtList{NbtType::Compound, std::move(elements)});
}

NbtTag stringList(std::span<const std::string_view> entries) {
    std::vector<NbtTag> elements;
    for (const std::string_view entry : entries) {
        elements.push_back(NbtTag::ofString(std::string{entry}));
    }
    return NbtTag::ofList(NbtList{NbtType::String, std::move(elements)});
}

void renameAndFix(NbtCompound& holder, std::string_view from, std::string_view to,
                  const std::function<void(NbtTag&)>& fix) {
    const NbtTag* found = holder.find(from);
    if (found == nullptr) {
        return;
    }
    NbtTag moved = *found;
    holder.remove(from);
    fix(moved);
    holder.set(to, std::move(moved));
}

NbtTag jsonAsNbt(const wire::common::JsonValue& json) {
    using wire::common::JsonObject;
    using wire::common::JsonValue;
    if (const bool* flag = json.get<bool>(); flag != nullptr) {
        return NbtTag::ofBoolean(*flag);
    }
    if (const auto* integral = json.get<std::int64_t>(); integral != nullptr) {
        const std::int64_t value = *integral;
        if (value == static_cast<std::int8_t>(value)) {
            return NbtTag::ofByte(static_cast<std::int8_t>(value));
        }
        if (value == static_cast<std::int16_t>(value)) {
            return NbtTag::ofShort(static_cast<std::int16_t>(value));
        }
        if (value == static_cast<std::int32_t>(value)) {
            return NbtTag::ofInt(static_cast<std::int32_t>(value));
        }
        return NbtTag::ofLong(value);
    }
    if (const double* number = json.get<double>(); number != nullptr) {
        const double whole = std::trunc(*number);
        if (whole == *number && std::abs(whole) < 9.0e18) {
            return jsonAsNbt(JsonValue::of(static_cast<std::int64_t>(whole)));
        }
        if (static_cast<double>(static_cast<float>(*number)) == *number) {
            return NbtTag::ofFloat(static_cast<float>(*number));
        }
        return NbtTag::ofDouble(*number);
    }
    if (const std::string* text = json.get<std::string>(); text != nullptr) {
        return NbtTag::ofString(*text);
    }
    if (const auto* array = json.get<std::vector<JsonValue>>(); array != nullptr) {
        std::vector<NbtTag> elements;
        for (const JsonValue& element : *array) {
            elements.push_back(jsonAsNbt(element));
        }
        const bool uniform = std::ranges::all_of(
            elements, [&](const NbtTag& element) { return element.type() == elements.front().type(); });
        if (!uniform) {
            for (NbtTag& element : elements) {
                if (element.get<NbtCompound>() == nullptr) {
                    NbtCompound wrapped;
                    wrapped.set("", element);
                    element = NbtTag::ofCompound(std::move(wrapped));
                }
            }
        }
        const NbtType type = elements.empty() ? NbtType::End : (uniform ? elements.front().type() : NbtType::Compound);
        return NbtTag::ofList(NbtList{type, std::move(elements)});
    }
    if (const JsonObject* object = json.get<JsonObject>(); object != nullptr) {
        NbtCompound compound;
        for (std::size_t i = 0; i < object->size(); ++i) {
            compound.set(object->names()[i], jsonAsNbt(object->values()[i]));
        }
        return NbtTag::ofCompound(std::move(compound));
    }
    return NbtTag::ofString("null");
}

namespace {

[[nodiscard]] NbtTag uuidArray(std::int64_t most, std::int64_t least) {
    return NbtTag::ofIntArray({static_cast<std::int32_t>(most >> 32), static_cast<std::int32_t>(most),
                               static_cast<std::int32_t>(least >> 32), static_cast<std::int32_t>(least)});
}

} // namespace

bool replaceUuidLeastMost(NbtCompound& tag, std::string_view oldKey, std::string_view newKey) {
    const std::string most = std::string{oldKey} + "Most";
    const std::string least = std::string{oldKey} + "Least";
    const std::int64_t high = whole(tag.find(most)).value_or(0);
    const std::int64_t low = whole(tag.find(least)).value_or(0);
    if (high == 0 || low == 0) {
        return false;
    }
    tag.remove(most);
    tag.remove(least);
    tag.set(newKey, uuidArray(high, low));
    return true;
}

bool replaceUuidString(NbtCompound& tag, std::string_view oldKey, std::string_view newKey) {
    const NbtTag* held = tag.find(oldKey);
    const std::string* text = held == nullptr ? nullptr : held->get<std::string>();
    if (text == nullptr || text->size() != 36) {
        return false;
    }
    std::string digits;
    for (const char c : *text) {
        if (c != '-') {
            digits += c;
        }
    }
    if (digits.size() != 32 || !std::ranges::all_of(digits, [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        return false;
    }
    const std::uint64_t high = std::stoull(digits.substr(0, 16), nullptr, 16);
    const std::uint64_t low = std::stoull(digits.substr(16), nullptr, 16);
    tag.remove(oldKey);
    tag.set(newKey, uuidArray(static_cast<std::int64_t>(high), static_cast<std::int64_t>(low)));
    return true;
}

std::optional<NbtTag> uuidFromMl(const NbtCompound& tag) {
    const std::int64_t high = whole(tag.find("M")).value_or(0);
    const std::int64_t low = whole(tag.find("L")).value_or(0);
    if (high == 0 || low == 0) {
        return std::nullopt;
    }
    return uuidArray(high, low);
}

std::optional<NbtTag> uuidFromLongs(const NbtCompound& tag, std::string_view most, std::string_view least) {
    const std::int64_t high = whole(tag.find(most)).value_or(0);
    const std::int64_t low = whole(tag.find(least)).value_or(0);
    if (high == 0 || low == 0) {
        return std::nullopt;
    }
    return uuidArray(high, low);
}

void refuse(const Context& context, std::string_view reason) {
    if (context.failure != nullptr && context.failure->empty()) {
        *context.failure = std::string{reason};
    }
}

std::optional<std::string> stringOf(const NbtTag* tag) {
    const std::string* text = tag == nullptr ? nullptr : tag->get<std::string>();
    return text == nullptr ? std::nullopt : std::optional<std::string>{*text};
}

void unflattenTextTag(NbtTag& text) {
    const std::string* json = text.get<std::string>();
    if (json == nullptr) {
        return;
    }
    if (const auto parsed = wire::common::parseJson(*json); parsed.has_value() && !parsed->isNull()) {
        text = jsonAsNbt(*parsed);
    }
}

} // namespace basalt::metamorph::datafix::support
