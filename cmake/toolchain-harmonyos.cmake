# HarmonyOS (OpenHarmony) NDK toolchain for myui. UNVERIFIED: no SDK locally.
# Usage: cmake -S . -B build-harmonyos -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-harmonyos.cmake \
#          -DOHOS_ARCH=arm64-v8a
# Requires: OHOS_NDK_HOME pointing at the OpenHarmony NDK (native/).
set(CMAKE_SYSTEM_NAME OHOS)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{OHOS_NDK_HOME})
  message(WARNING "OHOS_NDK_HOME not set; set it to <sdk>/native")
else()
  set(OHOS_NDK $ENV{OHOS_NDK_HOME})
  # OpenHarmony ships its own toolchain file; prefer it:
  if(EXISTS ${OHOS_NDK}/build/cmake/ohos.toolchain.cmake)
    include(${OHOS_NDK}/build/cmake/ohos.toolchain.cmake)
  endif()
endif()

# myui presets (port does not exist yet, see docs/porting.md):
set(MYUI_PAL dummy CACHE STRING "PAL port (TODO: harmonyos-ndk port)")
set(MYUI_BUILD_TESTS OFF CACHE BOOL "no ctest on device by default")
