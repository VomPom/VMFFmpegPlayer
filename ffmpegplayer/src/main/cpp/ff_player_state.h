#ifndef FFMPEG_PLAYER_FF_PLAYER_STATE_H
#define FFMPEG_PLAYER_FF_PLAYER_STATE_H

/**
 * FFPlayerState - 播放器状态枚举
 *
 * 独立头文件，避免各模块对 ff_player_context.h 的循环依赖。
 */
enum class FFPlayerState {
    IDLE = 0,
    PREPARING = 1,
    PREPARED = 2,
    PLAYING = 3,
    PAUSED = 4,
    STOPPED = 5,
    COMPLETED = 6,
    ERROR = 7
};

#endif // FFMPEG_PLAYER_FF_PLAYER_STATE_H
