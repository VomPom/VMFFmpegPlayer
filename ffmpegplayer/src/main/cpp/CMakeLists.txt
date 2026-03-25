cmake_minimum_required(VERSION 3.6.0)
project("ffmpegplayer")

# FFmpeg prebuilt library path
set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/ffmpeg)

# Add FFmpeg header file path
include_directories(${FFMPEG_DIR}/include)

# Define FFmpeg libraries
# libavformat
add_library(avformat SHARED IMPORTED)
set_target_properties(avformat PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_DIR}/libs/${ANDROID_ABI}/libavformat.so)

# libavcodec
add_library(avcodec SHARED IMPORTED)
set_target_properties(avcodec PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_DIR}/libs/${ANDROID_ABI}/libavcodec.so)

# libavutil
add_library(avutil SHARED IMPORTED)
set_target_properties(avutil PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_DIR}/libs/${ANDROID_ABI}/libavutil.so)

# libswresample
add_library(swresample SHARED IMPORTED)
set_target_properties(swresample PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_DIR}/libs/${ANDROID_ABI}/libswresample.so)

# libswscale
add_library(swscale SHARED IMPORTED)
set_target_properties(swscale PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_DIR}/libs/${ANDROID_ABI}/libswscale.so)

# OpenSSL libssl (HTTPS 支持)
add_library(ssl SHARED IMPORTED)
set_target_properties(ssl PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_DIR}/libs/${ANDROID_ABI}/libssl.so)

# OpenSSL libcrypto
add_library(crypto SHARED IMPORTED)
set_target_properties(crypto PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_DIR}/libs/${ANDROID_ABI}/libcrypto.so)

# Project native library
add_library(ffplayer SHARED
    ff_player_jni.cpp
    ff_player_context.cpp
    ff_demuxer.cpp
    ff_video_decoder.cpp
    ff_audio_decoder.cpp
    ff_av_sync.cpp
)

# Find system libraries
find_library(log-lib log)
find_library(android-lib android)
find_library(mediandk-lib mediandk)
find_library(opensles-lib OpenSLES)

# Link all libraries
target_link_libraries(ffplayer
    avformat
    avcodec
    avutil
    swresample
    swscale
    ssl
    crypto
    ${log-lib}
    ${android-lib}
    ${mediandk-lib}
    ${opensles-lib}
)