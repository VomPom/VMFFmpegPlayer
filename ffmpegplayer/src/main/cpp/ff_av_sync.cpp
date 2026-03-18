#include "ff_av_sync.h"

FFAVSync::FFAVSync() {
}

FFAVSync::SyncAction FFAVSync::sync(int64_t videoPtsUs, int64_t audioClockUs, int64_t &waitTimeUs) {
    waitTimeUs = 0;

    int64_t diff = videoPtsUs - audioClockUs;

    if (diff > syncThresholdUs) {
        // Video ahead of audio: need to wait
        waitTimeUs = diff;
        return WAIT;
    } else if (diff < -maxDropThresholdUs) {
        // Video severely behind audio: drop frame
        return DROP;
    } else {
        // Diff within acceptable range: render immediately
        return RENDER;
    }
}

void FFAVSync::reset() {
    // Current implementation needs no additional state reset
}
