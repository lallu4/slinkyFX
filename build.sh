#!/usr/bin/env bash
# slinkyFX - single cross-platform build script.
#
#   Windows (Git Bash / MSYS2 / Cygwin): slinky-loader.exe + slinky-native.dll
#   Linux (x86_64):                      slinky-loader + libslinky-native.so (+ libslinky-engine.so for 1.7.10)
#
# Everything lands in ./dist. Keep the files in the same folder when running the loader.
#
# Usage:
#   ./build.sh          configure + build + verify + copy to dist/
#   ./build.sh -Clean   remove build/ first
#
# Needs: CMake 3.20+, a C++17 compiler. On Windows the slinkyFX native engine
#        in SlinkyNative/ is built with the bundled clang-cl toolchain in tools/llvm/.
#        Needs: Python 3 on Windows - the constant alignment check runs the built engine
#        through it and fails the build when a 16-byte constant would be read misaligned.
#        The module export ledger is optional and skipped when its script is not in the tree.
#
# On Linux, ./build.sh additionally produces build/linux-engine/libslinky-engine.so - the 1.7.10 client -
# when clang, lld and the LLVM tools are installed. It is built with the machine's own toolchain.
#
# The same file can be cross-built from Windows instead, which is how one .so is made to serve every
# distribution (it links against an old glibc to keep the requirement low):
#   tools/setup-linux-sysroot.sh && tools/build-linux-engine.sh
#
# Windows-only, and not part of ./build.sh: tools/check-linux-bootstrap.sh compiles the Linux sources here,
# so a mistake in the loader or the bootstrap is caught before someone else's build.
set -euo pipefail
cd "$(dirname "$0")"

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        -Clean|--clean) CLEAN=1 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

if [[ "$CLEAN" -eq 1 && -d build ]]; then
    rm -rf build
fi

# A working interpreter: on Windows `python3` is often the Microsoft Store
# execution alias, which exists on PATH but fails when run.
PYEXE=""
for cand in python python3 py; do
    if command -v "$cand" >/dev/null 2>&1 && "$cand" -c 'import sys' >/dev/null 2>&1; then
        PYEXE="$cand"
        break
    fi
done

case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*)
    # ------------------------------------------------------------------
    # Windows (MSVC x64 loader + slinkyFX native engine)
    # ------------------------------------------------------------------
    echo "[1/5] Configuring..."
    cmake -S . -B build -A x64

    echo "[2/5] Building slinkyFX native engine with clang-cl..."
    if [[ ! -f "$PWD/tools/llvm/clang+llvm-23.1.4-x86_64-pc-windows-msvc/bin/clang-cl.exe" ]]; then
        echo "       LLVM toolchain not found; running tools/setup-llvm.sh (one-time download)..."
        bash tools/setup-llvm.sh
    fi
    if [[ ! -f "$PWD/tools/ninja/ninja.exe" ]]; then
        echo "       Ninja not found; downloading..."
        mkdir -p tools/ninja-download tools/ninja
        curl -L -o tools/ninja-download/ninja-win.zip 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip'
        if [[ -z "$PYEXE" ]]; then
            echo "[X] need Python to unpack Ninja; install it or extract tools/ninja-download/ninja-win.zip into tools/ninja" >&2
            exit 1
        fi
        "$PYEXE" -c "import zipfile; zipfile.ZipFile('tools/ninja-download/ninja-win.zip').extractall('tools/ninja')"
    fi
    export PATH="$PWD/tools/llvm/clang+llvm-23.1.4-x86_64-pc-windows-msvc/bin:$PWD/tools/ninja:$PATH"
    cmake -S SlinkyNative -B build/SlinkyNative -G Ninja \
        -DCMAKE_C_COMPILER=clang-cl \
        -DCMAKE_CXX_COMPILER=clang-cl \
        -DCMAKE_LINKER=lld-link \
        -DCMAKE_AR=llvm-lib \
        -DCMAKE_RC_COMPILER=llvm-rc
    cmake --build build/SlinkyNative --config Release --parallel 4
    mkdir -p build/bin/Release
    cp -f build/SlinkyNative/slinky-native.dll build/bin/Release/slinky-native.dll

    echo "[3/5] Building loader (Release)..."
    cmake --build build --config Release

    echo "[4/5] Verifying the module export (hash ledger) and the constant alignment..."
    verifier="SlinkyNative/audit/VerifyExport.py"
    if [[ ! -f "$verifier" ]]; then
        echo "       (skipped: $verifier is not in the tree)"
    elif [[ -z "$PYEXE" ]]; then
        echo "       (skipped: python not found)"
    else
        report="build/verify_export_report.txt"
        "$PYEXE" "$verifier" >"$report" 2>&1 || true
        cat "$report"
        "$PYEXE" - "$report" <<'PY' || { echo "[X] VerifyExport.py failed" >&2; exit 1; }
import json, sys
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
try:
    data = json.loads(text[text.index("{"):])
except (ValueError, IndexError):
    sys.exit(2)
real = [f for f in data.get("failures", []) if not str(f).startswith("File hash mismatch: audits/")]
sys.exit(0 if not real and data.get("build_link_passed") else 1)
PY
        echo "       package code verified (audit-document ledger entries are reference-only)"
    fi

    # src/data/rdata.s is a sequential dump, so an insertion that is not a multiple of 16
    # bytes slides every later constant - and the 16-byte ones are read with aligned SSE
    # loads, which fault instead of misbehaving quietly. v1.3.0 shipped exactly that and
    # crashed on joining a world. The check runs against the built engine, not the source.
    aligner="SlinkyNative/audit/AlignAudit.py"
    if [[ ! -f "$aligner" ]]; then
        echo "[X] $aligner is missing; the constant alignment cannot be verified" >&2
        exit 1
    elif [[ -z "$PYEXE" ]]; then
        echo "[X] python is required to verify the constant alignment" >&2
        exit 1
    else
        "$PYEXE" "$aligner" "build/SlinkyNative/slinky-native.dll" || {
            echo "[X] a 16-byte constant is read misaligned - pad the insertion in $aligner's message to 16 bytes" >&2
            exit 1
        }
    fi

    for f in build/bin/Release/slinky-loader.exe build/bin/Release/slinky-native.dll; do
        if [[ ! -f "$f" ]]; then
            echo "[X] $(basename "$f") was not produced in build" >&2
            exit 1
        fi
    done

    echo "[5/5] Copying to dist/ (keep files in the same folder)"
    mkdir -p dist
    cp -f build/bin/Release/slinky-loader.exe build/bin/Release/slinky-native.dll dist/
    echo "[OK] dist/slinky-loader.exe + dist/slinky-native.dll"
    ;;
*)
    # ------------------------------------------------------------------
    # Linux
    # ------------------------------------------------------------------
    echo "[1/4] Configuring..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

    echo "[2/4] Building the loader and libslinky-native.so (the 1.8.9 client)..."
    cmake --build build -j"$(nproc)"

    for f in build/bin/slinky-loader build/bin/libslinky-native.so; do
        if [[ ! -f "$f" ]]; then
            echo "[X] $(basename "$f") was not produced in build/bin" >&2
            exit 1
        fi
    done

    # 1.7.10 runs the C++ engine, and on this platform it is a third file: libslinky-engine.so, which the
    # bootstrap dlopens from the folder the loader staged it in. It needs clang, lld and the LLVM tools rather
    # than the g++ that builds everything else above - the engine was written and verified against clang, its
    # warning set is clang's, and the alignment check below reads clang's disassembly - so it is a separate
    # step that a 1.8.9 player without those packages can walk straight past.
    echo "[3/4] Building libslinky-engine.so (the 1.7.10 client)..."
    engine_tools=""
    for t in clang clang++ ld.lld llvm-readelf llvm-objdump llvm-nm; do
        command -v "$t" >/dev/null 2>&1 || engine_tools="$engine_tools $t"
    done
    ENGINE_BUILT=0
    if [[ -n $engine_tools ]]; then
        echo "       skipped - missing:$engine_tools"
        echo "       1.8.9 is complete without it; for 1.7.10 install them and run tools/build-linux-engine.sh"
    else
        bash tools/build-linux-engine.sh || {
            echo "[X] the 1.7.10 engine failed to build above" >&2; exit 1; }
        ENGINE_BUILT=1
    fi

    echo "[4/4] Copying to dist/ (keep the files in the same folder)"
    mkdir -p dist
    cp -f build/bin/slinky-loader build/bin/libslinky-native.so dist/
    if [[ $ENGINE_BUILT -eq 1 ]]; then
        cp -f build/linux-engine/libslinky-engine.so dist/
        echo "[OK] dist/slinky-loader + dist/libslinky-native.so + dist/libslinky-engine.so"
    else
        echo "[OK] dist/slinky-loader + dist/libslinky-native.so   (1.8.9 only)"
    fi
    ;;
esac
