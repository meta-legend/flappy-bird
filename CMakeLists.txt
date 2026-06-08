cmake_minimum_required(VERSION 3.28)

project(FlappyBird VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to a Release build when the generator is single-config and the user
# did not pick one (so a plain `cmake -B build` produces an optimized binary).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Dependencies ------------------------------------------------------------
# raylib comes from vcpkg (see vcpkg.json). The vcpkg toolchain file is passed
# on the command line, so manifest mode installs it automatically at configure.
find_package(raylib CONFIG REQUIRED)
# vcpkg's static raylib references GLFW symbols but doesn't propagate the glfw
# link dependency through its target, so link glfw explicitly.
find_package(glfw3 CONFIG REQUIRED)

# networkml v2.0.0 is built from source so the game is portable across
# Windows / macOS / Linux. The prebuilt .lib in the v2.0.0 release is
# Windows/MSVC-only; building from source lets CI produce native binaries for
# every platform. networkml needs libcurl + nlohmann-json (from vcpkg).
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Fetch the source only. networkml's own CMakeLists exports a target whose
# include path isn't $<BUILD_INTERFACE:>-wrapped, which fails CMake generation,
# so we point SOURCE_SUBDIR at a directory with no CMakeLists.txt to stop
# FetchContent from add_subdirectory()-ing it, then compile it ourselves.
include(FetchContent)
FetchContent_Declare(
    networkml
    GIT_REPOSITORY https://github.com/meta-legend/network-ml-library.git
    GIT_TAG        v2.0.0
    SOURCE_SUBDIR  _skip_networkml_cmake   # no CMakeLists here -> not configured
)
FetchContent_MakeAvailable(networkml)

add_library(networkml STATIC
    "${networkml_SOURCE_DIR}/Network ML/File.cpp"
    "${networkml_SOURCE_DIR}/Network ML/Requests.cpp"
    "${networkml_SOURCE_DIR}/Network ML/Chat.cpp"
)
target_include_directories(networkml PUBLIC "${networkml_SOURCE_DIR}/Network ML")
target_link_libraries(networkml PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
add_library(NetworkML::networkml ALIAS networkml)

# --- Game executable ---------------------------------------------------------
add_executable(Flappy_Bird
    Flappy_Bird/main.cpp
    Flappy_Bird/ui.cpp
    Flappy_Bird/net.cpp)
target_link_libraries(Flappy_Bird PRIVATE raylib glfw NetworkML::networkml nlohmann_json::nlohmann_json)

# HMAC secret for signing leaderboard submissions. Pass -DLEADERBOARD_SECRET=...
# (CI injects it from a GitHub Actions secret). Without it the game uses a dev
# secret that only matches a local dev server.
set(LEADERBOARD_SECRET "" CACHE STRING "HMAC secret for leaderboard signing")
if(LEADERBOARD_SECRET)
    target_compile_definitions(Flappy_Bird PRIVATE LEADERBOARD_SECRET="${LEADERBOARD_SECRET}")
endif()

# On Windows build a GUI app (no console window) while keeping int main().
if(WIN32)
    set_target_properties(Flappy_Bird PROPERTIES WIN32_EXECUTABLE TRUE)
    # raylib 5.5 needs winmm (timeBeginPeriod/timeEndPeriod); its vcpkg config
    # doesn't propagate it, so link it explicitly.
    target_link_libraries(Flappy_Bird PRIVATE winmm shell32)
    if(MSVC)
        target_link_options(Flappy_Bird PRIVATE /ENTRY:mainCRTStartup)
    else()
        target_link_options(Flappy_Bird PRIVATE -mwindows)
    endif()
endif()

# Copy assets next to the binary so the game runs straight from the build tree.
add_custom_command(TARGET Flappy_Bird POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Flappy_Bird/assets"
            "$<TARGET_FILE_DIR:Flappy_Bird>/assets"
    VERBATIM)

# --- Install + packaging (CPack) --------------------------------------------
# Layout inside the package: the executable at the root next to an assets/ dir.
install(TARGETS Flappy_Bird RUNTIME DESTINATION "." BUNDLE DESTINATION ".")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Flappy_Bird/assets" DESTINATION ".")

set(CPACK_PACKAGE_NAME "FlappyBird")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "FlappyBird-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
