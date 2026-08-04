# Android NDK toolchain for myui. UNVERIFIED: no NDK available locally.
# Usage: cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-android.cmake \
#          -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
# Requires: ANDROID_NDK_HOME (or ANDROID_NDK) pointing at the NDK.
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 24)

if(DEFINED ENV{ANDROID_NDK_HOME})
  set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_HOME})
elseif(DEFINED ENV{ANDROID_NDK})
  set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK})
endif()
# When CMAKE_ANDROID_NDK is set, CMake's built-in Android support picks
# the clang toolchain from the NDK automatically.

# myui presets (port does not exist yet, see docs/porting.md):
set(MYUI_PAL dummy CACHE STRING "PAL port (TODO: android-ndk port)")
# rendering: GLES2 backend (MYUI_HAS_GLES2 via system libGLESv2)
set(MYUI_BUILD_TESTS OFF CACHE BOOL "no ctest on device by default")
