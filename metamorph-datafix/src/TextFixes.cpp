#include "FixSupport.hpp"
#include "ItemWalk.hpp"

#include <basalt/wire/common/json/Json.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace basalt::metamorph::datafix {
namespace {

using wire::common::JsonObject;
using wire::common::JsonValue;
using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

using TextEdit = std::function<void(NbtTag&)>;

/// parity: `JsonOps.convertTo(NbtOps)`, where a list of mixed kinds wraps its non-compounds as `{"": value}`.
[[nodiscard]] NbtTag jsonToNbt(const JsonValue& json) {
    if (const bool* flag = json.get<bool>(); flag != nullptr) {
        return NbtTag::ofBoolean(*flag);
    }
    if (const auto* whole = json.get<std::int64_t>(); whole != nullptr) {
        return NbtTag::ofDouble(static_cast<double>(*whole));
    }
    if (const double* real = json.get<double>(); real != nullptr) {
        return NbtTag::ofDouble(*real);
    }
    if (const std::string* text = json.get<std::string>(); text != nullptr) {
        return NbtTag::ofString(*text);
    }
    if (const auto* array = json.get<std::vector<JsonValue>>(); array != nullptr) {
        std::vector<NbtTag> elements;
        for (const JsonValue& element : *array) {
            elements.push_back(jsonToNbt(element));
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
        const NbtType type = elements.empty() ? NbtType::End : elements.front().type();
        return NbtTag::ofList(NbtList{uniform ? type : NbtType::Compound, std::move(elements)});
    }
    if (const JsonObject* object = json.get<JsonObject>(); object != nullptr) {
        NbtCompound compound;
        for (std::size_t i = 0; i < object->size(); ++i) {
            compound.set(object->names()[i], jsonToNbt(object->values()[i]));
        }
        return NbtTag::ofCompound(std::move(compound));
    }
    return NbtTag::ofString("null");
}

[[nodiscard]] JsonValue nbtToJson(const NbtTag& tag) {
    if (const NbtCompound* compound = tag.get<NbtCompound>(); compound != nullptr) {
        std::vector<std::string> names = compound->names();
        std::ranges::sort(names);
        JsonObject object;
        for (const std::string& name : names) {
            object.set(name, nbtToJson(*compound->find(name)));
        }
        return JsonValue::ofObject(std::move(object));
    }
    if (const NbtList* list = tag.get<NbtList>(); list != nullptr) {
        std::vector<JsonValue> elements;
        for (const NbtTag& element : *list) {
            elements.push_back(nbtToJson(element));
        }
        return JsonValue::ofArray(std::move(elements));
    }
    if (const std::string* text = tag.get<std::string>(); text != nullptr) {
        return JsonValue::of(*text);
    }
    if (const double* real = tag.get<double>(); real != nullptr) {
        return JsonValue::of(*real);
    }
    if (const float* real = tag.get<float>(); real != nullptr) {
        return JsonValue::of(static_cast<double>(*real));
    }
    if (const auto* whole = tag.get<std::int8_t>(); whole != nullptr) {
        return JsonValue::of(static_cast<std::int64_t>(*whole));
    }
    if (const auto* whole = tag.get<std::int16_t>(); whole != nullptr) {
        return JsonValue::of(static_cast<std::int64_t>(*whole));
    }
    if (const auto* whole = tag.get<std::int32_t>(); whole != nullptr) {
        return JsonValue::of(static_cast<std::int64_t>(*whole));
    }
    if (const auto* whole = tag.get<std::int64_t>(); whole != nullptr) {
        return JsonValue::of(*whole);
    }
    return JsonValue{};
}

void filterable(NbtCompound& holder, const TextEdit& edit) {
    for (const std::string_view key : {"raw", "filtered"}) {
        if (NbtTag* text = holder.find(key); text != nullptr) {
            edit(*text);
        }
    }
}

void textInComponents(NbtCompound& components, const TextEdit& edit) {
    for (const std::string_view key : {"minecraft:custom_name", "minecraft:item_name"}) {
        if (NbtTag* text = components.find(key); text != nullptr) {
            edit(*text);
        }
    }
    if (NbtTag* lore = components.find("minecraft:lore"); lore != nullptr && lore->get<NbtList>() != nullptr) {
        NbtList& lines = *lore->get<NbtList>();
        for (std::size_t i = 0; i < lines.size(); ++i) {
            edit(lines[i]);
        }
    }
    if (NbtTag* held = components.find("minecraft:written_book_content"); held != nullptr) {
        if (NbtCompound* book = held->get<NbtCompound>(); book != nullptr) {
            if (NbtTag* title = book->find("title"); title != nullptr) {
                if (title->get<std::string>() != nullptr) {
                    edit(*title);
                } else if (NbtCompound* filtered = title->get<NbtCompound>(); filtered != nullptr) {
                    filterable(*filtered, edit);
                }
            }
            if (NbtTag* pages = book->find("pages"); pages != nullptr && pages->get<NbtList>() != nullptr) {
                NbtList& list = *pages->get<NbtList>();
                for (std::size_t i = 0; i < list.size(); ++i) {
                    if (list[i].get<std::string>() != nullptr) {
                        edit(list[i]);
                    } else if (NbtCompound* page = list[i].get<NbtCompound>(); page != nullptr) {
                        filterable(*page, edit);
                    }
                }
            }
        }
    }
}

void textInSign(NbtCompound& side, const TextEdit& edit) {
    for (const std::string_view key : {"messages", "filtered_messages"}) {
        if (NbtTag* held = side.find(key); held != nullptr && held->get<NbtList>() != nullptr) {
            NbtList& lines = *held->get<NbtList>();
            for (std::size_t i = 0; i < lines.size(); ++i) {
                edit(lines[i]);
            }
        }
    }
}

/// Every place a chunk or a player keeps a text component, whatever shape it is in.
void forEachText(NbtCompound& root, const TextEdit& edit) {
    forEachComponents(root, [&edit](NbtCompound& components) { textInComponents(components, edit); });
    forEachCompound(root, "block_entities", [&edit](NbtCompound& blockEntity) {
        if (NbtTag* name = blockEntity.find("CustomName"); name != nullptr) {
            edit(*name);
        }
        for (const std::string_view face : {"front_text", "back_text"}) {
            if (NbtTag* side = blockEntity.find(face); side != nullptr && side->get<NbtCompound>() != nullptr) {
                textInSign(*side->get<NbtCompound>(), edit);
            }
        }
    });
}

/// A text component and the components it nests through `extra`, `separator` and hover text.
void walkComponent(NbtTag& tag, const std::function<void(NbtCompound&)>& edit) {
    if (NbtList* list = tag.get<NbtList>(); list != nullptr) {
        for (std::size_t i = 0; i < list->size(); ++i) {
            walkComponent((*list)[i], edit);
        }
        return;
    }
    NbtCompound* component = tag.get<NbtCompound>();
    if (component == nullptr) {
        return;
    }
    edit(*component);
    for (const std::string_view key : {"extra", "separator"}) {
        if (NbtTag* child = component->find(key); child != nullptr) {
            walkComponent(*child, edit);
        }
    }
    for (const std::string_view key : {"hoverEvent", "hover_event"}) {
        NbtTag* hover = component->find(key);
        NbtCompound* event = hover == nullptr ? nullptr : hover->get<NbtCompound>();
        if (event == nullptr) {
            continue;
        }
        for (const std::string_view text : {"contents", "value"}) {
            if (NbtTag* child = event->find(text); child != nullptr && event->getString("action") == "show_text") {
                walkComponent(*child, edit);
            }
        }
    }
}

void walkAllText(NbtCompound& root, const std::function<void(NbtCompound&)>& edit) {
    forEachText(root, [&edit](NbtTag& text) { walkComponent(text, edit); });
}

// parity: `UnflattenTextComponentFix`.
void unflattenText(NbtCompound& root, const Context&) {
    forEachText(root, [](NbtTag& text) {
        const std::string* json = text.get<std::string>();
        if (json == nullptr) {
            return;
        }
        if (const auto parsed = wire::common::parseJson(*json); parsed.has_value() && !parsed->isNull()) {
            text = jsonToNbt(*parsed);
        }
    });
}

void stringToBool(NbtCompound& component, std::string_view key) {
    const NbtTag* value = component.find(key);
    if (const std::string* text = value == nullptr ? nullptr : value->get<std::string>(); text != nullptr) {
        component.putBoolean(key, *text == "true");
    }
}

// parity: `LegacyHoverEventFix`.
void legacyHoverEvent(NbtCompound& root, const Context&) {
    walkAllText(root, [](NbtCompound& component) {
        NbtTag* hover = component.find("hoverEvent");
        NbtCompound* event = hover == nullptr ? nullptr : hover->get<NbtCompound>();
        if (event == nullptr || !event->contains("value")) {
            return;
        }
        if (event->getString("action") == "show_text") {
            rename(*event, "value", "contents");
            return;
        }
        NbtCompound placeholder;
        placeholder.putString("action", "show_text");
        NbtCompound text;
        text.putString("text", "Legacy hoverEvent: " + wire::common::writeJson(nbtToJson(*hover)));
        placeholder.set("contents", NbtTag::ofCompound(std::move(text)));
        *hover = NbtTag::ofCompound(std::move(placeholder));
    });
}

// parity: `TextComponentStringifiedFlagsFix`.
void stringifiedFlags(NbtCompound& root, const Context&) {
    walkAllText(root, [](NbtCompound& component) {
        for (const std::string_view flag : {"bold", "italic", "underlined", "strikethrough", "obfuscated"}) {
            stringToBool(component, flag);
        }
    });
}

[[nodiscard]] bool validUri(std::string_view uri) {
    const std::size_t colon = uri.find(':');
    if (colon == std::string_view::npos || colon == 0) {
        return false;
    }
    std::string scheme{uri.substr(0, colon)};
    std::ranges::transform(scheme, scheme.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return scheme == "http" || scheme == "https";
}

[[nodiscard]] bool validChat(std::string_view text) {
    return std::ranges::none_of(text, [](unsigned char c) { return c == 0xC2 || c < ' ' || c == 127; });
}

void copyField(const NbtCompound& from, NbtCompound& to, std::string_view key) {
    if (const NbtTag* value = from.find(key); value != nullptr) {
        to.set(key, *value);
    }
}

void fixHoverEvent(NbtCompound& event) {
    const std::string action{event.getString("action")};
    if (action == "show_text") {
        rename(event, "contents", "value");
    } else if (action == "show_item" || action == "show_entity") {
        const NbtTag* held = event.find("contents");
        const NbtCompound contents =
            held != nullptr && held->get<NbtCompound>() != nullptr ? *held->get<NbtCompound>() : NbtCompound{};
        event.remove("contents");
        if (action == "show_item") {
            for (const std::string_view key : {"id", "count", "components"}) {
                copyField(contents, event, key);
            }
        } else {
            for (const std::string_view key : {"id", "type", "name"}) {
                copyField(contents, event, key);
            }
            rename(event, "id", "uuid");
            rename(event, "type", "id");
        }
    }
}

/// False when the click event is not valid any more and must be dropped.
[[nodiscard]] bool fixClickEvent(NbtCompound& event) {
    const std::string action{event.getString("action")};
    const std::string value{event.getString("value")};
    if (action == "open_url") {
        if (!validUri(value)) {
            return false;
        }
        rename(event, "value", "url");
    } else if (action == "open_file") {
        rename(event, "value", "path");
    } else if (action == "run_command" || action == "suggest_command") {
        if (!validChat(value)) {
            return false;
        }
        rename(event, "value", "command");
    } else if (action == "change_page") {
        const NbtTag* raw = event.find("value");
        std::optional<std::int32_t> page;
        if (raw != nullptr) {
            if (const auto* i = raw->get<std::int32_t>(); i != nullptr) {
                page = *i;
            } else if (const auto* d = raw->get<double>(); d != nullptr) {
                page = static_cast<std::int32_t>(*d);
            } else if (const std::string* s = raw->get<std::string>(); s != nullptr && !s->empty()) {
                try {
                    page = std::stoi(*s);
                } catch (const std::exception&) {
                    page = std::nullopt;
                }
            }
        }
        if (!page.has_value()) {
            return false;
        }
        event.remove("value");
        event.putInt("page", std::max(*page, 1));
    }
    return true;
}

// parity: `TextComponentHoverAndClickEventFix`.
void hoverAndClickEvents(NbtCompound& root, const Context&) {
    walkAllText(root, [](NbtCompound& component) {
        if (NbtTag* hover = component.find("hoverEvent"); hover != nullptr) {
            if (NbtCompound* event = hover->get<NbtCompound>(); event != nullptr) {
                fixHoverEvent(*event);
            }
            rename(component, "hoverEvent", "hover_event");
        }
        if (NbtTag* click = component.find("clickEvent"); click != nullptr) {
            NbtCompound* event = click->get<NbtCompound>();
            if (event != nullptr && !fixClickEvent(*event)) {
                component.remove("clickEvent");
            } else {
                rename(component, "clickEvent", "click_event");
            }
        }
    });
}

} // namespace

void addTextFixes(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {4290, Target::Root, {}, unflattenText},
                                  {4291, Target::Root, {}, legacyHoverEvent},
                                  {4291, Target::Root, {}, stringifiedFlags},
                                  {4292, Target::Root, {}, hoverAndClickEvents},
                              });
}

} // namespace basalt::metamorph::datafix
