#!/usr/bin/env python3
"""Writes metamorph-datafix/src/FlatteningTables.hpp from the item and entity tables of the 1.13 data fixers.
usage: extract-flattening.py <path to net/minecraft/util/datafix/fixes>"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
items = (root / "ItemStackTheFlatteningFix.java").read_text()
renamening = (root / "EntityTheRenameningFix.java").read_text()
codSalmon = (root / "EntityCodSalmonFix.java").read_text()
blockIdsSource = (root / "EntityBlockStateFix.java").read_text()
enchantments = (root / "ItemStackEnchantmentNamesFix.java").read_text()


def puts(text):
    return re.findall(r'put\("([^"]+)", "([^"]+)"\)', text)


flattening = re.findall(r'map\.put\("([^"]+)", "([^"]+)"\)', items)
damage = re.findall(r'"([^"]+)"', re.search(r"DAMAGE_IDS = Sets\.newHashSet\((.*?)\);", items, re.S).group(1))
ids = puts(re.search(r"RENAMED_IDS = (.*?)\.build\(\);", renamening, re.S).group(1))
blocks = puts(re.search(r"RENAMED_BLOCKS = (.*?)\.build\(\);", renamening, re.S).group(1))
extra_items = puts(re.search(r"RENAMED_ITEMS = (.*?)\.build\(\);", renamening, re.S).group(1))
cod_ids = puts(re.search(r"RENAMED_IDS = (.*?)\.build\(\);", codSalmon, re.S).group(1))
cod_eggs = puts(re.search(r"RENAMED_EGG_IDS = (.*?)\.build\(\);", codSalmon, re.S).group(1))

blockIds = re.findall(r'map\.put\("([^"]+)", (\d+)\)', blockIdsSource)
enchantIds = re.findall(r'map\.put\((\d+), "([^"]+)"\)', enchantments)

out = [
    "// Generated from the vanilla sources by scripts/extract-flattening.py. Do not edit.",
    "#pragma once",
    "",
    "#include <array>",
    "#include <string_view>",
    "#include <cstdint>",
    "",
    "namespace basalt::metamorph::datafix::flattening {",
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
]


def table(name, pairs):
    out.append(f"inline const std::array<Pair, {len(pairs)}> {name}{{{{")
    out.extend(f'    {{"{a}", "{b}"}},' for a, b in pairs)
    out.append("}};")
    out.append("")


table("kItems", flattening)
out.append(f"inline const std::array<std::string_view, {len(damage)}> kDamageItems{{{{")
out.extend(f'    "{name}",' for name in damage)
out.append("}};")
out.append("")
table("kEntityRenames", ids)
table("kBlockRenames", blocks)
table("kItemRenames", blocks + extra_items)
table("kCodSalmonEntities", cod_ids)
table("kCodSalmonEggs", cod_eggs)
out.append(f"inline const std::array<NumberedName, {len(blockIds)}> kBlockIds{{{{")
out.extend(f'    {{{int(i)}, "{n}"}},' for n, i in blockIds)
out.append("}};")
out.append("")
out.append(f"inline const std::array<NumberedName, {len(enchantIds)}> kEnchantments{{{{")
out.extend(f'    {{{int(i)}, "{n}"}},' for i, n in enchantIds)
out.append("}};")
out.append("")
out.append("} // namespace basalt::metamorph::datafix::flattening")
target = Path(__file__).resolve().parent.parent / "metamorph-datafix" / "src" / "FlatteningTables.hpp"
target.write_text("\n".join(out) + "\n")
print(len(flattening), len(damage), len(ids), len(blocks), len(extra_items))
