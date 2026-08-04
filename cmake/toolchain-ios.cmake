# iOS toolchain for myui. UNVERIFIED: no Xcode/SDK available locally.
# Usage: cmake -S . -B build-ios -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-ios.cmake \
#          -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY) # cannot link/run on host

# ObjC shim files (.m) for the uikit port must be enabled per-target:
#   set_source_files_properties(... PROPERTIES LANGUAGE C) is default;
#   enable ObjC with: enable_language(OBJC) in the port's CMake.
# myui presets (port does not exist yet, see docs/porting.md):
set(MYUI_PAL dummy CACHE STRING "PAL port (TODO: uikit port)")
set(MYUI_BUILD_TESTS OFF CACHE BOOL "no ctest on device by default")
