# Contributing / Building from source

The build uses CMake + vcpkg (manifest mode). Dependencies: raylib,
glfw3, curl, nlohmann-json, and networkml. They are resolved
automatically.

## Prerequisites
- CMake 3.28+
- A C++17 compiler (MSVC, GCC, or Clang)
- A full git clone of [vcpkg](https://github.com/microsoft/vcpkg) (the
  manifest's `builtin-baseline` needs vcpkg's git history; the stripped vcpkg
  bundled with Visual Studio will *not* work). Bootstrap it and point
  `VCPKG_ROOT` at it:
  ```bat
  git clone https://github.com/microsoft/vcpkg C:\vcpkg
  C:\vcpkg\bootstrap-vcpkg.bat
  setx VCPKG_ROOT C:\vcpkg
  ```
  Reopen your shell / Visual Studio afterwards so `VCPKG_ROOT` is picked up.

## Visual Studio (recommended on Windows)
1. Set `VCPKG_ROOT` (once): `setx VCPKG_ROOT "C:\path\to\vcpkg"`, then reopen VS.
2. File → Open → Folder… and select this repo's root.
3. Pick the Windows x64 Debug (or Release) configuration from the toolbar
   and build/run. VS reads `CMakePresets.json` automatically.

## Command line
```sh
cmake --preset windows-debug      # or: windows-release
cmake --build build/windows-debug
```
On Linux/macOS, configure with the toolchain directly:
```sh
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

The first configure builds the dependencies from source, which takes a few
minutes; subsequent builds are cached.

## Releases / CI

`.github/workflows/release.yml` builds self-contained binaries for Windows,
Linux and macOS on every push, and publishes a GitHub Release when a `v*` tag
is pushed:
```sh
git tag v1.0.0 && git push origin v1.0.0
```

## Online leaderboard

The game submits scores to `https://mxtalegend.netlify.app/api/leaderboard`.
Override the endpoint for local testing with the `FLAPPY_LEADERBOARD_URL`
environment variable. [personal-website](https://github.com/meta-legend/personal-website) repo.
