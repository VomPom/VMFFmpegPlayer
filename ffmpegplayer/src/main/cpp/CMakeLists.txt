cmake_minimum_required(VERSION 3.6.0)
project("ffmpegplayer")

# FFmpeg prebuilt library path
set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/ffmpeg)

# lib_effect 特效框架路径
set(EFFECT_DIR ${CMAKE_SOURCE_DIR}/../../../../lib_effect/src/main/cpp)

# Add FFmpeg header file path
include_directories(${FFMPEG_DIR}/include)

# 添加本模块头文件路径（ff_log.h 等公共头文件，供 lib_effect 源文件编译时使用）
include_directories(${CMAKE_SOURCE_DIR})

# 添加 lib_effect 头文件路径
include_directories(${EFFECT_DIR})

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
    ff_jni_callback.cpp
    ff_read_thread.cpp
    ff_video_render_loop.cpp
    ff_audio_render_loop.cpp
    ff_demuxer.cpp
    ff_video_decoder.cpp
    ff_audio_decoder.cpp
    ff_av_sync.cpp
    ff_media_source.cpp
    # lib_effect 特效框架源文件
    ${EFFECT_DIR}/effect_pipeline.cpp
    ${EFFECT_DIR}/speed_effect.cpp
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