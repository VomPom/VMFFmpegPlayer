# FFmpegPlayer

基于 **FFmpeg 6.1.1 + Android MediaCodec 硬解 + OpenSL ES** 的 Android 视频播放器。

## 功能特性

-  **视频硬解码**：通过 Android MediaCodec 实现 H.264 / HEVC 硬件解码，直接输出到 ANativeWindow
-  **音频软解码**：FFmpeg 软解 AAC / MP3 等音频格式，OpenSL ES 低延迟输出
-  **音视频同步**：基于音频时钟的 A/V 同步策略
-  **完整播放控制**：播放、暂停、恢复、停止、Seek、重置
-  **画面自适应**：TextureView + Matrix 变换，自动适配视频宽高比
-  **JNI 动态注册**：使用 `JNI_OnLoad` 动态注册 Native 方法

## 架构

```
┌─────────────────────────────────────────────────────┐
│                   App (Kotlin)                      │
│  MainActivity ──→ FFPlayer ──→ FFPlayerView          │
│                     │                                │
│                     │ JNI (动态注册)                   │
├─────────────────────┼───────────────────────────────┤
│                     ▼         Native (C++)           │
│               FFPlayerContext                         │
│              ┌──────┴──────────┐                     │
│              ▼                 ▼                     │
│         FFDemuxer        FFAVSync                    │
│        (FFmpeg 解封装)   (音视频同步)                   │
│              │                                       │
│       ┌──────┴──────┐                                │
│       ▼              ▼                               │
│  FFVideoDecoder  FFAudioDecoder                      │
│  (MediaCodec)    (FFmpeg 软解)                        │
│       │              │                               │
│       ▼              ▼                               │
│  ANativeWindow   OpenSL ES                           │
└─────────────────────────────────────────────────────┘
```

## 构建与运行

### 1. 克隆项目

```bash
git clone <repository-url>
cd FFmpegPlayer
```

### 2. 编译 FFmpeg（如需从源码重新编译）

项目已包含预编译的 FFmpeg .so 文件（`arm64-v8a` 和 `armeabi-v7a`），可直接跳过此步骤。

如需从源码重新编译：

```bash
cd ffmpegplayer
bash build_ffmpeg.sh
```

> **注意**：编译脚本会自动下载 FFmpeg 6.1.1 源码并进行交叉编译，需要确保已安装 Android NDK。
> 脚本会自动检测 `$ANDROID_NDK_HOME` 或 `$ANDROID_HOME/ndk/` 下的 NDK 路径。

FFmpeg 编译已启用的关键功能：
- **解码器**：h264, hevc, h264_mediacodec, hevc_mediacodec, aac, mp3, pcm_s16le
- **解封装器**：mov, mp4, matroska, mpegts, flv, avi, mp3, aac, wav
- **协议**：file, fd, pipe
- **解析器**：h264, hevc, aac, mpegaudio

## 使用方式

### 基本用法

```kotlin
// 1. 创建播放器
val player = FFPlayer()

// 2. 设置回调
player.setListener(object : FFPlayer.Listener {
    override fun onPrepared(durationMs: Long) { /* 准备就绪 */ }
    override fun onCompletion() { /* 播放完成 */ }
    override fun onError(code: Int, message: String) { /* 错误 */ }
    override fun onStateChanged(state: Int) { /* 状态变化 */ }
    override fun onVideoSizeChanged(width: Int, height: Int) { /* 尺寸变化 */ }
    override fun onProgress(currentMs: Long, totalMs: Long) { /* 进度 */ }
})

// 3. 绑定 FFPlayerView
val playerView = findViewById<FFPlayerView>(R.id.playerView)
playerView.bindPlayer(player)

// 4. 设置数据源并播放
player.setDataSource("/sdcard/video.mp4")
// 或使用 content:// URI
// player.setDataSource(context, uri)
player.prepareAsync()

// 5. 在 onPrepared 回调中开始播放
player.start()
```

### 播放控制

```kotlin
player.pause()                // 暂停
player.resume()               // 恢复
player.seekTo(positionMs)     // Seek 到指定位置（毫秒）
player.stop()                 // 停止
player.reset()                // 重置（可重新 prepare）
player.release()              // 释放资源

// 画面适配模式设置
playerView.setScaleType(FFPlayerView.ScaleType.CENTER_INSIDE)  // 居中显示（默认）
playerView.setScaleType(FFPlayerView.ScaleType.CENTER_CROP)    // 裁剪填充
playerView.setScaleType(FFPlayerView.ScaleType.FIT_XY)         // 拉伸铺满
playerView.setScaleType(FFPlayerView.ScaleType.ORIGINAL)      // 原始尺寸
```

### 画面适配模式说明

| 模式 | 说明 | 适用场景 |
|------|------|----------|
| **CENTER_INSIDE**（居中显示） | 保持视频原始宽高比，完整显示视频内容，不裁剪 | 默认模式，适合大多数场景 |
| **CENTER_CROP**（裁剪填充） | 保持视频原始宽高比，裁剪视频边缘以填充整个视图 | 全屏播放，避免黑边 |
| **FIT_XY**（拉伸铺满） | 拉伸视频以完全填充视图，会变形 | 特殊需求，不推荐使用 |
| **ORIGINAL**（原始尺寸） | 以视频原始尺寸显示，不缩放 | 需要查看原始像素的场景 |

### 播放器状态

| 常量 | 值 | 说明 |
|------|---|------|
| `STATE_IDLE` | 0 | 空闲 |
| `STATE_PREPARING` | 1 | 准备中 |
| `STATE_PREPARED` | 2 | 准备就绪 |
| `STATE_PLAYING` | 3 | 播放中 |
| `STATE_PAUSED` | 4 | 暂停 |
| `STATE_STOPPED` | 5 | 停止 |
| `STATE_COMPLETED` | 6 | 播放完成 |
| `STATE_ERROR` | 7 | 错误 |


## 技术细节

### 解封装
- 使用 FFmpeg `avformat` 进行解封装，支持文件路径和文件描述符 (fd) 两种数据源方式
- 自动检测视频流和音频流，并应用 Bitstream Filter 进行格式转换

### 视频解码
- 通过 Android NDK 的 `AMediaCodec` API 实现 MediaCodec 硬件解码
- 直接输出到 `ANativeWindow`（即 Surface），零拷贝渲染

### 音频解码
- 使用 FFmpeg `avcodec` 进行音频软解码
- 通过 `libswresample` 重采样为统一的 S16 / 44100Hz / Stereo 格式
- OpenSL ES 作为音频输出引擎

### 音视频同步
- 以音频播放时钟为基准
- 视频帧根据 PTS 与音频时钟的差值进行延迟或丢帧处理

## License

本项目仅供学习和参考使用。FFmpeg 遵循 LGPL/GPL 许可证，请根据实际使用场景遵守相关协议。
