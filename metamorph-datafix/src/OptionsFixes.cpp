#include "DomainSupport.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace basalt::metamorph::datafix {
namespace {

using namespace support;
using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr std::uint32_t kOptions = domainBit(Domain::Options);
constexpr std::uint32_t kDebug = domainBit(Domain::DebugProfile);
constexpr std::uint32_t kHotbar = domainBit(Domain::Hotbar);

[[nodiscard]] std::string valueOf(const NbtCompound& options, std::string_view key, std::string_view fallback) {
    const std::optional<std::string> text = stringOf(options.find(key));
    return text.has_value() ? *text : std::string{fallback};
}

/// parity: `Double.toString`, the shortest digits that read back, in plain form between 1e-3 and 1e7.
[[nodiscard]] std::string javaDouble(double value) {
    if (value == 0.0) {
        return "0.0";
    }
    std::array<char, 40> buffer{};
    const auto written =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), std::abs(value), std::chars_format::scientific);
    const std::string_view text{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())};
    const std::size_t exponentAt = text.find('e');
    std::string digits;
    for (const char c : text.substr(0, exponentAt)) {
        if (c != '.') {
            digits += c;
        }
    }
    const int exponent = std::atoi(std::string{text.substr(exponentAt + 1)}.c_str());
    std::string result = value < 0 ? "-" : "";
    if (exponent >= -3 && exponent < 7) {
        if (exponent >= 0) {
            const std::size_t whole = static_cast<std::size_t>(exponent) + 1;
            digits.resize(std::max(digits.size(), whole), '0');
            result += digits.substr(0, whole) + "." + (digits.size() > whole ? digits.substr(whole) : "0");
        } else {
            result += "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
        }
    } else {
        result +=
            digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : "0") + "E" + std::to_string(exponent);
    }
    return result;
}

[[nodiscard]] std::optional<double> parseDouble(const std::string& text) {
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size()) {
        return std::nullopt;
    }
    return parsed;
}

// parity: `OptionsForceVBOFix`.
void forceVbo(NbtCompound& options, const Context&) { options.putString("useVbo", "true"); }

// parity: `OptionsLowerCaseLanguageFix`.
void lowerCaseLanguage(NbtCompound& options, const Context&) {
    if (const std::optional<std::string> language = stringOf(options.find("lang")); language.has_value()) {
        std::string lowered = *language;
        std::ranges::transform(lowered, lowered.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        options.putString("lang", lowered);
    }
}

constexpr std::pair<int, std::string_view> kLwjglKeys[]{
    {0, "key.unknown"},
    {11, "key.0"},
    {2, "key.1"},
    {3, "key.2"},
    {4, "key.3"},
    {5, "key.4"},
    {6, "key.5"},
    {7, "key.6"},
    {8, "key.7"},
    {9, "key.8"},
    {10, "key.9"},
    {30, "key.a"},
    {40, "key.apostrophe"},
    {48, "key.b"},
    {43, "key.backslash"},
    {14, "key.backspace"},
    {46, "key.c"},
    {58, "key.caps.lock"},
    {51, "key.comma"},
    {32, "key.d"},
    {211, "key.delete"},
    {208, "key.down"},
    {18, "key.e"},
    {207, "key.end"},
    {28, "key.enter"},
    {13, "key.equal"},
    {1, "key.escape"},
    {33, "key.f"},
    {59, "key.f1"},
    {68, "key.f10"},
    {87, "key.f11"},
    {88, "key.f12"},
    {100, "key.f13"},
    {101, "key.f14"},
    {102, "key.f15"},
    {103, "key.f16"},
    {104, "key.f17"},
    {105, "key.f18"},
    {113, "key.f19"},
    {60, "key.f2"},
    {61, "key.f3"},
    {62, "key.f4"},
    {63, "key.f5"},
    {64, "key.f6"},
    {65, "key.f7"},
    {66, "key.f8"},
    {67, "key.f9"},
    {34, "key.g"},
    {41, "key.grave.accent"},
    {35, "key.h"},
    {199, "key.home"},
    {23, "key.i"},
    {210, "key.insert"},
    {36, "key.j"},
    {37, "key.k"},
    {82, "key.keypad.0"},
    {79, "key.keypad.1"},
    {80, "key.keypad.2"},
    {81, "key.keypad.3"},
    {75, "key.keypad.4"},
    {76, "key.keypad.5"},
    {77, "key.keypad.6"},
    {71, "key.keypad.7"},
    {72, "key.keypad.8"},
    {73, "key.keypad.9"},
    {78, "key.keypad.add"},
    {83, "key.keypad.decimal"},
    {181, "key.keypad.divide"},
    {156, "key.keypad.enter"},
    {141, "key.keypad.equal"},
    {55, "key.keypad.multiply"},
    {74, "key.keypad.subtract"},
    {38, "key.l"},
    {203, "key.left"},
    {56, "key.left.alt"},
    {26, "key.left.bracket"},
    {29, "key.left.control"},
    {42, "key.left.shift"},
    {219, "key.left.win"},
    {50, "key.m"},
    {12, "key.minus"},
    {49, "key.n"},
    {69, "key.num.lock"},
    {24, "key.o"},
    {25, "key.p"},
    {209, "key.page.down"},
    {201, "key.page.up"},
    {197, "key.pause"},
    {52, "key.period"},
    {183, "key.print.screen"},
    {16, "key.q"},
    {19, "key.r"},
    {205, "key.right"},
    {184, "key.right.alt"},
    {27, "key.right.bracket"},
    {157, "key.right.control"},
    {54, "key.right.shift"},
    {220, "key.right.win"},
    {31, "key.s"},
    {70, "key.scroll.lock"},
    {39, "key.semicolon"},
    {53, "key.slash"},
    {57, "key.space"},
    {20, "key.t"},
    {15, "key.tab"},
    {22, "key.u"},
    {200, "key.up"},
    {47, "key.v"},
    {17, "key.w"},
    {45, "key.x"},
    {21, "key.y"},
    {44, "key.z"},
};

[[nodiscard]] bool isKeyOption(const std::string& name) { return name.rfind("key_", 0) == 0; }

// parity: `OptionsKeyLwjgl3Fix`.
void lwjgl3Keys(NbtCompound& options, const Context& context) {
    for (const std::string& name : options.names()) {
        if (!isKeyOption(name)) {
            continue;
        }
        const std::string text = valueOf(options, name, "");
        char* end = nullptr;
        const long old = std::strtol(text.c_str(), &end, 10);
        if (text.empty() || end != text.c_str() + text.size()) {
            refuse(context, "Options key code is not a number: " + text);
            return;
        }
        std::string replacement = "key.unknown";
        if (old < 0) {
            const long button = old + 100;
            replacement = button == 0   ? "key.mouse.left"
                          : button == 1 ? "key.mouse.right"
                          : button == 2 ? "key.mouse.middle"
                                        : "key.mouse." + std::to_string(button + 1);
        } else {
            for (const auto& [code, key] : kLwjglKeys) {
                if (code == old) {
                    replacement = std::string{key};
                }
            }
        }
        options.putString(name, replacement);
    }
}

// parity: `OptionsKeyTranslationFix`.
void keyTranslation(NbtCompound& options, const Context&) {
    for (const std::string& name : options.names()) {
        if (!isKeyOption(name)) {
            continue;
        }
        const std::string old = valueOf(options, name, "");
        if (old.rfind("key.mouse", 0) != 0 && old.rfind("scancode.", 0) != 0) {
            options.putString(name, "key.keyboard." + old.substr(std::min<std::size_t>(4, old.size())));
        }
    }
}

// parity: `OptionsAddTextBackgroundFix`.
void addTextBackground(NbtCompound& options, const Context&) {
    if (const std::optional<std::string> opacity = stringOf(options.find("chatOpacity")); opacity.has_value()) {
        const std::optional<double> parsed = parseDouble(*opacity);
        const double background = parsed.has_value() ? (0.9 * *parsed + 0.1) / 2.0 : 0.5;
        options.putString("textBackgroundOpacity", javaDouble(background));
    }
}

// parity: `OptionsFancyGraphicsToGraphicsModeFix`.
void fancyGraphicsToMode(NbtCompound& options, const Context&) {
    renameAndFix(options, "fancyGraphics", "graphicsMode", [](NbtTag& value) {
        const std::string text = stringOf(&value).value_or("true");
        value = NbtTag::ofString(text == "true" ? "1" : "0");
    });
}

// parity: `OptionsRenameFieldFix` for the swap hands key.
void renameSwapHands(NbtCompound& options, const Context&) {
    rename(options, "key_key.swapHands", "key_key.swapOffhand");
}

void replaceProgrammerArt(NbtCompound& options, std::string_view key) {
    if (std::optional<std::string> text = stringOf(options.find(key)); text.has_value()) {
        constexpr std::string_view kFrom = "\"programer_art\"";
        constexpr std::string_view kTo = "\"programmer_art\"";
        for (std::size_t at = text->find(kFrom); at != std::string::npos; at = text->find(kFrom, at + kTo.size())) {
            text->replace(at, kFrom.size(), kTo);
        }
        options.putString(key, *text);
    }
}

// parity: `OptionsProgrammerArtFix`.
void programmerArt(NbtCompound& options, const Context&) {
    replaceProgrammerArt(options, "resourcePacks");
    replaceProgrammerArt(options, "incompatibleResourcePacks");
}

// parity: `OptionsAmbientOcclusionFix`.
void ambientOcclusion(NbtCompound& options, const Context&) {
    if (const std::optional<std::string> value = stringOf(options.find("ao")); value.has_value()) {
        options.putString("ao", *value == "0" ? "false" : (*value == "1" || *value == "2" ? "true" : *value));
    }
}

// parity: `OptionsAccessibilityOnboardFix`.
void accessibilityOnboard(NbtCompound& options, const Context&) { options.putString("onboardAccessibility", "false"); }

// parity: `OptionsMenuBlurrinessFix`.
void menuBlurriness(NbtCompound& options, const Context&) {
    if (!options.contains("menuBackgroundBlurriness")) {
        return;
    }
    const std::string text = valueOf(options, "menuBackgroundBlurriness", "0.5");
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    std::int32_t blurriness = 5;
    if (!text.empty() && end == text.c_str() + text.size()) {
        blurriness = std::isnan(parsed) ? 0 : static_cast<std::int32_t>(std::floor(parsed * 10.0F + 0.5F));
    }
    options.putString("menuBackgroundBlurriness", std::to_string(blurriness));
}

void splitGraphicsMode(NbtCompound& options, std::string_view field, std::string_view fast, std::string_view fancy,
                       std::string_view fabulous) {
    const std::optional<std::string> mode = stringOf(options.find("graphicsMode"));
    std::string_view value = fancy;
    if (mode.has_value()) {
        value = *mode == "2" ? fabulous : (*mode == "0" ? fast : fancy);
    }
    options.putString(field, std::string{value});
}

// parity: `OptionsGraphicsModeSplitFix` for the cutout leaves.
void splitCutoutLeaves(NbtCompound& options, const Context&) {
    splitGraphicsMode(options, "cutoutLeaves", "false", "true", "true");
}

// parity: `OptionsGraphicsModeSplitFix` for the weather radius.
void splitWeatherRadius(NbtCompound& options, const Context&) {
    splitGraphicsMode(options, "weatherRadius", "5", "10", "10");
}

// parity: `OptionsGraphicsModeSplitFix` for the vignette.
void splitVignette(NbtCompound& options, const Context&) {
    splitGraphicsMode(options, "vignette", "false", "true", "true");
}

// parity: `OptionsGraphicsModeSplitFix` for the improved transparency.
void splitImprovedTransparency(NbtCompound& options, const Context&) {
    splitGraphicsMode(options, "improvedTransparency", "false", "false", "true");
}

// parity: `OptionsSetGraphicsPresetToCustomFix`.
void graphicsPresetCustom(NbtCompound& options, const Context&) { options.putString("graphicsPreset", "custom"); }

// parity: `OptionsMusicToastFix`.
void musicToast(NbtCompound& options, const Context&) {
    renameAndFix(options, "showNowPlayingToast", "musicToast", [](NbtTag& value) {
        value = NbtTag::ofString(stringOf(&value).value_or("false") == "false" ? "never" : "pause_and_toast");
    });
}

// parity: `OptionsForceDefaultGraphicsApiFix`.
void defaultGraphicsApi(NbtCompound& options, const Context&) {
    options.putString("preferredGraphicsBackend", "default");
}

// parity: `EmptyItemInHotbarFix`.
void emptyItemInHotbar(NbtCompound& hotbar, const Context&) {
    for (const std::string& name : hotbar.names()) {
        NbtList* row = listAt(hotbar, name);
        for (std::size_t i = 0; row != nullptr && i < row->size(); ++i) {
            NbtCompound* stack = (*row)[i].get<NbtCompound>();
            if (stack == nullptr) {
                continue;
            }
            const std::optional<std::string> id = stringOf(stack->find("id"));
            const bool air = !id.has_value() || *id == "minecraft:air";
            const bool empty = whole(stack->find("Count")).value_or(0) <= 0;
            if (air || empty) {
                *stack = NbtCompound{};
            }
        }
    }
}

// parity: `DebugProfileOverlayReferenceFix`.
void overlayReference(NbtCompound& profile, const Context&) {
    NbtCompound* custom = compoundAt(profile, "custom");
    for (std::size_t i = 0; custom != nullptr && i < custom->size(); ++i) {
        if (const std::string* text = custom->values()[i].get<std::string>(); text != nullptr && *text == "inF3") {
            custom->putString(custom->names()[i], "inOverlay");
        }
    }
}

// parity: `DebugProfileLookingAtSplitFix`.
void lookingAtSplit(NbtCompound& profile, const Context&) {
    NbtCompound* custom = compoundAt(profile, "custom");
    if (custom == nullptr) {
        return;
    }
    NbtCompound options;
    for (std::size_t i = 0; i < custom->size(); ++i) {
        const std::string& key = custom->names()[i];
        const NbtTag& value = custom->values()[i];
        const std::string renamed = key == "minecraft:looking_at_block"   ? "minecraft:looking_at_block_state"
                                    : key == "minecraft:looking_at_fluid" ? "minecraft:looking_at_fluid_state"
                                                                          : std::string{};
        if (renamed.empty()) {
            options.set(key, value);
        } else if (!options.contains(renamed)) {
            options.set(renamed, value);
        }
    }
    *custom = std::move(options);
}

// parity: `DebugProfileRenameEntryFix` for the post effect entry.
void renamePostEffect(NbtCompound& profile, const Context&) {
    if (NbtCompound* custom = compoundAt(profile, "custom"); custom != nullptr) {
        rename(*custom, "minecraft:post_effect", "minecraft:post_effects");
    }
}

} // namespace

void addOptionsFixes(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(),
                 {
                     {505, Target::Root, {}, forceVbo, "OptionsForceVBOFix", kOptions},
                     {816, Target::Root, {}, lowerCaseLanguage, "OptionsLowerCaseLanguageFix", kOptions},
                     {1344, Target::Root, {}, lwjgl3Keys, "OptionsKeyLwjgl3Fix", kOptions},
                     {1446, Target::Root, {}, keyTranslation, "OptionsKeyTranslationFix", kOptions},
                     {1936, Target::Root, {}, addTextBackground, "OptionsAddTextBackgroundFix", kOptions},
                     {2556, Target::Root, {}, fancyGraphicsToMode, "OptionsFancyGraphicsToGraphicsModeFix", kOptions},
                     {2558, Target::Root, {}, renameSwapHands, "OptionsRenameFieldFix", kOptions},
                     {3201, Target::Root, {}, programmerArt, "OptionsProgrammerArtFix", kOptions},
                     {3214, Target::Root, {}, ambientOcclusion, "OptionsAmbientOcclusionFix", kOptions},
                     {3319, Target::Root, {}, accessibilityOnboard, "OptionsAccessibilityOnboardFix", kOptions},
                     {3818, Target::Root, {}, emptyItemInHotbar, "EmptyItemInHotbarFix", kHotbar},
                     {3943, Target::Root, {}, menuBlurriness, "OptionsMenuBlurrinessFix", kOptions},
                     {4650, Target::Root, {}, overlayReference, "DebugProfileOverlayReferenceFix", kDebug},
                     {4651, Target::Root, {}, splitCutoutLeaves, "OptionsGraphicsModeSplitFix", kOptions},
                     {4651, Target::Root, {}, splitWeatherRadius, "OptionsGraphicsModeSplitFix", kOptions},
                     {4651, Target::Root, {}, splitVignette, "OptionsGraphicsModeSplitFix", kOptions},
                     {4651, Target::Root, {}, splitImprovedTransparency, "OptionsGraphicsModeSplitFix", kOptions},
                     {4651, Target::Root, {}, graphicsPresetCustom, "OptionsSetGraphicsPresetToCustomFix", kOptions},
                     {4661, Target::Root, {}, musicToast, "OptionsMusicToastFix", kOptions},
                     {4763, Target::Root, {}, lookingAtSplit, "DebugProfileLookingAtSplitFix", kDebug},
                     {4892, Target::Root, {}, defaultGraphicsApi, "OptionsForceDefaultGraphicsApiFix", kOptions},
                     {5001, Target::Root, {}, renamePostEffect, "DebugProfileRenameEntryFix", kDebug},
                 });
}

} // namespace basalt::metamorph::datafix
