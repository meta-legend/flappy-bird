# Contributing / Building from source

Clone, point CMake at a vcpkg toolchain, build. Every dependency
(raylib, glfw3, curl, nlohmann-json, networkml) is resolved by vcpkg
automatically through the manifest in `vcpkg.json` and the private
registry declared in `vcpkg-configuration.json`. There is nothing to
install by hand and no `CMAKE_PREFIX_PATH` to set.

## Prerequisites
- CMake 3.28+
- A C++17 compiler (MSVC, GCC, or Clang)
- A vcpkg checkout. The full git clone is recommended so the manifest's
  `builtin-baseline` resolves cleanly:
  ```sh
  git clone https://github.com/microsoft/vcpkg
  ./vcpkg/bootstrap-vcpkg.sh        # bootstrap-vcpkg.bat on Windows
  export VCPKG_ROOT="$PWD/vcpkg"    # or `setx VCPKG_ROOT C:\path\to\vcpkg` on Windows
  ```

## Build

```sh
git clone https://github.com/meta-legend/flappy-bird
cd flappy-bird
cmake --preset windows-release    # or: windows-debug
cmake --build --preset windows-release
```

`CMakePresets.json` reads `$VCPKG_ROOT` and wires everything up. The
first configure builds the dependencies (including networkml from the
[private vcpkg registry](https://github.com/meta-legend/vcpkg-registry))
from source - a few minutes once, then cached.

On Linux/macOS pass the toolchain directly instead of a preset:
```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

**Visual Studio users**: set `VCPKG_ROOT` once via `setx`, then File →
Open → Folder. VS reads `CMakePresets.json` and you can build from the
toolbar.

## Releases / CI

`.github/workflows/release.yml` builds self-contained binaries for
Windows, Linux and macOS on every push, and publishes a GitHub Release
when a `v*` tag is pushed:
```sh
git tag v1.0.0 && git push origin v1.0.0
```

## Online leaderboard

The game submits scores to `https://mxtalegend.netlify.app/api/leaderboard`.
Override the endpoint for local testing with the `FLAPPY_LEADERBOARD_URL`
environment variable. The server lives in the
[personal-website](https://github.com/meta-legend/personal-website) repo.
