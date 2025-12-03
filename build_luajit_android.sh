#!/bin/bash

# 配置
NDK_PATH="E:/SDK/Android/ndk/27.0.12077973"
MIN_SDK=28
LUAJIT_SRC="path/to/LuaJIT"
OUTPUT_DIR="luadroid/src/main/jniLibs"

# 设置工具链
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/windows-x86_64"

# 编译 arm64-v8a
echo "Building for arm64-v8a..."
cd "$LUAJIT_SRC"
make clean

make HOST_CC="gcc -m64" \
     CROSS="$TOOLCHAIN/bin/aarch64-linux-android$MIN_SDK-" \
     TARGET_SYS=Linux \
     TARGET_FLAGS="-fPIC" \
     BUILDMODE=dynamic

mkdir -p "$OUTPUT_DIR/arm64-v8a"
cp src/libluajit.so "$OUTPUT_DIR/arm64-v8a/"

echo "Build complete!"
