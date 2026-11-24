# Android build of Carnage Heart (arm64, SDL3, software renderer first).
#
# Included from CMakeLists.txt BEFORE runtime.cmake when ANDROID is set (NDK
# toolchain file, or scripts/android/build_apk.py). Keeps the framework
# submodule untouched: everything Android-specific lives here and in
# android/ (Java shell, manifest) + src/android/ (C shims).
#
# The game target becomes libmain.so, which SDLActivity loads and whose
# SDL_main it calls. Desktop-only surfaces (recomp-ui launcher, setup wizard,
# netplay, Vulkan, OpenXR, runtime overlay compiler) are off.

set(CH_ANDROID ON)
message(STATUS "Carnage Heart: Android build (${ANDROID_ABI})")

foreach(_ch_off PSX_RECOMP_UI PSX_SETUP_WIZARD PSX_NETPLAY PSX_ENABLE_VULKAN
                PSX_OPENXR PSX_PGXP_VARIANT PSX_DEBUG_TOOLS PSX_REWIND)
    set(${_ch_off} OFF CACHE BOOL "Off on Android" FORCE)
endforeach()
set(PSX_SDL3_FETCH OFF CACHE BOOL "Android: SDL3 comes from SDL3_DIR" FORCE)

# Configure checks compile only: an Android test program can't be linked the
# way a desktop one is (static SDL3 carries C++ objects and JNI hooks).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# ch_ucontext_aarch64.c provides getcontext & co. (runtime.cmake checks for it
# when the build reports a Linux system name).
set(PSX_HAVE_GETCONTEXT ON CACHE BOOL "Android: provided by src/android" FORCE)
# pthreads live in Bionic's libc; with compile-only checks FindThreads would
# otherwise settle on a -lpthreads that does not exist.
set(CMAKE_HAVE_LIBC_PTHREAD 1 CACHE INTERNAL "Android: pthreads are in libc")
set(THREADS_PREFER_PTHREAD_FLAG OFF)

if(NOT SDL3_DIR)
    message(FATAL_ERROR
        "Android build needs a static SDL3 built for Android: pass "
        "-DSDL3_DIR=<prefix>/lib/cmake/SDL3 (scripts/android/build_apk.py does this).")
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
        set_property(GLOBAL APPEND PROPERTY CH_ANDROID_GAME_TARGETS ${name})
    else()
        _add_executable(${name} ${ARGN})
    endif()
endfunction()

# Called from CMakeLists.txt after psxrecomp_add_game_runtime().
function(ch_android_finish_target target)
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/android/ch_android_main.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/android/ch_ucontext_aarch64.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/android/ch_android_gl_shims.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/android")
    # Bionic has no getcontext/makecontext/swapcontext; psx_fiber.c gets the
    # declarations from this header and the code from ch_ucontext_aarch64.S.
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/android/ch_android_compat.h>")
    # main.cpp's main() (renamed SDL_main by SDL_main.h) becomes
    # ch_android_real_main; ch_android_main.c supplies SDL_main, which sets up
    # the app's files directory as the game folder first.
    get_target_property(_srcs ${target} SOURCES)
    foreach(_s IN LISTS _srcs)
        if(_s MATCHES "/runtime/src/main\\.cpp$")
            set_property(SOURCE "${_s}" TARGET_DIRECTORY ${target} APPEND PROPERTY
                COMPILE_DEFINITIONS "SDL_main=ch_android_real_main")
        endif()
    endforeach()
    target_link_libraries(${target} PRIVATE android log EGL GLESv3 OpenSLES)
    target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384")
endfunction()

# runtime.cmake asks recomp-ui's recomp_resolve_gl() for the desktop GL
# library; Android has GLES only (EGL + GLESv3 are linked in
# ch_android_finish_target).
function(recomp_resolve_gl out_var)
    set(${out_var} GLESv3 PARENT_SCOPE)
endfunction()
