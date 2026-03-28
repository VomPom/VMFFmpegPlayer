#ifndef FFMPEG_PLAYER_FF_PACKET_QUEUE_H
#define FFMPEG_PLAYER_FF_PACKET_QUEUE_H

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
}

/**
 * FFPacketQueue - 线程安全的 AVPacket 队列
 *
 * 用于读取线程与解码线程之间的数据传递。
 * 支持阻塞式 push/pop、容量控制、EOF 标记和中止操作。
 */
struct FFPacketQueue {
    std::vector<AVPacket *> packets;
    std::mutex mutex;
    std::condition_variable cond;
    std::atomic<bool> eof{false};
    std::atomic<bool> aborted{false};
    int maxSize = 128;

    void push(AVPacket *pkt) {
        std::unique_lock<std::mutex> lock(mutex);
        while ((int) packets.size() >= maxSize && !aborted.load()) {
            cond.wait_for(lock, std::chrono::milliseconds(10));
        }
        if (aborted.load()) {
            av_packet_free(&pkt);
            return;
        }
        packets.push_back(pkt);
        cond.notify_one();
    }

    AVPacket *pop() {
        std::unique_lock<std::mutex> lock(mutex);
        while (packets.empty() && !eof.load() && !aborted.load()) {
            cond.wait_for(lock, std::chrono::milliseconds(10));
        }
        if (packets.empty()) return nullptr;
        AVPacket *pkt = packets.front();
        packets.erase(packets.begin());
        cond.notify_one();
        return pkt;
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto *pkt : packets) {
            av_packet_free(&pkt);
        }
        packets.clear();
        eof.store(false);
        aborted.store(false);
        cond.notify_all();
    }

    int size() {
        std::lock_guard<std::mutex> lock(mutex);
        return (int) packets.size();
    }

    void abort() {
        aborted.store(true);
        eof.store(true);
        cond.notify_all();
    }
};

#endif // FFMPEG_PLAYER_FF_PACKET_QUEUE_H
