# Flappy Bird (raylib)

A Flappy Bird clone in C++ with [raylib](https://www.raylib.com/), featuring an
online high-score leaderboard (powered by a Netlify Function + Netlify Blobs).

## Downloads

Prebuilt binaries for Windows, Linux and macOS are published on the
[Releases](https://github.com/meta-legend/Flappy_Bird/releases) page — download,
unzip, and run. No installation required.

## Building from source

The build uses **CMake** + **vcpkg** (manifest mode). Dependencies — raylib,
glfw3, curl, nlohmann-json, and networkml (fetched from source) — are resolved
automatically.

### Prerequisites
- CMake 3.28+
- A C++17 compiler (MSVC, GCC, or Clang)
- [vcpkg](https://github.com/microsoft/vcpkg) with the `VCPKG_ROOT` environment
  variable pointing at it. On Windows you can reuse the copy bundled with Visual
  Studio, e.g. `C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg`.

### Visual Studio (recommended on Windows)
1. Set `VCPKG_ROOT` (once): `setx VCPKG_ROOT "C:\path\to\vcpkg"` then reopen VS.
2. **File → Open → Folder…** and select this repo's root.
3. Pick the **Windows x64 Debug** (or Release) configuration from the toolbar
   and build/run. VS reads `CMakePresets.json` automatically.

### Command line
```sh
cmake --preset windows-debug      # or: windows-release
cmake --build build/windows-debug
```
On Linux/macOS, configure with your own preset or pass the toolchain directly:
```sh
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

## Releases / CI

`.github/workflows/release.yml` builds self-contained binaries for all three
platforms on every push, and **publishes a GitHub Release when a `v*` tag is
pushed** (e.g. `git tag v1.0.0 && git push origin v1.0.0`).

## Online leaderboard

The game submits scores to `https://pranabshukla.netlify.app/api/leaderboard`.
Override the endpoint for local testing with the `FLAPPY_LEADERBOARD_URL`
environment variable. The leaderboard page lives in the
[personal-website](https://github.com/meta-legend/personal-website) repo.

## Note on the legacy Visual Studio project

The `Flappy_Bird.sln` / `.vcxproj` and the vendored `vcpkg/` folder are kept for
reference but are **superseded by the CMake build**, which is what CI uses. Prefer
the CMake workflow above.
