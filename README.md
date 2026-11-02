<p align="center">
  <img src="assets/logo.png" alt="slinkyFX" width="420">
</p>

<h1 align="center">slinkyFX</h1>

<p align="center">
  A ghost client for Minecraft 1.8.9.
</p>

---

slinkyFX serves as an accessible, cost-effective alternative to the official slinky ghost client.
Developed specifically for budget-conscious users, its core mission is to emulate the original software's entire ecosystem.
The developers have made every effort to match slinky's exact functionality, ensuring that all variables, configurations, and performance traits remain indistinguishable from the premium version.

The loader is deliberately quiet. It scans for a Java process that actually
has LWJGL loaded, works out which naming scheme the game is running with, and
picks the matching payload automatically. If it cannot inject, it says so in
the same place and the same shape as a successful injection, including a short
close countdown, so a failure is never a silent window that disappears.

Everything runs in user space. On Windows the loader writes into the game
process with the normal Win32 process APIs; on Linux it attaches with ptrace
and calls `dlopen` in the target. Linux does not need root and does not
re-launch itself through `sudo`. If Yama's `kernel.yama.ptrace_scope`
restriction is active, the loader tries to relax it for the session; on a
system where that needs root the attempt fails quietly and the regular error
path reports it.

## Supported clients

| Client | Minecraft 1.8.9 | Minecraft 1.7.10 |
|---|---|---|
| Vanilla (Mojang launcher) | Yes | Yes (Windows) |
| Forge | Yes | Yes (Windows) |
| Lunar Client | Yes | Yes (Windows) |
| Badlion Client | Yes | Yes (Windows) |

### Full compatibility matrix

| Client | Windows 1.8.9 | Windows 1.7.10 | Linux 1.8.9 | Linux 1.7.10 |
|---|---|---|---|---|
| Vanilla | Yes | Yes | Yes | Not supported |
| Forge | Yes | Yes | Yes | Not supported |
| Lunar Client | Yes | Yes | Yes | Not supported |
| Badlion Client | Yes | Yes | Yes | Not supported |

The native layer is built from `SlinkyNative/`. On Windows it produces the
slinkyFX native engine (`slinky-native.dll`) and hooks the game directly. On
Linux it produces the legacy JNI/JVMTI payload library (`libslinky-native.so`)
with the embedded Java payload jars. The engine detects the naming scheme in use
(official/notch, SRG for Forge, named for MCP/dev environments, or legacy
1.7.10). Check `slinky-native.log` in the loader's temp folder if you want to see
which mapping was chosen; the log prints the detected version and namespace.

## Supported platforms

| Platform | Display stack | Status |
|---|---|---|
| Windows 10 / 11 (x64) | Win32 | Supported |
| Linux (x86_64) Arch / Debian / Ubuntu / Fedora | Wayland (native) | Supported |
| Linux (x86_64) Arch / Debian / Ubuntu / Fedora | X11 / XWayland | Supported |

On Linux the game is located through `/proc` first, so a Wayland session needs
no X11 tooling. X11 support is optional and soft: when the X11 libraries are
present the loader can also match a window title, but the `/proc` scan works on
its own. Install the X11 development headers only if your distribution does not
already provide them.

## Building

You need CMake 3.20 or newer and a C++17 compiler. On Windows the slinkyFX
native engine in `SlinkyNative/` is built with the bundled LLVM/clang-cl
toolchain in `tools/llvm/`, so no separate compiler installation is required.
Python 3 is optional; when it is present the build also checks the native module
export ledger.

### Windows

Use the **x64 Native Tools Command Prompt for VS** (or any shell where `cl`
is available), then from the repository root run the cross-platform helper
under Git Bash, MSYS2 or Cygwin:

```sh
./build.sh
```

`build.sh` downloads LLVM 23.1.4 and Ninja into `tools/` on first run, builds
the slinkyFX native engine with clang-cl, builds the MSVC loader, and copies
both files to `dist/`.

You can also configure and build directly. The loader and engine are built
separately because the engine requires clang-cl/Ninja:

```bat
cmake -S . -B build -A x64
cmake -S SlinkyNative -B build/SlinkyNative -G Ninja ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_LINKER=lld-link -DCMAKE_AR=llvm-lib -DCMAKE_RC_COMPILER=llvm-rc
cmake --build build/SlinkyNative --config Release
cmake --build build --config Release
```

Then copy `build/SlinkyNative/slinky-native.dll` next to
`build/bin/Release/slinky-loader.exe`.

### Linux

Install a compiler, CMake, and the X11 development headers if your distribution
splits them out (`libx11-dev` on Debian/Ubuntu, `libX11-devel` on Fedora,
`libx11` on Arch):

```sh
sudo apt install build-essential cmake libx11-dev    # Debian / Ubuntu
sudo dnf install gcc-c++ cmake libX11-devel          # Fedora
sudo pacman -S base-devel cmake libx11               # Arch
./build.sh
```

The build produces `build/bin/slinky-loader` and `build/bin/libslinky-native.so`.
Keep them in the same folder.

### Cross-platform helper

`build.sh` works under Git Bash, MSYS2 and Cygwin on Windows. It configures,
builds, runs the export check when Python is available, and copies the pair into
`dist/`:

```sh
./build.sh          # configure + build + copy to dist/
./build.sh -Clean   # remove build/ first
```

## Running

Start Minecraft first, then run the loader:

- Windows: double-click `slinky-loader.exe` (or run it from a terminal).
- Linux: run `./slinky-loader` from the folder that contains
  `libslinky-native.so`.

The loader waits for the game, injects, and prints
`Injected successfully. Loader will close in 5s.` before it exits. If anything
fails you get the same block with `Failed to inject.` and the reason on the
license line, followed by the same countdown.

Open the in-game GUI with the key bind the client registers. It appears in
Minecraft's Controls screen and can be changed there.

---

<p align="center">This project carries an MIT license.</p>
