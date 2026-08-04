# Emscripten toolchain for myui. UNVERIFIED: no SDK available locally.
# Usage: cmake -S . -B build-web -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-emscripten.cmake
# Requires: emsdk activated (emcc/em++ in PATH).
set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

# Emscripten ships its own toolchain file; prefer it when present and let
# it drive compiler detection:
if(DEFINED ENV{EMSDK})
  include($ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake OPTIONAL)
endif()

# myui presets for web (ports do not exist yet, see docs/porting.md):
set(MYUI_PAL dummy CACHE STRING "PAL port (TODO: emscripten port)")
# WebGL backend: reuse the GLES2 vgcanvas (GL calls map to WebGL1/GLES2)
set(MYUI_BUILD_TESTS OFF CACHE BOOL "ctest has no wasm runner here")
