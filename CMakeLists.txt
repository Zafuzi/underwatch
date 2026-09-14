cmake_minimum_required(VERSION 3.16)

# Must be set before project() to take effect.
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build a universal binary on macOS")
endif()
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(underwatch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statically link the MSVC runtime so the .exe doesn't need the VC++ redistributable installed.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# On Linux, keep using the distro's SDL3 package (fast incremental builds).
# On Windows/macOS there's no guarantee SDL3 is installed on the machine that
# runs the game, so build SDL3 from source and link it in statically instead
# of shipping/loading SDL3.dll / libSDL3.dylib alongside the executable.
# Emscripten (wasm) always needs the from-source build too, since there's no
# system SDL3 package to find on that target.
option(UNDERWATCH_VENDOR_SDL3 "Build SDL3 from source and link it in statically" "${WIN32}")
if(APPLE OR EMSCRIPTEN)
  set(UNDERWATCH_VENDOR_SDL3 ON CACHE BOOL "" FORCE)
endif()

if(UNDERWATCH_VENDOR_SDL3)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)
else()
  find_package(SDL3 REQUIRED CONFIG)
endif()

add_executable(underwatch src/main.cpp)
target_link_libraries(underwatch PRIVATE SDL3::SDL3)

# MinGW: fold in libgcc/libstdc++/winpthreads so the .exe has no extra DLL dependencies.
if(MINGW)
  target_link_options(underwatch PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Emscripten: produce underwatch.html (+ .js/.wasm alongside it) instead of a
# native binary. ALLOW_MEMORY_GROWTH is needed since the game's vectors of
# players/projectiles aren't bounded ahead of time.
if(EMSCRIPTEN)
  set_target_properties(underwatch PROPERTIES SUFFIX ".html")
  target_link_options(underwatch PRIVATE -sALLOW_MEMORY_GROWTH=1)
endif()
