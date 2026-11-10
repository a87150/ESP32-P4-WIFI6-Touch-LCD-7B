/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace esp_brookesia::app::video::mp4 {

/**
 * @brief One coded video sample inside an MP4 file.
 *
 * Samples are in decode order; `offset` is an absolute file offset. H.264
 * samples are stored as length-prefixed NAL units (AVCC), exactly as they are
 * laid out in the mdat box.
 */
struct Sample {
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t duration_ms = 0;
    bool keyframe = false;
};

/**
 * @brief Parsed description of the first H.264 video track of an MP4 file.
 */
struct Track {
    bool valid = false;
    bool h264 = false;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t fps = 12;
    uint32_t duration_ms = 0;
    // Size of the NAL length field in the samples (usually 4).
    uint8_t nal_length_size = 4;
    // Annex-B SPS + PPS (with 00 00 00 01 start codes).
    std::vector<uint8_t> extradata;
    std::vector<Sample> samples;
    // Human readable reason when `valid` is false.
    std::string error;
};

/**
 * @brief Demux the first H.264 video track from an ISO BMFF (MP4/MOV) file.
 *
 * Only the H.264 (avc1/avc3) sample tables are read; audio tracks are ignored.
 * The file handle is not retained, the caller is free to close it afterwards.
 *
 * @param file       Open file positioned anywhere.
 * @param file_size  Total size of the file in bytes.
 * @param track      Output description.
 * @return true on success (track.valid == true).
 */
bool parse(std::FILE *file, uint64_t file_size, Track &track);

} // namespace esp_brookesia::app::video::mp4
