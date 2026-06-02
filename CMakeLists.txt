cmake_minimum_required(VERSION 3.28)

project(FlappyBird VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to a Release build when the generator is single-config and the user did not pick one (so a plain `cmake -B build` produces an optimized binary)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# dependencies
# raylib + its dependencies comes from vcpkg
find_package(raylib CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

# Flappy uses nlohmann_json directly as well as through networkml.
find_package(nlohmann_json CONFIG REQUIRED)

# networkml comes from vcpkg like every other dependency. It lives in a private
# vcpkg registry (declared in vcpkg.json + vcpkg-configuration.json), so vcpkg
# builds and installs it automatically on configure
# Its package config pulls in libcurl transitively (nlohmann_json is compiled in)
find_package(NetworkML CONFIG REQUIRED)

# expose the project version to C++ so the Info screen can show it without a
# hardcoded duplicate. (`-DFLAPPY_VERSION=...`)
add_compile_definitions(FLAPPY_VERSION="${PROJECT_VERSION}")

# game files
add_executable(Flappy_Bird
    src/main.cpp
    src/game.cpp
    src/game_lifecycle.cpp
    src/assets.cpp
    src/gameplay_helpers.cpp
    src/particles.cpp
    src/render_game.cpp
    src/render_menus.cpp
    src/run_artifacts.cpp
    src/save.cpp
    src/screenshot.cpp
    src/scene_singleplayer.cpp
    src/scene_singleplayer_update.cpp
    src/scene_versus.cpp
    src/screen_customize.cpp
    src/screen_main_menu.cpp
    src/screen_secondary.cpp
    src/screen_settings.cpp
    src/screen_tutorial.cpp
    src/system_display.cpp
    src/system_media.cpp
    src/ui.cpp
    src/net.cpp
    src/pak_archive.cpp
    src/hmac_sha256.cpp)
target_link_libraries(Flappy_Bird PRIVATE raylib glfw NetworkML::networkml nlohmann_json::nlohmann_json)

# Final shipped name is "Flappy Bird" (with a space). The CMake target stays
# Flappy_Bird so every reference below keeps working; only the output file is renamed.
set_target_properties(Flappy_Bird PROPERTIES OUTPUT_NAME "Flappy Bird")

# Link-Time Optimization (whole-program optimization) for shipping builds only.
# Maps to MSVC /GL + /LTCG (or -flto on GCC/Clang). Release-only so Debug stays fast
# to build and debuggable; guarded with check_ipo_supported so a toolchain that lacks
# it doesn't break configuration.
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
if(_ipo_ok)
    set_target_properties(Flappy_Bird PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "LTO/IPO unavailable, skipping: ${_ipo_msg}")
endif()

# HMAC secret injection
set(LEADERBOARD_SECRET "" CACHE STRING "HMAC secret for leaderboard signing")
if(LEADERBOARD_SECRET)
    target_compile_definitions(Flappy_Bird PRIVATE LEADERBOARD_SECRET="${LEADERBOARD_SECRET}")
endif()

# On Windows build a GUI app (no console window)
if(WIN32)
    set_target_properties(Flappy_Bird PROPERTIES WIN32_EXECUTABLE TRUE)
    # Embed the app icon + version/publisher metadata into the .exe via a resource
    # script. The version comes from project() so there's no duplicated number.
    enable_language(RC)   # Windows resource compiler (only needed here)
    set(FLAPPY_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/flappy.ico")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(Flappy_Bird PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    # raylib 5.5 special dependencies
    target_link_libraries(Flappy_Bird PRIVATE winmm shell32)
    if(MSVC)
        target_link_options(Flappy_Bird PRIVATE /ENTRY:mainCRTStartup)
    else()
        target_link_options(Flappy_Bird PRIVATE -mwindows)
    endif()
endif()

# On macOS build a proper .app bundle with an Info.plist + icns icon
if(APPLE)
    set_target_properties(Flappy_Bird PROPERTIES MACOSX_BUNDLE TRUE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "Flappy Bird")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.metalegend.flappybird")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 meta_legend")
    set(MACOSX_BUNDLE_INFO_STRING "Flappy Bird ${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_ICON_FILE "flappy.icns")
    set_target_properties(Flappy_Bird PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in")
    # Drop the icns into the bundle's Contents/Resources where CFBundleIconFile expects it
    set(FLAPPY_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/src/flappy.icns")
    target_sources(Flappy_Bird PRIVATE "${FLAPPY_ICNS}")
    set_source_files_properties("${FLAPPY_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# Copy resources next to the binary so the game runs straight from the build tree
add_custom_command(TARGET Flappy_Bird POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
            "$<TARGET_FILE_DIR:Flappy_Bird>/resources"
    VERBATIM)

# Optional asset packing. When -DPAK_PACK=ON is passed, after the build we
# invoke the build_pak C++ tool to produce data.pak next to the exe. The game
# loads from data.pak when present, falls back to ./resources/... otherwise,
# so the raw resources are kept around for dev
#
# We build build_pak as a separate CMake target so it gets compiled with the
# same toolchain as the main game and can be run manually from the build dir whenever assets change
option(PAK_PACK "Pack resources into data.pak after building" ON)
option(PAK_XOR "Obfuscate the pak data section with XOR (not real encryption)" OFF)

# build_pak is a tiny CLI tool that walks src/resources and writes data.pak.
add_executable(build_pak tools/build_pak.cpp)
set_target_properties(build_pak PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# Uses ML::File for directory walks + binary I/O — same library the game uses,
# so we don't drag in any extra deps beyond what's already configured.
target_link_libraries(build_pak PRIVATE NetworkML::networkml)

if(PAK_PACK)
    set(_PAK_XOR_ARG "")
    if(PAK_XOR)
        set(_PAK_XOR_ARG "--xor")
    endif()
    add_custom_command(TARGET Flappy_Bird POST_BUILD
        COMMAND $<TARGET_FILE:build_pak>
                "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
                "$<TARGET_FILE_DIR:Flappy_Bird>/data.pak"
                ${_PAK_XOR_ARG}
        DEPENDS build_pak
        COMMENT "Packing resources -> data.pak"
        VERBATIM)
endif()

# cpack for distrobution
install(TARGETS Flappy_Bird RUNTIME DESTINATION "." BUNDLE DESTINATION ".")
# The game loads ./resources relative to the executable. On Windows/Linux that's the
# package root (next to the exe). Inside a macOS .app the executable lives in
# Contents/MacOS, so the resources must be installed there too for the packaged
# bundle to be self-contained and relocatable.
if(APPLE)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
            DESTINATION "Flappy Bird.app/Contents/MacOS")
else()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/resources" DESTINATION ".")
endif()

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
