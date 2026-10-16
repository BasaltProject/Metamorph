#!/usr/bin/env python3
"""Writes metamorph-datafix/src/RenameSteps.hpp: every block and item rename step of DataFixers.java in a range.
usage: extract-renames.py <path to net/minecraft/util/datafix> <from version> <to version>"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
low, high = int(sys.argv[2]), int(sys.argv[3])
text = (root / "DataFixers.java").read_text()
fixes = root / "fixes"
classes = {path.stem: path.read_text() for path in fixes.glob("*.java")}


def pairs(expression):
    strings = re.findall(r'"(minecraft:[^"]*)"', expression)
    return list(zip(strings[0::2], strings[1::2]))


def constant(owner, name):
    body = classes[owner]
    match = re.search(name + r"\s*=\s*(.*?)(?:\.build\(\)|\);\s*$)", body, re.S | re.M)
    block = re.search(name + r"[^=]*=\s*(.*?);\n", body, re.S)
    source = block.group(1) if block else ""
    return pairs(source)


variables = {}
current = None
steps = []
for line in text.splitlines():
    version = re.search(r"addSchema\((\d+)(?:, (\d+))?", line)
    if version:
        current = int(version.group(1))
    declared = re.search(r"(?:ImmutableMap<String, String>|UnaryOperator<String>|Map<String, String>)\s+(\w+)\s*=\s*(.*)", line)
    if declared:
        variables[declared.group(1)] = declared.group(2)
    found = re.search(r"(ItemRenameFix|LegacyBlockRenameFix)\.create\((\w+), \"([^\"]*)\", (.*)\)\);", line)
    if not found or current is None or not (low <= current <= high):
        continue
    kind = "item" if found.group(1) == "ItemRenameFix" else "block"
    expression = found.group(4)
    if "->" in expression:
        continue
    resolved = pairs(expression)
    for owner_name in re.findall(r"(\w+)\.(\w+)\)?", expression):
        owner, member = owner_name
        if owner in classes and member.isupper():
            resolved += constant(owner, member)
    for name, value in variables.items():
        if re.search(r"\b" + name + r"\b", expression):
            resolved += pairs(value)
    steps.append((current, kind, found.group(3), resolved))

out = [
    "// Generated from the vanilla sources by scripts/extract-renames.py. Do not edit.",
    "#pragma once",
    "",
    "#include <array>",
    "#include <cstdint>",
    "#include <string_view>",
    "",
    "namespace basalt::metamorph::datafix::renames {",
    "",
    "struct Pair {",
    "    std::string_view from;",
    "    std::string_view to;",
    "};",
    "",
]
for index, (version, kind, name, resolved) in enumerate(steps):
    out.append(f"// {name}")
    out.append(f"inline const std::array<Pair, {len(resolved)}> kStep{index}{{{{")
    out.extend(f'    {{"{a}", "{b}"}},' for a, b in resolved)
    out.append("}};")
    out.append("")
out.append("struct Step {")
out.append("    std::int32_t version;")
out.append("    bool block;")
out.append("    const Pair* pairs;")
out.append("    std::size_t count;")
out.append("};")
out.append("")
out.append(f"inline const std::array<Step, {len(steps)}> kSteps{{{{")
for index, (version, kind, name, resolved) in enumerate(steps):
    out.append(f"    {{{version}, {'true' if kind == 'block' else 'false'}, kStep{index}.data(), kStep{index}.size()}},")
out.append("}};")
out.append("")
out.append("} // namespace basalt::metamorph::datafix::renames")
target = Path(__file__).resolve().parent.parent / "metamorph-datafix" / "src" / "RenameSteps.hpp"
target.write_text("\n".join(out) + "\n")
for version, kind, name, resolved in steps:
    print(version, kind, len(resolved), name)
