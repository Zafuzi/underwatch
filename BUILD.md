# Building for Windows / macOS / Linux

`CMakeLists.txt` now produces a single self-contained executable on every
platform:

- **Linux**: unchanged — links against the system's SDL3 package
  (`find_package(SDL3)`), so local dev builds stay fast.
- **Windows / macOS**: SDL3 is fetched from source (via `FetchContent`,
  pinned to `release-3.4.2` to match the version this project was built
  against) and linked in **statically**, so there's no `SDL3.dll` /
  `libSDL3.dylib` to ship alongside the game. On Windows the MSVC runtime
  (or MinGW's libgcc/libstdc++) is also linked statically, so nothing but
  the OS itself is required to run the binary.

This means the first configure on Windows/macOS will compile SDL3 from
source (a few minutes); subsequent builds are incremental as normal.

## Windows

Requires Visual Studio 2022 (or the Build Tools) with the "Desktop
development with C++" workload, and CMake 3.16+.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The result is `build\Release\underwatch.exe` — a single file, no installer
or redistributable needed.

If you'd rather use MinGW-w64 (e.g. via MSYS2) instead of MSVC:

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## macOS

Requires Xcode command line tools (`xcode-select --install`) and CMake
3.16+.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The result is `build/underwatch` — a universal binary (arm64 + x86_64,
targeting macOS 11+), so one file runs on both Apple Silicon and Intel
Macs.

Note: since the binary isn't code-signed/notarized, Gatekeeper will warn
on first launch (right-click → Open, or `xattr -d com.apple.quarantine
underwatch`). Signing requires an Apple Developer account and isn't
covered here.

## Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requires SDL3 installed via your package manager (already the case in
this dev environment).

## Forcing the vendored/static SDL3 path anywhere (e.g. to test it on Linux)

```bash
cmake -S . -B build-static -DUNDERWATCH_VENDOR_SDL3=ON
cmake --build build-static -j
```

## WebAssembly (browser)

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
installed and activated (`source /path/to/emsdk/emsdk_env.sh`).

```bash
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j
```

This vendors and builds SDL3 from source against the Emscripten toolchain
(same as the Windows/macOS path — there's no system SDL3 for wasm to find),
and produces `build-wasm/underwatch.html` + `underwatch.js` + `underwatch.wasm`.
Serve the directory with any static file server and open `underwatch.html`:

```bash
python3 -m http.server -d build-wasm 8000
# then open http://localhost:8000/underwatch.html
```

It can't be opened directly via `file://` — browsers block `fetch()` of the
`.wasm` file from a local file path.
