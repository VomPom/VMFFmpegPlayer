#!/bin/bash
# ============================================================
# FFmpeg Android 交叉编译脚本
# 用法: cd ffmpegplayer && bash build_ffmpeg.sh
# 输出: src/main/cpp/ffmpeg/include/ 和 libs/{arm64-v8a,armeabi-v7a}/
#       src/main/jniLibs/{arm64-v8a,armeabi-v7a}/
# ============================================================
set -e

# -------- 配置 --------
FFMPEG_VERSION="6.1.1"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"

# NDK 路径 (自动检测)
if [ -z "$ANDROID_NDK_HOME" ]; then
    if [ -z "$ANDROID_HOME" ]; then
        ANDROID_HOME="$HOME/Library/Android/sdk"
    fi
    NDK_PATH=$(ls -d "$ANDROID_HOME/ndk/"* 2>/dev/null | sort -V | tail -1)
else
    NDK_PATH="$ANDROID_NDK_HOME"
fi

if [ ! -d "$NDK_PATH" ]; then
    echo "❌ 找不到 Android NDK，请设置 ANDROID_NDK_HOME 环境变量"
    exit 1
fi
echo "✅ NDK 路径: $NDK_PATH"

TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/darwin-x86_64"
API=21

# 项目路径
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_ffmpeg"
OUTPUT_DIR="$SCRIPT_DIR/src/main/cpp/ffmpeg"
JNILIBS_DIR="$SCRIPT_DIR/src/main/jniLibs"

# 创建输出目录
mkdir -p "$OUTPUT_DIR/include"
mkdir -p "$OUTPUT_DIR/libs/arm64-v8a"
mkdir -p "$OUTPUT_DIR/libs/armeabi-v7a"
mkdir -p "$JNILIBS_DIR/arm64-v8a"
mkdir -p "$JNILIBS_DIR/armeabi-v7a"
mkdir -p "$BUILD_DIR"

# -------- 下载 FFmpeg 源码 --------
FFMPEG_SRC="$BUILD_DIR/ffmpeg-${FFMPEG_VERSION}"
if [ ! -d "$FFMPEG_SRC" ]; then
    echo "📦 下载 FFmpeg $FFMPEG_VERSION ..."
    cd "$BUILD_DIR"
    if [ ! -f "ffmpeg-${FFMPEG_VERSION}.tar.bz2" ]; then
        curl -L -o "ffmpeg-${FFMPEG_VERSION}.tar.bz2" "$FFMPEG_URL"
    fi
    echo "📂 解压..."
    tar xjf "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
else
    echo "✅ FFmpeg 源码已存在: $FFMPEG_SRC"
fi

# -------- 编译函数 --------
build_ffmpeg() {
    local ARCH=$1
    local ABI=$2
    local CROSS_PREFIX=$3
    local CC_PREFIX=$4
    local EXTRA_CFLAGS=$5
    local EXTRA_LDFLAGS=$6

    local PREFIX="$BUILD_DIR/output/$ABI"

    echo ""
    echo "=========================================="
    echo "🔨 编译 FFmpeg for $ABI (arch=$ARCH)"
    echo "=========================================="

    cd "$FFMPEG_SRC"

    # 清理上一次构建
    make clean 2>/dev/null || true

    ./configure \
        --prefix="$PREFIX" \
        --target-os=android \
        --arch="$ARCH" \
        --cpu="$ARCH" \
        --cc="${TOOLCHAIN}/bin/${CC_PREFIX}${API}-clang" \
        --cxx="${TOOLCHAIN}/bin/${CC_PREFIX}${API}-clang++" \
        --cross-prefix="${TOOLCHAIN}/bin/${CROSS_PREFIX}-" \
        --nm="${TOOLCHAIN}/bin/llvm-nm" \
        --ar="${TOOLCHAIN}/bin/llvm-ar" \
        --ranlib="${TOOLCHAIN}/bin/llvm-ranlib" \
        --strip="${TOOLCHAIN}/bin/llvm-strip" \
        --enable-cross-compile \
        --enable-shared \
        --disable-static \
        --disable-programs \
        --disable-doc \
        --disable-htmlpages \
        --disable-manpages \
        --disable-podpages \
        --disable-txtpages \
        --disable-avdevice \
        --disable-postproc \
        --disable-avfilter \
        --disable-symver \
        --disable-debug \
        --disable-asm \
        --enable-jni \
        --enable-mediacodec \
        --enable-decoder=h264_mediacodec \
        --enable-decoder=hevc_mediacodec \
        --enable-decoder=aac \
        --enable-decoder=mp3 \
        --enable-decoder=pcm_s16le \
        --enable-decoder=h264 \
        --enable-decoder=hevc \
        --enable-demuxer=mov \
        --enable-demuxer=mp4 \
        --enable-demuxer=matroska \
        --enable-demuxer=mpegts \
        --enable-demuxer=flv \
        --enable-demuxer=avi \
        --enable-demuxer=mp3 \
        --enable-demuxer=aac \
        --enable-demuxer=wav \
        --enable-protocol=file \
        --enable-protocol=fd \
        --enable-protocol=pipe \
        --enable-parser=h264 \
        --enable-parser=hevc \
        --enable-parser=aac \
        --enable-parser=mpegaudio \
        --enable-small \
        --extra-cflags="$EXTRA_CFLAGS" \
        --extra-ldflags="$EXTRA_LDFLAGS" \
        --pkg-config=/dev/null

    echo "🔨 编译中 ($ABI)..."
    make -j$(sysctl -n hw.ncpu)
    make install

    # 复制 .so 到项目目录
    echo "📁 复制 .so 到项目目录 ($ABI)..."
    cp -f "$PREFIX/lib/"*.so "$OUTPUT_DIR/libs/$ABI/"
    cp -f "$PREFIX/lib/"*.so "$JNILIBS_DIR/$ABI/"

    echo "✅ $ABI 编译完成!"
}

# -------- 编译 arm64-v8a --------
build_ffmpeg \
    "aarch64" \
    "arm64-v8a" \
    "aarch64-linux-android" \
    "aarch64-linux-android" \
    "" \
    ""

# -------- 编译 armeabi-v7a --------
build_ffmpeg \
    "arm" \
    "armeabi-v7a" \
    "arm-linux-androideabi" \
    "armv7a-linux-androideabi" \
    "-march=armv7-a -mfloat-abi=softfp -mfpu=neon" \
    "-march=armv7-a -Wl,--fix-cortex-a8"

# -------- 复制头文件（只需一份） --------
echo ""
echo "📁 复制头文件..."
FIRST_PREFIX="$BUILD_DIR/output/arm64-v8a"
cp -rf "$FIRST_PREFIX/include/"* "$OUTPUT_DIR/include/"

# -------- 清理临时文件 --------
echo ""
echo "🧹 是否删除临时编译文件？(y/n)"
read -r CLEAN_UP
if [ "$CLEAN_UP" = "y" ]; then
    rm -rf "$BUILD_DIR"
    echo "✅ 临时文件已删除"
else
    echo "💡 临时文件保留在: $BUILD_DIR"
fi

echo ""
echo "=========================================="
echo "🎉 FFmpeg 编译全部完成!"
echo "=========================================="
echo "头文件: $OUTPUT_DIR/include/"
echo "arm64-v8a:   $OUTPUT_DIR/libs/arm64-v8a/"
echo "armeabi-v7a: $OUTPUT_DIR/libs/armeabi-v7a/"
echo "jniLibs:     $JNILIBS_DIR/"
echo ""
echo "📌 下一步: 取消 build.gradle 中 CMake 配置的注释，重新编译项目"
