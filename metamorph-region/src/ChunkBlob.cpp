#include <basalt/metamorph/region/ChunkBlob.hpp>
#include <basalt/wire/nbt/NbtIO.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <unordered_map>

namespace basalt::metamorph::region::chunkBlob {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr std::size_t kMaxPalette = 4096;
constexpr std::uint8_t kHasExtras = 1;

constexpr std::array<std::string_view, 7> kHeaderKeys = {"DataVersion",   "xPos",   "yPos",    "zPos",
                                                         "InhabitedTime", "Status", "sections"};

[[nodiscard]] bool isHeaderKey(std::string_view key) {
    return std::ranges::find(kHeaderKeys, key) != kHeaderKeys.end();
}

[[nodiscard]] std::vector<std::uint8_t> anonymous(const NbtCompound& compound) {
    return wire::nbt::nbtIO::writeAnonymous(NbtTag::ofCompound(compound));
}

[[nodiscard]] std::optional<NbtCompound> compoundOf(std::span<const std::uint8_t> bytes) {
    const auto read = wire::nbt::nbtIO::readAnonymous(bytes);
    if (!read.has_value()) {
        return std::nullopt;
    }
    const NbtCompound* compound = read->tag.get<NbtCompound>();
    return compound == nullptr ? std::nullopt : std::optional{*compound};
}

/// Entries run across word borders, which is how the paletted containers hold them in memory.
[[nodiscard]] std::vector<std::uint32_t> unpackStraddled(std::span<const std::uint8_t> bytes, int bits,
                                                         std::size_t cells) {
    std::vector<std::uint32_t> values(cells);
    const auto width = static_cast<unsigned>(bits);
    const std::uint64_t mask = (std::uint64_t{1} << width) - 1;
    unsigned __int128 buffer = 0;
    unsigned buffered = 0;
    std::size_t next = 0;
    for (std::size_t i = 0; i < cells; ++i) {
        if (buffered < width) {
            std::uint64_t word = 0;
            std::memcpy(&word, bytes.data() + next * 8, sizeof word);
            ++next;
            buffer |= static_cast<unsigned __int128>(word) << buffered;
            buffered += 64;
        }
        values[i] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(buffer) & mask);
        buffer >>= width;
        buffered -= width;
    }
    return values;
}

[[nodiscard]] std::vector<std::uint64_t> packStraddled(std::span<const std::uint32_t> values, int bits) {
    std::vector<std::uint64_t> words(packedWordCount(values.size(), bits), 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const std::size_t bitIndex = i * static_cast<std::size_t>(bits);
        const std::size_t word = bitIndex >> 6;
        const unsigned offset = static_cast<unsigned>(bitIndex & 63);
        words[word] |= static_cast<std::uint64_t>(values[i]) << offset;
        if (offset + static_cast<unsigned>(bits) > 64) {
            words[word + 1] |= static_cast<std::uint64_t>(values[i]) >> (64 - offset);
        }
    }
    return words;
}

/// The vanilla NBT layout: whole entries per long, none split between two.
[[nodiscard]] std::vector<std::int64_t> packVanilla(std::span<const std::uint32_t> values, int bits) {
    const std::size_t perLong = 64 / static_cast<std::size_t>(bits);
    std::vector<std::int64_t> longs((values.size() + perLong - 1) / perLong, 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        longs[i / perLong] |= static_cast<std::int64_t>(static_cast<std::uint64_t>(values[i])
                                                        << ((i % perLong) * static_cast<std::size_t>(bits)));
    }
    return longs;
}

[[nodiscard]] std::optional<std::vector<std::uint32_t>> unpackVanilla(std::span<const std::int64_t> longs, int bits,
                                                                      std::size_t cells) {
    const std::size_t perLong = 64 / static_cast<std::size_t>(bits);
    if (longs.size() != (cells + perLong - 1) / perLong) {
        return std::nullopt;
    }
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
    std::vector<std::uint32_t> values(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        values[i] = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(longs[i / perLong]) >> ((i % perLong) * static_cast<std::size_t>(bits))) &
            mask);
    }
    return values;
}

/// The canonical text of a vanilla palette entry; empty when the entry holds something the blob cannot keep.
[[nodiscard]] std::optional<std::string> blockEntryText(const NbtTag& entry) {
    const NbtCompound* compound = entry.get<NbtCompound>();
    if (compound == nullptr) {
        return std::nullopt;
    }
    StateText state;
    for (std::size_t i = 0; i < compound->size(); ++i) {
        const std::string& key = compound->names()[i];
        const NbtTag& value = compound->values()[i];
        if (key == "id" && value.get<std::string>() != nullptr) {
            state.id = *value.get<std::string>();
        } else if (key == "properties" && value.get<NbtCompound>() != nullptr) {
            const NbtCompound& properties = *value.get<NbtCompound>();
            for (std::size_t p = 0; p < properties.size(); ++p) {
                const std::string* text = properties.values()[p].get<std::string>();
                if (text == nullptr || properties.names()[p].find_first_of("[],=") != std::string::npos ||
                    text->find_first_of("[],=") != std::string::npos) {
                    return std::nullopt;
                }
                state.properties.emplace(properties.names()[p], *text);
            }
        } else {
            return std::nullopt;
        }
    }
    if (state.id.empty() || state.id.find_first_of("[],=") != std::string::npos) {
        return std::nullopt;
    }
    return formatState(state);
}

[[nodiscard]] std::optional<std::string> biomeEntryText(const NbtTag& entry) {
    const std::string* name = entry.get<std::string>();
    if (name == nullptr || name->empty() || name->find_first_of("[],=") != std::string::npos) {
        return std::nullopt;
    }
    return *name;
}

[[nodiscard]] NbtTag blockEntryTag(std::string_view text) {
    const std::optional<StateText> state = parseState(text);
    NbtCompound entry;
    entry.putString("id", state.has_value() ? state->id : std::string{text});
    if (state.has_value() && !state->properties.empty()) {
        NbtCompound properties;
        for (const auto& [name, value] : state->properties) {
            properties.putString(name, value);
        }
        entry.set("properties", NbtTag::ofCompound(std::move(properties)));
    }
    return NbtTag::ofCompound(std::move(entry));
}

class Names {
public:
    [[nodiscard]] std::uint32_t refOf(const std::string& text) {
        const auto [at, fresh] = index_.try_emplace(text, static_cast<std::uint32_t>(names_.size()));
        if (fresh) {
            names_.push_back(text);
        }
        return at->second;
    }

    [[nodiscard]] const std::vector<std::string>& all() const noexcept { return names_; }

private:
    std::unordered_map<std::string, std::uint32_t> index_;
    std::vector<std::string> names_;
};

struct PaletteSpec {
    std::size_t cells = 0;
    int minBits = 0;
    int maxIndirectBits = 0;
    bool block = false;
};

/// Reads one NBT palette container into blob form; false when the NBT holds more than the blob can keep.
[[nodiscard]] bool encodePalette(const NbtCompound& container, const PaletteSpec& spec, Names& names, ByteWriter& out) {
    for (const std::string& key : container.names()) {
        if (key != "palette" && key != "data") {
            return false;
        }
    }
    const NbtList* palette = container.getList("palette");
    if (palette == nullptr || palette->empty() || palette->size() > kMaxPalette) {
        return false;
    }
    std::vector<std::uint32_t> refs;
    refs.reserve(palette->size());
    for (const NbtTag& entry : *palette) {
        const std::optional<std::string> text = spec.block ? blockEntryText(entry) : biomeEntryText(entry);
        if (!text.has_value()) {
            return false;
        }
        refs.push_back(names.refOf(*text));
    }
    const NbtTag* data = container.find("data");
    if (palette->size() == 1) {
        if (data != nullptr) {
            return false;
        }
        writePalette(out, refs, 0, {});
        return true;
    }
    const int bits = std::max(bitsFor(palette->size()), spec.minBits);
    const auto* longs = data == nullptr ? nullptr : data->get<std::vector<std::int64_t>>();
    if (longs == nullptr || bits > spec.maxIndirectBits) {
        return false;
    }
    const std::optional<std::vector<std::uint32_t>> indices = unpackVanilla(*longs, bits, spec.cells);
    if (!indices.has_value() ||
        std::ranges::any_of(*indices, [&](std::uint32_t index) { return index >= palette->size(); })) {
        return false;
    }
    writePalette(out, refs, bits, packStraddled(*indices, bits));
    return true;
}

[[nodiscard]] std::optional<NbtCompound> decodePalette(const Palette& palette, const PaletteSpec& spec,
                                                       std::span<const std::string_view> names) {
    NbtList entries{spec.block ? NbtType::Compound : NbtType::String, {}};
    for (const std::uint32_t ref : palette.refs) {
        if (ref >= names.size()) {
            return std::nullopt;
        }
        entries.push(spec.block ? blockEntryTag(names[ref]) : NbtTag::ofString(std::string{names[ref]}));
    }
    NbtCompound container;
    container.set("palette", NbtTag::ofList(std::move(entries)));
    if (palette.refs.size() > 1) {
        const int bits = std::max(bitsFor(palette.refs.size()), spec.minBits);
        if (bits > spec.maxIndirectBits) {
            return std::nullopt;
        }
        const std::vector<std::uint32_t> indices = unpackStraddled(palette.words, palette.bits, spec.cells);
        if (std::ranges::any_of(indices, [&](std::uint32_t index) { return index >= palette.refs.size(); })) {
            return std::nullopt;
        }
        container.putLongArray("data", packVanilla(indices, bits));
    }
    return container;
}

constexpr PaletteSpec kBlocks{kBlockCells, kBlockMinBits, kBlockMaxIndirectBits, true};
constexpr PaletteSpec kBiomes{kBiomeCells, kBiomeMinBits, kBiomeMaxIndirectBits, false};

[[nodiscard]] bool readPalette(ByteReader& in, std::size_t cells, Palette& out) {
    const std::uint32_t count = in.varint();
    if (in.failed() || count == 0 || count > kMaxPalette) {
        return false;
    }
    out.refs.resize(count);
    for (std::uint32_t& ref : out.refs) {
        ref = in.varint();
    }
    out.bits = 0;
    out.words = {};
    if (count > 1) {
        out.bits = in.u8();
        if (out.bits < bitsFor(count) || out.bits > 32) {
            return false;
        }
        out.words = in.take(packedWordCount(cells, out.bits) * 8);
    }
    return !in.failed();
}

/// Cell by cell, with the palette entries spelled out: the part of a section that must match to be the same section.
[[nodiscard]] std::optional<std::vector<std::string>> cellsOf(const NbtCompound& container, const PaletteSpec& spec) {
    const NbtList* palette = container.getList("palette");
    if (palette == nullptr || palette->empty()) {
        return std::nullopt;
    }
    std::vector<std::string> texts;
    for (const NbtTag& entry : *palette) {
        std::optional<std::string> text = spec.block ? blockEntryText(entry) : biomeEntryText(entry);
        if (!text.has_value()) {
            return std::nullopt;
        }
        texts.push_back(*std::move(text));
    }
    if (texts.size() == 1) {
        return std::vector<std::string>(spec.cells, texts[0]);
    }
    const auto* longs =
        container.find("data") == nullptr ? nullptr : container.find("data")->get<std::vector<std::int64_t>>();
    if (longs == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::uint32_t>> indices =
        unpackVanilla(*longs, std::max(bitsFor(texts.size()), spec.minBits), spec.cells);
    if (!indices.has_value() ||
        std::ranges::any_of(*indices, [&](std::uint32_t index) { return index >= texts.size(); })) {
        return std::nullopt;
    }
    std::vector<std::string> cells;
    cells.reserve(spec.cells);
    for (const std::uint32_t index : *indices) {
        cells.push_back(texts[index]);
    }
    return cells;
}

[[nodiscard]] bool deepEqual(const NbtTag& a, const NbtTag& b);

[[nodiscard]] bool compoundsEqual(const NbtCompound& a, const NbtCompound& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const NbtTag* other = b.find(a.names()[i]);
        if (other == nullptr || !deepEqual(a.values()[i], *other)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool deepEqual(const NbtTag& a, const NbtTag& b) {
    if (a.type() != b.type()) {
        return false;
    }
    if (const NbtCompound* left = a.get<NbtCompound>()) {
        return compoundsEqual(*left, *b.get<NbtCompound>());
    }
    if (const NbtList* left = a.get<NbtList>()) {
        const NbtList& right = *b.get<NbtList>();
        if (left->size() != right.size() || (left->size() != 0 && left->elementType() != right.elementType())) {
            return false;
        }
        for (std::size_t i = 0; i < left->size(); ++i) {
            if (!deepEqual((*left)[i], right[i])) {
                return false;
            }
        }
        return true;
    }
    if (const float* left = a.get<float>()) {
        return std::bit_cast<std::uint32_t>(*left) == std::bit_cast<std::uint32_t>(*b.get<float>());
    }
    if (const double* left = a.get<double>()) {
        return std::bit_cast<std::uint64_t>(*left) == std::bit_cast<std::uint64_t>(*b.get<double>());
    }
    return a == b;
}

[[nodiscard]] bool containersEqual(const NbtCompound& a, const NbtCompound& b, const PaletteSpec& spec) {
    const std::optional<std::vector<std::string>> left = cellsOf(a, spec);
    const std::optional<std::vector<std::string>> right = cellsOf(b, spec);
    if (!left.has_value() || !right.has_value()) {
        return compoundsEqual(a, b);
    }
    return *left == *right;
}

[[nodiscard]] bool sectionsEqual(const NbtCompound& a, const NbtCompound& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const std::string& key = a.names()[i];
        const NbtTag* other = b.find(key);
        if (other == nullptr) {
            return false;
        }
        const NbtCompound* left = a.values()[i].get<NbtCompound>();
        const NbtCompound* right = other->get<NbtCompound>();
        if ((key == "block_states" || key == "biomes") && left != nullptr && right != nullptr) {
            if (!containersEqual(*left, *right, key == "biomes" ? kBiomes : kBlocks)) {
                return false;
            }
        } else if (!deepEqual(a.values()[i], *other)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string formatState(const StateText& state) {
    std::string text = state.id;
    if (!state.properties.empty()) {
        text += '[';
        bool first = true;
        for (const auto& [name, value] : state.properties) {
            if (!first) {
                text += ',';
            }
            first = false;
            text += name;
            text += '=';
            text += value;
        }
        text += ']';
    }
    return text;
}

std::optional<StateText> parseState(std::string_view text) {
    StateText state;
    const std::size_t open = text.find('[');
    state.id = std::string{text.substr(0, open)};
    if (open == std::string_view::npos) {
        return state;
    }
    if (text.back() != ']') {
        return std::nullopt;
    }
    std::string_view rest = text.substr(open + 1, text.size() - open - 2);
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string_view pair = rest.substr(0, comma);
        const std::size_t equals = pair.find('=');
        if (equals == std::string_view::npos) {
            return std::nullopt;
        }
        state.properties.emplace(std::string{pair.substr(0, equals)}, std::string{pair.substr(equals + 1)});
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
    }
    return state;
}

void writePalette(ByteWriter& out, std::span<const std::uint32_t> refs, int bits,
                  std::span<const std::uint64_t> words) {
    out.varint(static_cast<std::uint32_t>(refs.size()));
    for (const std::uint32_t ref : refs) {
        out.varint(ref);
    }
    if (refs.size() > 1) {
        out.u8(static_cast<std::uint8_t>(bits));
        out.words(words);
    }
}

void assemble(std::vector<std::uint8_t>& out, const Header& header, std::span<const std::string> names,
              std::span<const SectionSpan> sections, std::span<const std::uint8_t> payloads,
              std::span<const std::uint8_t> rest) {
    out.reserve(out.size() + 64 + payloads.size() + rest.size() + sections.size() * 5 + names.size() * 24);
    ByteWriter writer{out};
    writer.u8(kMagic);
    writer.u8(kVersion);
    writer.u16(static_cast<std::uint16_t>(sections.size()));
    writer.i32(header.dataVersion);
    writer.i32(header.xPos);
    writer.i32(header.zPos);
    writer.i32(header.minSection);
    writer.i64(header.inhabitedTime);
    writer.u8(static_cast<std::uint8_t>(header.status.size()));
    writer.text(header.status);
    writer.u32(static_cast<std::uint32_t>(rest.size()));
    writer.varint(static_cast<std::uint32_t>(names.size()));
    for (const std::string& name : names) {
        writer.varint(static_cast<std::uint32_t>(name.size()));
        writer.text(name);
    }
    for (const SectionSpan& section : sections) {
        writer.u8(static_cast<std::uint8_t>(section.y));
        writer.u32(static_cast<std::uint32_t>(section.length));
    }
    writer.bytes(payloads);
    writer.bytes(rest);
}

std::optional<View> parse(std::span<const std::uint8_t> bytes) {
    ByteReader in{bytes};
    View view;
    if (in.u8() != kMagic || in.u8() != kVersion) {
        return std::nullopt;
    }
    const std::uint16_t sectionCount = in.u16();
    view.header.dataVersion = in.i32();
    view.header.xPos = in.i32();
    view.header.zPos = in.i32();
    view.header.minSection = in.i32();
    view.header.inhabitedTime = in.i64();
    const std::size_t statusLength = in.u8();
    const std::span<const std::uint8_t> status = in.take(statusLength);
    const std::uint32_t restLength = in.u32();
    const std::uint32_t nameCount = in.varint();
    if (in.failed() || nameCount > bytes.size()) {
        return std::nullopt;
    }
    view.header.status = std::string_view{reinterpret_cast<const char*>(status.data()), status.size()};
    view.names.reserve(nameCount);
    for (std::uint32_t i = 0; i < nameCount; ++i) {
        const std::span<const std::uint8_t> name = in.take(in.varint());
        view.names.emplace_back(reinterpret_cast<const char*>(name.data()), name.size());
    }
    std::vector<std::pair<std::int8_t, std::uint32_t>> directory(sectionCount);
    for (auto& [y, length] : directory) {
        y = static_cast<std::int8_t>(in.u8());
        length = in.u32();
    }
    if (in.failed()) {
        return std::nullopt;
    }
    view.sections.reserve(sectionCount);
    for (const auto& [y, length] : directory) {
        view.sections.push_back({y, in.take(length)});
    }
    view.rest = in.take(restLength);
    if (in.failed() || in.remaining() != 0) {
        return std::nullopt;
    }
    return view;
}

bool parseSection(std::span<const std::uint8_t> payload, SectionPayload& out) {
    ByteReader in{payload};
    const std::uint8_t flags = in.u8();
    if (!readPalette(in, kBlockCells, out.blocks) || !readPalette(in, kBiomeCells, out.biomes)) {
        return false;
    }
    out.extras = {};
    if ((flags & kHasExtras) != 0) {
        out.extras = in.take(in.varint());
    }
    return !in.failed() && in.remaining() == 0;
}

std::optional<std::vector<std::uint8_t>> fromNbt(const NbtCompound& root, std::int32_t dataVersion) {
    const auto intOf = [&](std::string_view key) -> std::optional<std::int32_t> {
        const NbtTag* tag = root.find(key);
        return tag != nullptr && tag->get<std::int32_t>() != nullptr ? std::optional{*tag->get<std::int32_t>()}
                                                                     : std::nullopt;
    };
    const std::optional<std::int32_t> version = intOf("DataVersion");
    const std::optional<std::int32_t> x = intOf("xPos");
    const std::optional<std::int32_t> y = intOf("yPos");
    const std::optional<std::int32_t> z = intOf("zPos");
    const NbtTag* inhabited = root.find("InhabitedTime");
    const NbtTag* status = root.find("Status");
    const NbtList* sections = root.getList("sections");
    if (version != dataVersion || !x || !y || !z || inhabited == nullptr || inhabited->get<std::int64_t>() == nullptr ||
        status == nullptr || status->get<std::string>() == nullptr || status->get<std::string>()->size() > 255 ||
        sections == nullptr || sections->size() > 0xFFFF ||
        (!sections->empty() && sections->elementType() != NbtType::Compound)) {
        return std::nullopt;
    }
    Names names;
    std::vector<std::uint8_t> payloads;
    std::vector<SectionSpan> spans;
    for (const NbtTag& entry : *sections) {
        const NbtCompound& section = *entry.get<NbtCompound>();
        const NbtTag* sectionY = section.find("Y");
        const NbtCompound* blockStates = section.getCompound("block_states");
        const NbtCompound* biomes = section.getCompound("biomes");
        if (sectionY == nullptr || sectionY->get<std::int8_t>() == nullptr || blockStates == nullptr ||
            biomes == nullptr) {
            return std::nullopt;
        }
        NbtCompound extras;
        for (std::size_t i = 0; i < section.size(); ++i) {
            const std::string& key = section.names()[i];
            if (key != "Y" && key != "block_states" && key != "biomes") {
                extras.set(key, section.values()[i]);
            }
        }
        const std::size_t offset = payloads.size();
        ByteWriter out{payloads};
        out.u8(extras.empty() ? 0 : kHasExtras);
        if (!encodePalette(*blockStates, kBlocks, names, out) || !encodePalette(*biomes, kBiomes, names, out)) {
            return std::nullopt;
        }
        if (!extras.empty()) {
            const std::vector<std::uint8_t> bytes = anonymous(extras);
            out.varint(static_cast<std::uint32_t>(bytes.size()));
            out.bytes(bytes);
        }
        spans.push_back({*sectionY->get<std::int8_t>(), offset, payloads.size() - offset});
    }
    NbtCompound rest;
    for (std::size_t i = 0; i < root.size(); ++i) {
        if (!isHeaderKey(root.names()[i])) {
            rest.set(root.names()[i], root.values()[i]);
        }
    }
    std::vector<std::uint8_t> restBytes;
    if (!rest.empty()) {
        restBytes = anonymous(rest);
    }
    std::vector<std::uint8_t> blob;
    assemble(blob, {*version, *x, *z, *y, *inhabited->get<std::int64_t>(), *status->get<std::string>()}, names.all(),
             spans, payloads, restBytes);
    return blob;
}

std::optional<NbtCompound> toNbt(std::span<const std::uint8_t> blob) {
    const std::optional<View> view = parse(blob);
    if (!view.has_value()) {
        return std::nullopt;
    }
    NbtCompound root;
    root.putInt("DataVersion", view->header.dataVersion);
    root.putInt("xPos", view->header.xPos);
    root.putInt("yPos", view->header.minSection);
    root.putInt("zPos", view->header.zPos);
    root.putLong("InhabitedTime", view->header.inhabitedTime);
    root.putString("Status", std::string{view->header.status});
    std::vector<NbtTag> sections;
    sections.reserve(view->sections.size());
    for (const SectionEntry& entry : view->sections) {
        SectionPayload payload;
        if (!parseSection(entry.payload, payload)) {
            return std::nullopt;
        }
        std::optional<NbtCompound> blocks = decodePalette(payload.blocks, kBlocks, view->names);
        std::optional<NbtCompound> biomes = decodePalette(payload.biomes, kBiomes, view->names);
        if (!blocks.has_value() || !biomes.has_value()) {
            return std::nullopt;
        }
        NbtCompound section;
        section.putByte("Y", entry.y);
        section.set("block_states", NbtTag::ofCompound(*std::move(blocks)));
        section.set("biomes", NbtTag::ofCompound(*std::move(biomes)));
        if (!payload.extras.empty()) {
            const std::optional<NbtCompound> extras = compoundOf(payload.extras);
            if (!extras.has_value()) {
                return std::nullopt;
            }
            for (std::size_t i = 0; i < extras->size(); ++i) {
                section.set(extras->names()[i], extras->values()[i]);
            }
        }
        sections.push_back(NbtTag::ofCompound(std::move(section)));
    }
    root.set("sections", NbtTag::ofList(NbtList{NbtType::Compound, std::move(sections)}));
    if (!view->rest.empty()) {
        const std::optional<NbtCompound> rest = compoundOf(view->rest);
        if (!rest.has_value()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < rest->size(); ++i) {
            root.set(rest->names()[i], rest->values()[i]);
        }
    }
    return root;
}

std::vector<std::uint8_t> encode(std::span<const std::uint8_t> nbtBytes, std::int32_t dataVersion) {
    const auto read = wire::nbt::nbtIO::read(nbtBytes);
    if (read.has_value()) {
        if (const NbtCompound* root = read->tag.get<NbtCompound>(); root != nullptr) {
            if (std::optional<std::vector<std::uint8_t>> blob = fromNbt(*root, dataVersion); blob.has_value()) {
                return *std::move(blob);
            }
        }
    }
    return {nbtBytes.begin(), nbtBytes.end()};
}

std::vector<std::uint8_t> decode(std::span<const std::uint8_t> stored) {
    if (!isBlob(stored)) {
        return {stored.begin(), stored.end()};
    }
    const std::optional<NbtCompound> root = toNbt(stored);
    return root.has_value() ? wire::nbt::nbtIO::write(NbtTag::ofCompound(*root)) : std::vector<std::uint8_t>{};
}

bool equivalent(std::span<const std::uint8_t> nbtA, std::span<const std::uint8_t> nbtB) {
    if (nbtA.size() == nbtB.size() && std::equal(nbtA.begin(), nbtA.end(), nbtB.begin())) {
        return true;
    }
    const auto a = wire::nbt::nbtIO::read(nbtA);
    const auto b = wire::nbt::nbtIO::read(nbtB);
    if (!a.has_value() || !b.has_value()) {
        return false;
    }
    const NbtCompound* left = a->tag.get<NbtCompound>();
    const NbtCompound* right = b->tag.get<NbtCompound>();
    if (left == nullptr || right == nullptr || left->size() != right->size()) {
        return false;
    }
    for (std::size_t i = 0; i < left->size(); ++i) {
        const NbtTag* other = right->find(left->names()[i]);
        if (other == nullptr) {
            return false;
        }
        if (left->names()[i] == "sections" && left->values()[i].get<NbtList>() != nullptr &&
            other->get<NbtList>() != nullptr) {
            const NbtList& sa = *left->values()[i].get<NbtList>();
            const NbtList& sb = *other->get<NbtList>();
            if (sa.size() != sb.size()) {
                return false;
            }
            for (std::size_t s = 0; s < sa.size(); ++s) {
                const NbtCompound* ca = sa[s].get<NbtCompound>();
                const NbtCompound* cb = sb[s].get<NbtCompound>();
                if (ca == nullptr || cb == nullptr ? !deepEqual(sa[s], sb[s]) : !sectionsEqual(*ca, *cb)) {
                    return false;
                }
            }
        } else if (!deepEqual(left->values()[i], *other)) {
            return false;
        }
    }
    return true;
}

} // namespace basalt::metamorph::region::chunkBlob
