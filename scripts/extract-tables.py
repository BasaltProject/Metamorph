#!/usr/bin/env python3
"""Writes metamorph-datafix/src/Tables.hpp from the name tables in the vanilla data fixer sources.
usage: extract-tables.py <path to net/minecraft/util/datafix>"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
fixes = (root / "fixes")
height = (fixes / "ChunkHeightAndBiomeFix.java").read_text()
biomes = re.findall(r'BIOMES_BY_ID\.put\((\d+), "([^"]+)"\)', height)
caves = re.findall(r'put\("([^"]+)", "([^"]+)"\)', (fixes / "CavesAndCliffsRenames.java").read_text())
fixers = (root / "DataFixers.java").read_text()


def builder(name: str):
    block = re.search(name + r' = ImmutableMap\.<String, String>builder\(\)(.*?)\.build\(\);', fixers, re.S)
    return re.findall(r'put\("([^"]+)", "([^"]+)"\)', block.group(1))


tables = [
    ("kCavesAndCliffsBiomes", caves),
    ("kCopperOxidation", builder("copperBlocksToRename")),
    ("kWaxedCopperSuffixes", builder("waxedCopperRename")),
    ("kGrimstoneToDeepslate", builder("grimstoneBlocksToRename")),
]
out = [
    "// Generated from the vanilla sources by scripts/extract-tables.py. Do not edit.",
    "#pragma once",
    "",
    "#include <array>",
    "#include <cstdint>",
    "#include <string_view>",
    "",
    "namespace basalt::metamorph::datafix::tables {",
    "",
    "struct Pair {",
    "    std::string_view from;",
    "    std::string_view to;",
    "};",
    "",
    "struct NumberedName {",
    "    std::int32_t id;",
    "    std::string_view name;",
    "};",
    "",
    f"inline constexpr std::array<NumberedName, {len(biomes)}> kBiomesById{{{{",
]
out += [f'    {{{i}, "{n}"}},' for i, n in biomes]
out.append("}};")
out.append("")
for name, pairs in tables:
    out.append(f"inline constexpr std::array<Pair, {len(pairs)}> {name}{{{{")
    out += [f'    {{"{a}", "{b}"}},' for a, b in pairs]
    out.append("}};")
    out.append("")
out.append("} // namespace basalt::metamorph::datafix::tables")
target = Path(__file__).resolve().parent.parent / "metamorph-datafix" / "src" / "Tables.hpp"
target.write_text("\n".join(out) + "\n")
