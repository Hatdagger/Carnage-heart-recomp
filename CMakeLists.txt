cmake_minimum_required(VERSION 3.20)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR
        "PSXRecomp runtime is missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake\n"
        "This project expects the psxrecomp framework checkout at ${PSXRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive\n"
        "If psxrecomp/ is not a submodule, set -DPSXRECOMP_ROOT=<path-to-psxrecomp>.")
endif()

project(CarnageHeartRecomp LANGUAGES C CXX)

# Android (arm64 APK): the PSXRecomp Android kit (psxrecomp-android-kit/,
# settings in android_game.json) turns off the desktop-only surfaces and
# makes the game a libmain.so for SDLActivity.
if(ANDROID)
    include("${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp-android-kit/cmake/psxak.cmake")
    # Online campaign (turn files through the relay): on in the app, which has
    # its own Online screen (android/.../OnlineActivity.java) and does the
    # relay requests in Java (src/android/ch_android_relay.c).
    set(CH_REMOTE_BATTLE ON CACHE BOOL "Remote-player battles / online campaign")
endif()

# --- Opt-in product surfaces (OFF by default in the framework) -------------
# Scaffold prompts / flags fill these. Without recomp-ui, PSX_RECOMP_UI is OFF.
# The wizard (WIZARD_CMAKE_BLOCK / ENABLE_SETUP_WIZARD) is the first-run disc
# picker; releases are compiled from the committed generated/ C, so it never
# needs to generate or rebuild anything on a player's machine.
#
# Carnage Heart ships as a self-compiling kit (the repo holds no generated C):
# the zip-root exe is a recomp-ui launcher + setup wizard that finds the
# player's disc, generates the game C and builds the game on their machine.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/recomp_ui.cmake")
    set(PSX_RECOMP_UI ON CACHE BOOL "recomp-ui launcher (submodule)")
    set(PSX_SETUP_WIZARD ON CACHE BOOL "First-run disc picker + Generate & rebuild")
else()
    set(PSX_RECOMP_UI OFF CACHE BOOL
        "recomp-ui submodule missing (git submodule update --init)" FORCE)
endif()
# Full netplay UI — enable after adding recomp-ui + testing:
# if(EXISTS "${PSXRECOMP_ROOT}/lib/recomp-net/CMakeLists.txt")
#     set(PSX_NETPLAY ON CACHE BOOL "…" FORCE)
# endif()
# First-run setup wizard — enable after testing:
# set(PSX_SETUP_WIZARD ON CACHE BOOL "…" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# This project's own mod catalog. The FRAMEWORK stages it -- together with its
# builtin psx.* packages -- into <exe-dir>/mods/bundled, and refuses to
# configure if a catalog exists here but is not declared. Shared Internal
# Resolution is included even with a builtin allowlist, enabled at 4x on fresh
# profiles, with 1x..32x choices; do not supply a title-local copy of that id.
# Never copy the catalog by hand: five titles did, and framework 4cc04be3's rename of the staged
# directory broke all five silently (bead beads-eio.3.101).
set(PSX_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(NOT IS_DIRECTORY "${PSX_PRELOADED_MODS}")
    set(PSX_PRELOADED_MODS "NONE")
endif()

psxrecomp_add_game_runtime(psx-runtime
    PRELOADED_MODS_DIR "${PSX_PRELOADED_MODS}"
    # ENABLE_NETPLAY_IF_PRESENT
    # NETPLAY_LOBBY_URL "ws://netplay.retcomm.net:8765"
    ENABLE_SETUP_WIZARD
    WINDOW_TITLE "CarnageHeart Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 2
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/psxrecomp.ico"
    # Committed game C (psxrecomp_cli.py generate; git add generated). Release
    # CI configures with -DPSXRECOMP_REQUIRE_GAME_C=ON so a checkout without
    # it fails instead of building a setup host.
    GEN_MARKER "generated/SCUS_946.04_dispatch.c"
    GEN_FULL_GLOB "generated/SCUS_946.04_full_*.c"
    # Setup-host hooks: codegen host config + "disc beside the exe" finder.
    CODEGEN_SETUP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/codegen_setup.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/disc_autodetect.c"
)

# Online campaign (chat B's turn files, src/remote_battle.c and friends) is
# built into players' games by default, so the launcher's ONLINE CAMPAIGN
# page works in every release. The hooks do nothing until the launcher
# starts the game with the turn switches (CH_TURN_*), so a normal campaign
# plays exactly as before. Set here, before anything reads it (the launcher
# menu does at ch_add_launcher_menu below); the option() further down then
# keeps this value. -DCH_REMOTE_BATTLE=OFF still turns it off. (Android
# sets it above.)
if(NOT DEFINED CACHE{CH_REMOTE_BATTLE} AND NOT ANDROID)
    set(CH_REMOTE_BATTLE ON CACHE BOOL "Remote-player battle support (online campaign)")
endif()

# Stage runtime configs next to the exe for direct build/ launches. Only copy
# files that exist — never fail POST_BUILD when game_options.toml is absent.
foreach(_PSX_RUNTIME_CONFIG game.toml game_options.toml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}")
        add_custom_command(TARGET psx-runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}"
                "$<TARGET_FILE_DIR:psx-runtime>/${_PSX_RUNTIME_CONFIG}"
            COMMENT "Staging ${_PSX_RUNTIME_CONFIG}"
            VERBATIM)
    endif()
endforeach()

# Launcher menu: LAUNCH GAME / ONLINE PLAY / SHARE DATA / UPDATE / SETTINGS,
# for controller, keyboard or a 2-button mouse (launcher/ch_menu.inc).
if(PSX_RECOMP_UI)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ch_launcher_menu.cmake")
    ch_add_launcher_menu(psx-runtime)
    if(TARGET psx-runtime-pgxp)
        ch_add_launcher_menu(psx-runtime-pgxp)
    endif()
    # Setup wizard writes setup-log.txt in the game folder (cmake/ch_setup_log.cmake).
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ch_setup_log.cmake")
    ch_add_setup_log(psx-runtime)
    if(TARGET psx-runtime-pgxp)
        ch_add_setup_log(psx-runtime-pgxp)
    endif()
endif()

# PlayStation Mouse support restored from Carnage Heart EZ (src/mouse_pad_hle.c),
# plus window pointer / touch control and the right-click button menu
# (src/pointer_menu.c; CH_POINTER=0 at run time turns that off).
# HLE = mouse restored; LLE = original USA controller routine.
set(CH_MOUSE_IMPL "HLE" CACHE STRING "HLE (mouse restored) or LLE (original)")
set(_ch_mouse_targets psx-runtime)
if(TARGET psx-runtime-pgxp)
    list(APPEND _ch_mouse_targets psx-runtime-pgxp)
endif()
file(GLOB _ch_game_shards "${CMAKE_CURRENT_SOURCE_DIR}/generated/SCUS_946.04_full_*.c")
# The setup host (no generated C yet) has nothing to patch; the player's
# rebuild after Generate configures again with the shards present.
if(_ch_game_shards AND NOT PSXRECOMP_FORCE_SETUP_HOST)
psxrecomp_select_guest_implementation(
    NAME ch_mouse IMPLEMENTATION ${CH_MOUSE_IMPL}
    TARGETS ${_ch_mouse_targets}
    GENERATED_SOURCES ${_ch_game_shards}
    SYMBOLS func_8003E154 func_8003E89C func_8003EEC4 func_8001FB04 func_8003E4C8
    HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/mouse_pad_hle.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/pointer_menu.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/screen_labels.c"
    REFERENCE_PREFIX ch_mouse_lle_
    SNAPSHOT_TAG 0x43484D31)
endif()
# Remote-player battles (multiplayer groundwork, off by default): campaign
# battles against another player's designs, fought the way VS mode does.
# Run-time switches are described in src/remote_battle.c, src/hotseat.c,
# src/turn_files.c and
# docs/research/VS_MODE_AND_SAVES.md.
option(CH_REMOTE_BATTLE "Remote-player battle support (multiplayer groundwork)" OFF)
if(CH_REMOTE_BATTLE AND _ch_game_shards AND NOT PSXRECOMP_FORCE_SETUP_HOST)
psxrecomp_select_guest_implementation(
    NAME ch_remote IMPLEMENTATION HLE
    TARGETS ${_ch_mouse_targets}
    GENERATED_SOURCES ${_ch_game_shards}
    SYMBOLS func_8001CF48 func_80035CD4 func_80035988
            func_8001B8EC func_8001C268 func_8001BCB0 func_8008A2B8
            func_800333F8
    HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/remote_battle.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/hotseat.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/turn_files.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/turn_relay.c"
    REFERENCE_PREFIX ch_remote_lle_
    SNAPSHOT_TAG 0x43485243)
endif()
# Test-only: -DCH_MOUSE_SELFTEST=ON drives a fake mouse from a file (see src/mouse_pad_hle.c).
option(CH_MOUSE_SELFTEST "Fake PS1 Mouse from CH_MOUSE_SELFTEST_FILE (testing only)" OFF)
if(CH_MOUSE_SELFTEST)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/mouse_pad_hle.c"
                                "${CMAKE_CURRENT_SOURCE_DIR}/src/pointer_menu.c"
        TARGET_DIRECTORY ${_ch_mouse_targets}
        PROPERTIES COMPILE_DEFINITIONS CH_MOUSE_SELFTEST=1)
endif()

# Test-only: -DCH_GLES_DESKTOP_TEST=ON runs the OpenGL renderer through the
# Android OpenGL ES bridge (psxrecomp-android-kit/native/psxak_gles.c) on a desktop Linux
# build, so the GLES path can be checked on Mesa without a device:
#   SDL_VIDEO_DRIVER=offscreen EGL_PLATFORM=surfaceless ./CarnageHeart_Recompiled --renderer opengl ...
# CH_GLES_HEADERS: a directory with GLES3/ GLES2/ EGL/ KHR/ headers (the NDK
# sysroot's usr/include works). Link GLES like Android does, e.g.
#   -DOPENGL_gl_LIBRARY=/usr/lib/x86_64-linux-gnu/libGLESv2.so.2
#   -DOPENGL_INCLUDE_DIR=<any dir> -DPSX_RECOMP_UI=OFF -DPSX_SETUP_WIZARD=OFF
#   -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
option(CH_GLES_DESKTOP_TEST "Desktop build using the Android GLES bridge (testing only)" OFF)
if(CH_GLES_DESKTOP_TEST AND NOT ANDROID)
    set(CH_GLES_HEADERS "" CACHE PATH "Directory with GLES3/ and KHR/ headers")
    set(_psxak_n "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp-android-kit/native")
    foreach(_t IN LISTS _ch_mouse_targets)
        target_sources(${_t} PRIVATE
            "${_psxak_n}/psxak_gles.c" "${_psxak_n}/psxak_gl_shims.c"
            "${_psxak_n}/psxak_gles_translate.c")
        target_include_directories(${_t} PRIVATE "${_psxak_n}")
        set_source_files_properties(
            "${_psxak_n}/psxak_gles.c" "${_psxak_n}/psxak_gl_shims.c"
            TARGET_DIRECTORY ${_t}
            PROPERTIES COMPILE_OPTIONS "-idirafter;${CH_GLES_HEADERS}")
        target_link_options(${_t} PRIVATE
            "-Wl,--wrap=SDL_GL_SetAttribute" "-Wl,--wrap=SDL_GL_GetProcAddress"
            "-Wl,--wrap=glTexImage2D" "-Wl,--wrap=glTexSubImage2D" "-Wl,--wrap=glReadPixels"
            "-Wl,--wrap=gl_renderer_init_context")
    endforeach()
endif()

# Android: the kit's libmain.so extras (shims, GLES/EGL, present hook) plus
# Carnage Heart's own (online relay through Java, start-up hook). The touch
# menu (src/pointer_menu.c) draws through the kit's psxak_draw_overlay_sdl().
if(PSXAK_ANDROID)
    psxak_finish_targets(SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/android/ch_android_relay.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/android/ch_android_game.c")
endif()
