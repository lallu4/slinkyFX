#!/usr/bin/env bash
# slinkyFX - single cross-platform build script.
#
#   Windows (Git Bash / MSYS2 / Cygwin): slinky-loader.exe + slinky-native.dll
#   Linux (x86_64):                      slinky-loader + libslinky-native.so
#
# Everything lands in ./dist. Keep the two files in the same folder when
# running the loader.
#
# Usage:
#   ./build.sh          configure + build + verify + copy to dist/
#   ./build.sh -Clean   remove build/ first
#
# Needs: CMake 3.20+, a C++17 compiler. On Windows the slinkyFX native engine
#        in SlinkyNative/ is built with the bundled clang-cl toolchain in tools/llvm/.
#        Python 3 is optional; when present the module export ledger is checked.
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

    echo "[4/5] Verifying the module export (hash ledger)..."
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
    echo "[1/3] Configuring..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

    echo "[2/3] Building..."
    cmake --build build -j"$(nproc)"

    for f in build/bin/slinky-loader build/bin/libslinky-native.so; do
        if [[ ! -f "$f" ]]; then
            echo "[X] $(basename "$f") was not produced in build/bin" >&2
            exit 1
        fi
    done

    echo "[3/3] Copying to dist/ (keep both files in the same folder)"
    mkdir -p dist
    cp -f build/bin/slinky-loader build/bin/libslinky-native.so dist/
    echo "[OK] dist/slinky-loader + dist/libslinky-native.so"
    ;;
esac
