# PSXRecomp Android kit: turns a PSXRecomp game project into an arm64
# libmain.so for SDLActivity, without changing the psxrecomp submodule.
#
# In the game's CMakeLists.txt, BEFORE runtime.cmake is included:
#
#   if(ANDROID)
#       include("${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp-android-kit/cmake/psxak.cmake")
#   endif()
#
# and at the end, once the game target exists:
#
#   if(PSXAK_ANDROID)
#       psxak_finish_targets()            # or psxak_finish_target(<target> ...)
#   endif()
#
# Per-game values come from android_game.json in the game's root
# (PSXAK_GAME_JSON to use another path). See docs/SETTINGS.md.
#
# Desktop-only surfaces (recomp-ui launcher, setup wizard, netplay, Vulkan,
# OpenXR, rewind, PGXP, debug tools) are turned off.

if(CMAKE_VERSION VERSION_LESS 3.19)
    message(FATAL_ERROR "PSXRecomp Android kit needs CMake 3.19+ (string(JSON))")
endif()

set(PSXAK_ANDROID ON)
set(PSXAK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "PSXRecomp Android kit root")
get_filename_component(PSXAK_DIR "${PSXAK_DIR}" ABSOLUTE)

if(NOT PSXAK_GAME_JSON)
    set(PSXAK_GAME_JSON "${CMAKE_CURRENT_SOURCE_DIR}/android_game.json")
endif()
if(NOT EXISTS "${PSXAK_GAME_JSON}")
    message(FATAL_ERROR "PSXRecomp Android kit: ${PSXAK_GAME_JSON} is missing "
                        "(copy android_game.example.json from the kit and fill it in)")
endif()
file(READ "${PSXAK_GAME_JSON}" _psxak_json)

# psxak_json_get(<out> <key> <default>): a top-level string from the settings.
function(psxak_json_get out key default)
    string(JSON _v ERROR_VARIABLE _err GET "${_psxak_json}" "${key}")
    if(_err OR _v STREQUAL "")
        set(_v "${default}")
    endif()
    set(${out} "${_v}" PARENT_SCOPE)
endfunction()

psxak_json_get(PSXAK_APP_NAME      app_name      "PSXRecomp game")
psxak_json_get(PSXAK_LOG_TAG       log_tag       "PSXRecomp")
psxak_json_get(PSXAK_LOG_NAME      log_name      "game")
psxak_json_get(PSXAK_EXE_NAME      exe_name      "psx_game")
psxak_json_get(PSXAK_DEFAULT_SCALE default_scale "3x")
message(STATUS "PSXRecomp Android kit: ${PSXAK_APP_NAME} (${ANDROID_ABI})")

foreach(_psxak_off PSX_RECOMP_UI PSX_SETUP_WIZARD PSX_NETPLAY PSX_ENABLE_VULKAN
                   PSX_OPENXR PSX_PGXP_VARIANT PSX_DEBUG_TOOLS PSX_REWIND)
    set(${_psxak_off} OFF CACHE BOOL "Off on Android" FORCE)
endforeach()
set(PSX_SDL3_FETCH OFF CACHE BOOL "Android: SDL3 comes from SDL3_DIR" FORCE)

# Configure checks compile only: an Android test program can't be linked the
# way a desktop one is (static SDL3 carries C++ objects and JNI hooks).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# psxak_ucontext_aarch64.c provides getcontext & co. (runtime.cmake checks for
# it when the build reports a Linux system name).
set(PSX_HAVE_GETCONTEXT ON CACHE BOOL "Android: provided by the kit" FORCE)
# pthreads live in Bionic's libc; with compile-only checks FindThreads would
# otherwise settle on a -lpthreads that does not exist.
set(CMAKE_HAVE_LIBC_PTHREAD 1 CACHE INTERNAL "Android: pthreads are in libc")
set(THREADS_PREFER_PTHREAD_FLAG OFF)

if(NOT SDL3_DIR)
    message(FATAL_ERROR
        "Android build needs a static SDL3 built for Android: pass "
        "-DSDL3_DIR=<prefix>/lib/cmake/SDL3 (scripts/build_apk.py does this).")
endif()

# runtime.cmake creates the game with add_executable(); on Android it must be
# a shared library that SDLActivity can System.loadLibrary(). Overriding the
# command (CMake keeps the original as _add_executable) avoids a framework patch.
function(add_executable name)
    if(name MATCHES "^psx-runtime")
        set(_args ${ARGN})
        list(REMOVE_ITEM _args WIN32 MACOSX_BUNDLE EXCLUDE_FROM_ALL)
        add_library(${name} SHARED ${_args})
        set_target_properties(${name} PROPERTIES OUTPUT_NAME main PREFIX "lib")
        set_property(GLOBAL APPEND PROPERTY PSXAK_GAME_TARGETS ${name})
    else()
        _add_executable(${name} ${ARGN})
    endif()
endfunction()

# psxak_finish_target(<target> [SOURCES <game's own Android C files>...])
function(psxak_finish_target target)
    cmake_parse_arguments(PARSE_ARGV 1 _a "" "" "SOURCES")
    set(_n "${PSXAK_DIR}/native")
    target_sources(${target} PRIVATE
        "${_n}/psxak_main.c" "${_n}/psxak_present.c" "${_n}/psxak_perf.c"
        "${_n}/psxak_watch.c" "${_n}/psxak_ucontext_aarch64.c"
        "${_n}/psxak_gl_shims.c" "${_n}/psxak_gles.c" "${_n}/psxak_gles_translate.c"
        ${_a_SOURCES})
    target_include_directories(${target} PRIVATE "${_n}")
    set_property(SOURCE "${_n}/psxak_main.c" TARGET_DIRECTORY ${target} APPEND PROPERTY
        COMPILE_DEFINITIONS
            "PSXAK_LOG_TAG=\"${PSXAK_LOG_TAG}\"" "PSXAK_LOG_NAME=\"${PSXAK_LOG_NAME}\""
            "PSXAK_EXE_NAME=\"${PSXAK_EXE_NAME}\"" "PSXAK_GAME_NAME=\"${PSXAK_APP_NAME}\""
            "PSXAK_DEFAULT_SCALE=\"${PSXAK_DEFAULT_SCALE}\"")
    # Bionic has no getcontext/makecontext/swapcontext; psx_fiber.c gets the
    # declarations from this header and the code from psxak_ucontext_aarch64.c.
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-include ${_n}/psxak_compat.h>")
    # main.cpp's main() (renamed SDL_main by SDL_main.h) becomes
    # psxak_real_main; psxak_main.c supplies SDL_main, which sets up the
    # app's files directory as the game folder first.
    get_target_property(_srcs ${target} SOURCES)
    foreach(_s IN LISTS _srcs)
        if(_s MATCHES "/runtime/src/main\\.cpp$")
            set_property(SOURCE "${_s}" TARGET_DIRECTORY ${target} APPEND PROPERTY
                COMPILE_DEFINITIONS "SDL_main=psxak_real_main")
        endif()
    endforeach()
    # Every SDL_RenderPresent goes through psxak_present.c first (frame-time
    # hint, optional game overlay on the software renderer).
    # The runtime's OpenGL renderer on OpenGL ES 3.2: context attributes,
    # shader translation and a few desktop-only formats (psxak_gles.c).
    target_link_options(${target} PRIVATE
        "-Wl,--wrap=SDL_RenderPresent"
        "-Wl,--wrap=SDL_GL_SetAttribute" "-Wl,--wrap=SDL_GL_GetProcAddress"
        "-Wl,--wrap=glTexImage2D" "-Wl,--wrap=glTexSubImage2D" "-Wl,--wrap=glReadPixels"
        "-Wl,--wrap=gl_renderer_init_context")
    target_link_libraries(${target} PRIVATE android log EGL GLESv3 OpenSLES)
    target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384")
endfunction()

# psxak_finish_targets([SOURCES ...]): psxak_finish_target on every game
# library runtime.cmake made.
function(psxak_finish_targets)
    get_property(_targets GLOBAL PROPERTY PSXAK_GAME_TARGETS)
    foreach(_t IN LISTS _targets)
        psxak_finish_target(${_t} ${ARGN})
    endforeach()
endfunction()

# runtime.cmake asks recomp-ui's recomp_resolve_gl() for the desktop GL
# library; Android has GLES only (EGL + GLESv3 are linked above).
function(recomp_resolve_gl out_var)
    set(${out_var} GLESv3 PARENT_SCOPE)
endfunction()
