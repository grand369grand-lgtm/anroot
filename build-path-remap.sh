#!/bin/bash
# Build libpath_remap.so for all Android architectures
set -e

NDK=/tmp/my-project/android-sdk/sdk/ndk/29.0.14206865
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
SRC=app/src/main/jni/path-remap/path_remap.c
OUT_DIR=app/src/main/res/raw

mkdir -p "$OUT_DIR"

# Build for aarch64
echo "Building for aarch64..."
$TOOLCHAIN/bin/aarch64-linux-android21-clang -shared -fPIC -O2 -o "$OUT_DIR/libpath_remap_aarch64.so" "$SRC"
echo "  Done: $OUT_DIR/libpath_remap_aarch64.so"

# Build for arm
echo "Building for arm..."
$TOOLCHAIN/bin/armv7a-linux-androideabi21-clang -shared -fPIC -O2 -o "$OUT_DIR/libpath_remap_arm.so" "$SRC"
echo "  Done: $OUT_DIR/libpath_remap_arm.so"

# Build for x86_64
echo "Building for x86_64..."
$TOOLCHAIN/bin/x86_64-linux-android21-clang -shared -fPIC -O2 -o "$OUT_DIR/libpath_remap_x86_64.so" "$SRC"
echo "  Done: $OUT_DIR/libpath_remap_x86_64.so"

# Build for i686
echo "Building for i686..."
$TOOLCHAIN/bin/i686-linux-android21-clang -shared -fPIC -O2 -o "$OUT_DIR/libpath_remap_i686.so" "$SRC"
echo "  Done: $OUT_DIR/libpath_remap_i686.so"

echo "All builds complete!"
ls -la "$OUT_DIR"/libpath_remap_*.so
