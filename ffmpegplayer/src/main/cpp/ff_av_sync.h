#ifndef FFMPEG_PLAYER_FF_AV_SYNC_H
#define FFMPEG_PLAYER_FF_AV_SYNC_H

#include <cstdint>
#include <atomic>

/**
 * Audio/Video Sync Manager
 * Uses audio clock as master clock to control video frame rendering pace
 */
class FFAVSync {
public:
    enum SyncAction {
        RENDER,     // Render immediately
        DROP,       // Drop frame (video lagging too much)
        WAIT        // Wait (video ahead)
    };

    FFAVSync();

    /**
     * Determine how to handle the current video frame
     * @param videoPtsUs Video frame PTS (microseconds)
     * @param audioClockUs Audio clock (microseconds)
     * @param waitTimeUs If waiting is needed, output wait time (microseconds)
     * @return SyncAction
     */
    SyncAction sync(int64_t videoPtsUs, int64_t audioClockUs, int64_t &waitTimeUs);

    /** Reset sync state (call after seek) */
    void reset();

    /** Set sync threshold */
    void setSyncThresholdUs(int64_t thresholdUs) { this->syncThresholdUs = thresholdUs; }

private:
    // Sync threshold: wait if video is ahead of audio by more than this value
    int64_t syncThresholdUs = 40000; // 40ms
    // Max drop threshold: drop frames only if video is behind audio by more than this value
    int64_t maxDropThresholdUs = 100000; // 100ms
};

#endif // FFMPEG_PLAYER_FF_AV_SYNC_H