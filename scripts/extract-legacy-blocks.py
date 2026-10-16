#!/usr/bin/env python3
"""Writes metamorph-datafix/src/LegacyBlocks.hpp from the block state table of the vanilla data fixer sources.
usage: extract-legacy-blocks.py <path to net/minecraft/util/datafix/fixes>"""
import re
import sys
from pathlib import Path

source = (Path(sys.argv[1]) / "BlockStateData.java").read_text()

constants = {}
for name, body in re.findall(r"private static final Map<String, String> (\w+) = Map\.of\((.*?)\);", source):
    strings = re.findall(r'"([^"]*)"', body)
    constants[name] = list(zip(strings[0::2], strings[1::2]))


def split_top_level(text):
    parts, depth, current, quoted = [], 0, "", False
    for ch in text:
        if ch == '"':
            quoted = not quoted
        if not quoted:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                parts.append(current.strip())
                current = ""
                continue
        current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def parse_create(call):
    inner = call[len("create("):-1]
    parts = split_top_level(inner)
    name = parts[0].strip('"')
    properties = []
    if len(parts) > 1:
        argument = parts[1]
        if argument.startswith("Map.of("):
            strings = re.findall(r'"([^"]*)"', argument)
            properties = list(zip(strings[0::2], strings[1::2]))
        else:
            properties = constants[argument]
    return name, ",".join(f"{k}={v}" for k, v in sorted(properties))


rows = []
position = 0
while True:
    found = source.find("        register(", position)
    if found < 0:
        break
    start = found + len("        register(")
    depth, end = 1, start
    quoted = False
    while depth:
        ch = source[end]
        if ch == '"':
            quoted = not quoted
        if not quoted:
            depth += (ch == "(") - (ch == ")")
        end += 1
    arguments = split_top_level(source[start:end - 1])
    position = end
    identifier = int(arguments[0])
    now = parse_create(arguments[1])
    olds = [parse_create(a) for a in arguments[2:]]
    if not olds:
        rows.append((identifier, now, ("", "")))
    for old in olds:
        rows.append((identifier, now, old))

out = [
    "// Generated from the vanilla sources by scripts/extract-legacy-blocks.py. Do not edit.",
    "#pragma once",
    "",
    "#include <array>",
    "#include <cstdint>",
    "#include <string_view>",
    "",
    "namespace basalt::metamorph::datafix::legacy {",
    "",
    "/// One block state of the game before 1.13: `id` is `block << 4 | data`, `name` and `properties` are what the",
    "/// state became, `oldName` and `oldProperties` are how that same state was named before the flattening.",
    "struct StateRow {",
    "    std::int32_t id;",
    "    std::string_view name;",
    "    std::string_view properties;",
    "    std::string_view oldName;",
    "    std::string_view oldProperties;",
    "};",
    "",
    f"inline const std::array<StateRow, {len(rows)}> kStates{{{{",
]
for identifier, (name, props), (old_name, old_props) in rows:
    out.append(f'    {{{identifier}, "{name}", "{props}", "{old_name}", "{old_props}"}},')
out.append("}};")
out.append("")
out.append("} // namespace basalt::metamorph::datafix::legacy")
target = Path(__file__).resolve().parent.parent / "metamorph-datafix" / "src" / "LegacyBlocks.hpp"
target.write_text("\n".join(out) + "\n")
print(len(rows), "rows")
