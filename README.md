<p align="center">
  <img src="docs/assets/basalt-logo.png" alt="Basalt" width="360">
</p>

<h2 align="center">Basalt Metamorph</h2>

<p align="center">
  <strong>Converts and upgrades Minecraft worlds. Native, no JVM.</strong>
</p>

<p align="center">
  <img alt="Minecraft" src="https://img.shields.io/badge/Minecraft-26.3-62B47A?logo=minecraft&logoColor=white">
  <img alt="World version" src="https://img.shields.io/badge/world_version-5023-62B47A">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS_%7C_Linux_%7C_Windows-00599C">
  <img alt="Build" src="https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-green">
</p>

## Overview

Metamorph is part of the Basalt Project. It reads a Minecraft: Java Edition world and writes it back in another
shape, without touching the original:

- **Between region formats**: Anvil (`.mca`) to Linear (`.linear`) and the other way round.
- **Between versions**: every chunk and entity file saved by an older game is brought to the world version Basalt
  runs, with the same fixes the official game applies when it opens an old world.

It is both a command line tool for people moving a world, and the library that Basalt Server uses to read and
convert worlds when it loads them. Server includes it as a git submodule, so there is one implementation of each
format and each fix.

Like the rest of the project it is written in **C++23** and ships as a native binary.

## Building

Requires CMake 3.28 or newer, Ninja and Clang.

```bash
git submodule update --init
cmake --preset release && cmake --build build/release
```

The binary is `build/release/tools/cli/metamorph`.

## Usage

```
metamorph convert <world> --to anvil|linear [-o <out>]
metamorph upgrade <world> [-o <out>]
```

`<world>` is a folder with region files anywhere under it; dimension folders, `entities/` and `poi/` are all found.
The input is never modified: the result goes to `<world>.converted` unless `-o` says otherwise, and an output folder
that already holds files is refused.

| Command | What it writes |
|---|---|
| `convert --to linear` | Every region file as `.linear`. A world is usually about half the size |
| `convert --to anvil` | Every region file as `.mca`, each chunk zlib-compressed |
| `upgrade` | Every chunk and entity file at the current world version, each file keeping its format |

`upgrade` reads the `DataVersion` of each chunk, runs the fixes from that version up to 5023, and leaves a chunk that
is already current as it is. A chunk that cannot be read is reported, and the command exits with an error.

## Modules

| Module | Namespace | Holds |
|---|---|---|
| `metamorph-region` | `basalt::metamorph::region` | Anvil and Linear region files, read and write |
| `metamorph-datafix` | `basalt::metamorph::datafix` | The world data fixes, from 1.21.2 data versions to the current one |
| `tools/cli` | | The `metamorph` executable |

Wire provides the build, NBT and compression. Inside Basalt Server, Metamorph reuses the Wire that Server already
has instead of building its own.

## Behaviour

- An Anvil region file of zero bytes is a region with no chunks, as in the official game.
- A chunk that does not fit an Anvil region (1 MiB or more compressed) makes `convert --to anvil` fail for that
  region rather than write a corrupt file.
- Output is written through a temporary file and renamed, so an interrupted run never leaves a half-written region.

## License

GPL-3.0-only. See [LICENSE](LICENSE).
