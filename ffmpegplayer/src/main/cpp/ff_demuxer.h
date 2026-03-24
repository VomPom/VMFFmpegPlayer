#ifndef FFMPEG_PLAYER_FF_DEMUXER_H
#define FFMPEG_PLAYER_FF_DEMUXER_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <string>
#include <mutex>

/**
 * FFmpeg Demuxer
 * Responsible for opening media files and reading audio/video packets
 */
class FFDemuxer {
public:
    FFDemuxer();

    ~FFDemuxer();

    /**
     * Open media file
     * @param path File path or fd pipe
     * @return 0 on success, negative on failure
     */
    int open(const char *path);

    /**
     * Open via file descriptor (for content:// URI)
     * @param fd File descriptor
     * @return 0 on success, negative on failure
     */
    int openFd(int fd);

    /**
     * Read next packet
     * @param packet Output packet (caller is responsible for unref)
     * @return 0 on success, AVERROR_EOF on end, other negative on failure
     */
    int readPacket(AVPacket *packet);

    /**
     * Seek to specified timestamp
     * @param timestampUs Timestamp in microseconds
     * @return 0 on success, negative on failure
     */
    int seek(int64_t timestampUs);

    /** Close and release resources */
    void close();

    // Stream info access
    int getVideoStreamIndex() const { return videoStreamIndex; }

    int getAudioStreamIndex() const { return audioStreamIndex; }

    AVCodecParameters *getVideoCodecParams() const;

    AVCodecParameters *getAudioCodecParams() const;

    int64_t getDurationUs() const;

    int getWidth() const;

    int getHeight() const;

    double getVideoFps() const;

    int getSampleRate() const;

    int getChannels() const;

    /** Get video stream time_base */
    AVRational getVideoTimeBase() const;

    /** Get audio stream time_base */
    AVRational getAudioTimeBase() const;

private:
    AVFormatContext *formatCtx = nullptr;
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    std::mutex readMutex;
};

#endif // FFMPEG_PLAYER_FF_DEMUXER_H
