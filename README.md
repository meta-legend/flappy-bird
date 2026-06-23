<div align="center">

<img src="docs/bird.gif" alt="Flappy Bird" width="136" height="96">

# Flappy Bird

[![Version](https://img.shields.io/github/v/release/meta-legend/Flappy_Bird?label=version&sort=semver)](https://github.com/meta-legend/Flappy_Bird/releases/latest)
[![Build & Release](https://github.com/meta-legend/Flappy_Bird/actions/workflows/release.yml/badge.svg?branch=master)](https://github.com/meta-legend/Flappy_Bird/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)

</div>

A Flappy Bird clone built in C++ with [raylib](https://www.raylib.com/) - now
with a global online leaderboard. Tap to fly, dodge the pipes, and chase the
top spot.

## Download

Grab the build for your OS from the
[**Releases**](https://github.com/meta-legend/Flappy_Bird/releases) page, unzip
it, and run Flappy_Bird - no installation needed (Supports Windows, Linux,
macOS).

> **Heads up about antivirus warnings:** the Windows build is unsigned (code-
> signing certificates are expensive for a hobby project), so Windows Defender
> SmartScreen or your antivirus may flag it on first launch. The binary is
> safe - if SmartScreen shows up, click **More info** then **Run anyway**. If
> you'd rather not trust the prebuilt binary, build from source yourself with
> the steps below.

## How To Play:

1. On the title screen, type your name and press Enter to start.
2. Flap with Space, W, Up arrow, or left mouse button.
3. Avoid the pipes, and the game speeds up the longer you survive.
4. After you crash, press Space to try again.

Your best score is saved locally, and each run is submitted to the online
leaderboard automatically.

## Build from source

Every dependency (raylib, glfw3, curl, nlohmann-json, networkml) is resolved by
vcpkg automatically through the manifest in `vcpkg.json` and the private
registry in `vcpkg-configuration.json`. Nothing to install by hand, no
`CMAKE_PREFIX_PATH` to set.

**Prerequisites (all platforms):**
- CMake 3.28+
- A C++17 compiler
- A vcpkg checkout (full git clone, not the stripped version bundled with
  Visual Studio - the manifest's `builtin-baseline` needs vcpkg's git history)

### Windows

```bat
:: one-time vcpkg setup
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT C:\vcpkg
:: reopen your shell so VCPKG_ROOT is picked up

:: build the game
git clone https://github.com/meta-legend/flappy-bird
cd flappy-bird
cmake --preset windows-release
cmake --build --preset windows-release
```

The exe lands at `build/windows-release/Flappy Bird.exe`.

**Using Visual Studio?** After the one-time `setx VCPKG_ROOT` step above,
File → Open → Folder and pick the repo. VS reads `CMakePresets.json` and
you can build from the toolbar.

### Linux & macOS

```sh
# one-time vcpkg setup
git clone https://github.com/microsoft/vcpkg
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$PWD/vcpkg"
# add the export line to your ~/.bashrc or ~/.zshrc so it persists

# build the game
git clone https://github.com/meta-legend/flappy-bird
cd flappy-bird
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

The binary lands at `build/Flappy Bird` (Linux) or
`build/Flappy Bird.app/Contents/MacOS/Flappy Bird` (macOS).

> The first configure builds every dependency (including networkml from the
> [private vcpkg registry](https://github.com/meta-legend/vcpkg-registry))
> from source - a few minutes once, then cached for every subsequent build.

## Credits

Theme music by [HeatleyBros](https://www.youtube.com/channel/UCsLlqLIE-TqDq3lh5kU2PeA).
