#!/bin/bash
# Build runner.c for all Android target ABIs using the NDK.
# Output: app/src/main/assets/binaries/runner-{aarch64,armhf,x86_64,x86}
#
# Usage:  ./build-runner.sh [ndk-dir]
# If ndk-dir is omitted, it's read from local.properties or ANDROID_NDK_HOME.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ASSETS_DIR="$SCRIPT_DIR/app/src/main/assets/binaries"
mkdir -p "$ASSETS_DIR"

# ── Locate NDK ──────────────────────────────────────────────────────────────

NDK=""
if [ -n "$1" ]; then
    NDK="$1"
elif [ -f "$SCRIPT_DIR/local.properties" ]; then
    NDK_DIR_LINE=$(grep '^ndk\.dir' "$SCRIPT_DIR/local.properties" | head -1 | cut -d= -f2)
    [ -n "$NDK_DIR_LINE" ] && NDK="$NDK_DIR_LINE"
fi
if [ -z "$NDK" ] && [ -n "$ANDROID_NDK_HOME" ]; then
    NDK="$ANDROID_NDK_HOME"
fi
if [ -z "$NDK" ] && [ -n "$ANDROID_HOME" ]; then
    NDK=$(ls -d "$ANDROID_HOME/ndk"/* 2>/dev/null | sort -V | tail -1)
fi

if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "NDK not found. Provide the path: ./build-runner.sh /path/to/ndk"
    echo "   Or set ANDROID_NDK_HOME or ndk.dir in local.properties"
    exit 1
fi

TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
if [ ! -d "$TOOLCHAIN" ]; then
    echo "Toolchain not found at $TOOLCHAIN"
    exit 1
fi

echo "NDK: $NDK"
echo ""

# ── Targets ─────────────────────────────────────────────────────────────────

TARGETS="aarch64 armhf x86_64 x86"

SRC="$SCRIPT_DIR/app/src/main/cpp/runner.c"

for ARCH in $TARGETS; do
    case "$ARCH" in
        aarch64) CC_TARGET="aarch64-linux-android21-clang" ;;
        armhf)   CC_TARGET="armv7a-linux-androideabi21-clang" ;;
        x86_64)  CC_TARGET="x86_64-linux-android21-clang" ;;
        x86)     CC_TARGET="i686-linux-android21-clang" ;;
    esac

    CC="$TOOLCHAIN/$CC_TARGET"
    OUTPUT="$ASSETS_DIR/runner-$ARCH"

    echo "  -> Building runner-$ARCH ..."
    "$CC" -std=c99 -Wall -Wextra -Wpedantic -Werror \
        -O2 -fstack-protector-strong \
        "$SRC" -o "$OUTPUT" -ldl

    "$TOOLCHAIN/llvm-strip" "$OUTPUT" 2>/dev/null || true

    SIZE=$(stat -c%s "$OUTPUT" 2>/dev/null || stat -f%z "$OUTPUT" 2>/dev/null)
    echo "    runner-$ARCH  ($SIZE bytes)"
done

echo ""
echo "All runners built in $ASSETS_DIR/"
