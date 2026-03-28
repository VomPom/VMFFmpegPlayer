#ifndef FFMPEG_PLAYER_FF_TIMELINE_H
#define FFMPEG_PLAYER_FF_TIMELINE_H

#include <string>
#include <vector>
#include <cstdint>

/**
 * ClipDescriptor - 片段描述
 *
 * 描述一个视频文件中的一段时间区间，以及它在项目时间线上的位置。
 *
 * 时间映射关系：
 *   源文件 [srcStartUs, srcEndUs) → 时间线 [timelineStartUs, timelineEndUs)
 *
 * 例如：
 *   video1.mp4 的 2s~5s 映射到时间线的 0s~3s
 *   video2.mp4 的 0s~4s 映射到时间线的 3s~7s
 */
struct ClipDescriptor {
    std::string sourcePath;     // 源文件路径
    int fd = -1;                // 或通过 fd 打开（content:// URI）

    int64_t srcStartUs = 0;     // 源文件裁剪起点（微秒）
    int64_t srcEndUs = 0;       // 源文件裁剪终点（微秒），0 表示到文件末尾

    int64_t timelineStartUs = 0; // 在项目时间线上的起点（微秒）
    int64_t timelineEndUs = 0;   // 在项目时间线上的终点（微秒）

    /** 获取片段时长（微秒） */
    int64_t durationUs() const {
        return timelineEndUs - timelineStartUs;
    }

    /**
     * 将源文件 PTS 映射到项目时间线 PTS
     * @param srcPtsUs 源文件中的 PTS（微秒）
     * @return 项目时间线上的 PTS（微秒）
     */
    int64_t srcToTimelinePts(int64_t srcPtsUs) const {
        return srcPtsUs - srcStartUs + timelineStartUs;
    }

    /**
     * 将项目时间线 PTS 映射回源文件 PTS
     * @param timelinePtsUs 项目时间线上的 PTS（微秒）
     * @return 源文件中的 PTS（微秒）
     */
    int64_t timelineToSrcPts(int64_t timelinePtsUs) const {
        return timelinePtsUs - timelineStartUs + srcStartUs;
    }

    /**
     * 判断给定的时间线 PTS 是否在此片段范围内
     */
    bool containsTimelinePts(int64_t timelinePtsUs) const {
        return timelinePtsUs >= timelineStartUs && timelinePtsUs < timelineEndUs;
    }
};

/**
 * Track - 轨道
 *
 * 一组按时间排列的片段。Phase 1 只支持单轨道（主轨道）。
 * 片段之间不允许重叠，按 timelineStartUs 升序排列。
 */
struct Track {
    int trackId = 0;
    int zOrder = 0;             // 渲染层级（0=底层），Phase 1 不使用
    std::vector<ClipDescriptor> clips;

    /**
     * 根据时间线位置查找对应的片段索引
     * @param timelinePtsUs 项目时间线上的位置（微秒）
     * @return 片段索引，-1 表示不在任何片段范围内
     */
    int findClipIndex(int64_t timelinePtsUs) const {
        for (int i = 0; i < (int) clips.size(); i++) {
            if (clips[i].containsTimelinePts(timelinePtsUs)) {
                return i;
            }
        }
        return -1;
    }
};

/**
 * Timeline - 项目时间线
 *
 * 所有轨道的集合。Phase 1 只有一个主轨道（tracks[0]）。
 * 未来 Phase 3 支持多轨道叠加和转场。
 */
struct Timeline {
    std::vector<Track> tracks;
    int64_t totalDurationUs = 0; // 项目总时长（微秒）

    /**
     * 获取主轨道（第一个轨道）
     * @return 主轨道引用，如果没有轨道则行为未定义
     */
    Track &mainTrack() {
        return tracks[0];
    }

    const Track &mainTrack() const {
        return tracks[0];
    }

    /**
     * 计算并更新总时长（取所有轨道中最大的 timelineEndUs）
     */
    void updateDuration() {
        totalDurationUs = 0;
        for (const auto &track : tracks) {
            for (const auto &clip : track.clips) {
                if (clip.timelineEndUs > totalDurationUs) {
                    totalDurationUs = clip.timelineEndUs;
                }
            }
        }
    }

    /**
     * 是否为空时间线
     */
    bool empty() const {
        return tracks.empty() || tracks[0].clips.empty();
    }

    /**
     * 片段总数（主轨道）
     */
    int clipCount() const {
        if (tracks.empty()) return 0;
        return (int) tracks[0].clips.size();
    }
};

#endif // FFMPEG_PLAYER_FF_TIMELINE_H
