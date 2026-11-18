<p align="center">
  <a href="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-white.svg#gh-dark-mode-only">
    <img src="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-white.svg" alt="Basalt" width="360">
  </a>
  <a href="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-black.svg#gh-light-mode-only">
    <img src="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-black.svg" alt="Basalt" width="360">
  </a>
</p>

<h2 align="center">Basalt Metamorph</h2>

<p align="center">
  <strong>Converts and upgrades Minecraft worlds. Native, no JVM.</strong>
</p>

<p align="center">
  <img alt="Java Edition" src="https://img.shields.io/badge/Java_Edition-26.3-62B47A?logo=minecraft&logoColor=white">
  <img alt="C++" src="https://img.shields.io/badge/language-C%2B%2B-00599C?logo=cplusplus&logoColor=white">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS_%7C_Linux_%7C_Windows-00599C">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-green">
</p>

## About

Metamorph reads a Minecraft Java Edition world and writes it back in another shape, without touching the original:

- **Between region formats:** Anvil (`.mca`), Linear (`.linear`) and Basalt's own bsw (`.bsw`), the default for new
  Basalt Server worlds. Each chunk is checked after converting.
- **Between versions:** every chunk and entity file from an older game is brought to the current world version, with
  the same fixes the official game applies when it opens an old world.

It is a command line tool for people moving a world, and the library that Basalt Server uses to read and convert
worlds on load, as a git submodule.

## Usage

```
metamorph convert <world> --to anvil|linear|bsw [-o <out>]
metamorph upgrade <world> [-o <out>]
```

The input is never modified: the result goes to `<world>.converted` unless `-o` says otherwise, and an output folder
that already holds files is refused.

## Building

```bash
git submodule update --init
cmake --preset release && cmake --build build/release
```

Needs CMake 3.28+, Ninja and Clang. The binary is `build/release/tools/cli/metamorph`.

## License

[GPL-3.0-only](LICENSE).
