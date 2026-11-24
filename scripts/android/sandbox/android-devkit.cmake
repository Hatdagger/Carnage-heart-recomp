# CMake toolchain for building the APK WITHOUT a full Android NDK: host
# clang/lld (18+) + the "android-devkit" draft release (aarch64 sysroot and
# clang runtime libs). Used in the Claude build sandbox, which cannot reach
# dl.google.com. Players' kits use the real NDK instead.
#
#   ANDROID_DEVKIT   unpacked android-devkit/ (sysroot/, clang-rt/)  [required]
#   NDK_RESOURCE_DIR clang resource dir to make: include/ = host clang's
#                    lib/clang/<ver>/include, lib/linux/ = devkit clang-rt/*
#                    (default: <devkit>/../ndkres; prepare_sandbox.sh makes it)
#
# CMAKE_SYSTEM_NAME is Linux (CMake's Android platform insists on a full NDK)
# with ANDROID set, as cmake/ch_android.cmake and SDL3/libwebp expect.
if(NOT DEFINED ENV{ANDROID_DEVKIT})
    message(FATAL_ERROR "set ANDROID_DEVKIT to the unpacked android-devkit folder")
endif()
set(DK $ENV{ANDROID_DEVKIT})
if(DEFINED ENV{NDK_RESOURCE_DIR})
    set(RES $ENV{NDK_RESOURCE_DIR})
else()
    get_filename_component(RES "${DK}/../ndkres" ABSOLUTE)
endif()
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT ${DK}/sysroot)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-android29)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-android29)
set(CMAKE_ASM_COMPILER_TARGET aarch64-linux-android29)   # libchdr's LzmaDecOpt.S
set(CMAKE_AR llvm-ar CACHE FILEPATH "")
set(CMAKE_RANLIB llvm-ranlib CACHE FILEPATH "")
set(CMAKE_STRIP llvm-strip CACHE FILEPATH "")
set(_f "-resource-dir=${RES} -fPIC")
set(CMAKE_C_FLAGS_INIT "${_f}")
set(CMAKE_ASM_FLAGS_INIT "${_f}")
set(CMAKE_CXX_FLAGS_INIT "${_f} -stdlib=libc++")
set(_l "-fuse-ld=lld -resource-dir=${RES} -rtlib=compiler-rt --unwindlib=libunwind -static-libstdc++ -stdlib=libc++ -Wl,--build-id=sha1 -Wl,--no-undefined")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_l}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_l}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_l}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(ANDROID TRUE)
set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-29)
set(ANDROID_NATIVE_API_LEVEL 29)
# libwebp compiles ${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c;
# ndkstub/ is a minimal arm64 stand-in.
set(ANDROID_NDK ${CMAKE_CURRENT_LIST_DIR}/ndkstub)
