/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/app_video/mp4_demuxer.hpp"

#include <algorithm>
#include <cstring>

#include <esp_log.h>

namespace esp_brookesia::app::video::mp4 {

namespace {

constexpr const char *TAG = "VideoMp4";

constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    // Box types are stored as four big-endian ASCII bytes.
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

uint16_t rd16(const uint8_t *p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t rd32(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint64_t rd64(const uint8_t *p)
{
    return (static_cast<uint64_t>(rd32(p)) << 32) | rd32(p + 4);
}

struct View {
    const uint8_t *data = nullptr;
    size_t size = 0;
};

// Walk the sibling boxes inside `view`. The callback returns false to stop.
template <typename Fn>
bool for_each_box(const View &view, Fn &&fn)
{
    size_t offset = 0;
    while (offset + 8 <= view.size) {
        uint64_t box_size = rd32(view.data + offset);
        const uint32_t type = rd32(view.data + offset + 4);
        size_t header = 8;
        if (box_size == 1) {
            if (offset + 16 > view.size) {
                return false;
            }
            box_size = rd64(view.data + offset + 8);
            header = 16;
        } else if (box_size == 0) {
            box_size = view.size - offset;
        }
        if (box_size < header || offset + box_size > view.size) {
            return false;
        }
        const View child{view.data + offset + header, static_cast<size_t>(box_size - header)};
        if (!fn(type, child)) {
            return false;
        }
        offset += static_cast<size_t>(box_size);
    }
    return true;
}

struct BitReader {
    const uint8_t *data;
    size_t total_bits;
    size_t pos = 0;

    bool read_bit(uint32_t &out)
    {
        if (pos >= total_bits) {
            return false;
        }
        out = (data[pos >> 3] >> (7 - (pos & 7))) & 1U;
        ++pos;
        return true;
    }

    bool read_bits(int count, uint32_t &out)
    {
        uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            uint32_t bit = 0;
            if (!read_bit(bit)) {
                return false;
            }
            value = (value << 1) | bit;
        }
        out = value;
        return true;
    }

    bool read_ue(uint32_t &out)
    {
        int zeros = 0;
        uint32_t bit = 0;
        while (true) {
            if (!read_bit(bit)) {
                return false;
            }
            if (bit != 0) {
                break;
            }
            if (++zeros > 31) {
                return false;
            }
        }
        uint32_t rest = 0;
        if (zeros > 0 && !read_bits(zeros, rest)) {
            return false;
        }
        out = ((1U << zeros) - 1U) + rest;
        return true;
    }

    bool read_se(int32_t &out)
    {
        uint32_t value = 0;
        if (!read_ue(value)) {
            return false;
        }
        out = (value & 1U) ? static_cast<int32_t>((value + 1U) / 2U) :
              -static_cast<int32_t>(value / 2U);
        return true;
    }
};

// Parse the SPS (including its 1-byte NAL header) and derive the cropped size.
bool parse_sps_dimensions(const uint8_t *nal, size_t length, uint16_t &width, uint16_t &height)
{
    if (length < 4) {
        return false;
    }
    // Strip emulation prevention bytes after the NAL header.
    std::vector<uint8_t> rbsp;
    rbsp.reserve(length);
    for (size_t i = 1; i < length; ++i) {
        if (i + 2 < length && nal[i] == 0 && nal[i + 1] == 0 && nal[i + 2] == 3) {
            rbsp.push_back(0);
            rbsp.push_back(0);
            i += 2;
            continue;
        }
        rbsp.push_back(nal[i]);
    }

    BitReader reader{rbsp.data(), rbsp.size() * 8};
    uint32_t profile = 0;
    uint32_t ignored = 0;
    uint32_t level = 0;
    if (!reader.read_bits(8, profile) || !reader.read_bits(8, ignored) || !reader.read_bits(8, level)) {
        return false;
    }
    uint32_t sps_id = 0;
    if (!reader.read_ue(sps_id)) {
        return false;
    }

    uint32_t chroma_format_idc = 1;
    switch (profile) {
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135: {
        if (!reader.read_ue(chroma_format_idc)) {
            return false;
        }
        if (chroma_format_idc == 3) {
            uint32_t separate_colour_plane = 0;
            if (!reader.read_bit(separate_colour_plane)) {
                return false;
            }
        }
        uint32_t value = 0;
        if (!reader.read_ue(value) || !reader.read_ue(value)) {  // bit depths
            return false;
        }
        uint32_t qpprime = 0;
        if (!reader.read_bit(qpprime)) {
            return false;
        }
        uint32_t scaling = 0;
        if (!reader.read_bit(scaling)) {
            return false;
        }
        if (scaling != 0) {
            const int lists = (chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < lists; i++) {
                uint32_t present = 0;
                if (!reader.read_bit(present)) {
                    return false;
                }
                if (present == 0) {
                    continue;
                }
                const int entries = (i < 6) ? 16 : 64;
                int32_t last = 8;
                int32_t next = 8;
                for (int j = 0; j < entries; j++) {
                    int32_t delta = 0;
                    if (!reader.read_se(delta)) {
                        return false;
                    }
                    if (next != 0) {
                        next = (last + delta + 256) % 256;
                    }
                    last = (next == 0) ? last : next;
                }
            }
        }
        break;
    }
    default:
        break;
    }

    uint32_t value = 0;
    if (!reader.read_ue(value)) {  // log2_max_frame_num_minus4
        return false;
    }
    uint32_t poc_type = 0;
    if (!reader.read_ue(poc_type)) {
        return false;
    }
    if (poc_type == 0) {
        if (!reader.read_ue(value)) {
            return false;
        }
    } else if (poc_type == 1) {
        uint32_t flag = 0;
        int32_t se = 0;
        if (!reader.read_bit(flag) || !reader.read_se(se) || !reader.read_se(se)) {
            return false;
        }
        uint32_t cycle = 0;
        if (!reader.read_ue(cycle)) {
            return false;
        }
        for (uint32_t i = 0; i < cycle; i++) {
            if (!reader.read_se(se)) {
                return false;
            }
        }
    }
    if (!reader.read_ue(value)) {  // max_num_ref_frames
        return false;
    }
    uint32_t gaps = 0;
    if (!reader.read_bit(gaps)) {
        return false;
    }
    uint32_t width_mbs = 0;
    uint32_t height_map = 0;
    if (!reader.read_ue(width_mbs) || !reader.read_ue(height_map)) {
        return false;
    }
    uint32_t frame_mbs_only = 0;
    if (!reader.read_bit(frame_mbs_only)) {
        return false;
    }
    if (frame_mbs_only == 0) {
        uint32_t mb_adaptive = 0;
        if (!reader.read_bit(mb_adaptive)) {
            return false;
        }
    }
    uint32_t direct_8x8 = 0;
    if (!reader.read_bit(direct_8x8)) {
        return false;
    }
    uint32_t cropping = 0;
    if (!reader.read_bit(cropping)) {
        return false;
    }
    uint32_t crop_left = 0;
    uint32_t crop_right = 0;
    uint32_t crop_top = 0;
    uint32_t crop_bottom = 0;
    if (cropping != 0) {
        if (!reader.read_ue(crop_left) || !reader.read_ue(crop_right) ||
                !reader.read_ue(crop_top) || !reader.read_ue(crop_bottom)) {
            return false;
        }
    }

    int w = static_cast<int>((width_mbs + 1U) * 16U);
    int h = static_cast<int>((2U - frame_mbs_only) * (height_map + 1U) * 16U);
    if (cropping != 0) {
        const int crop_unit_x = (chroma_format_idc == 0) ? 1 : 2;
        const int crop_unit_y = (chroma_format_idc == 0) ? static_cast<int>(2U - frame_mbs_only) :
                                static_cast<int>(2U * (2U - frame_mbs_only));
        w -= static_cast<int>(crop_left + crop_right) * crop_unit_x;
        h -= static_cast<int>(crop_top + crop_bottom) * crop_unit_y;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }
    width = static_cast<uint16_t>(w);
    height = static_cast<uint16_t>(h);
    return true;
}

struct RawTrack {
    bool has_video = false;
    bool unsupported_codec = false;
    std::string codec;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t timescale = 0;
    std::vector<uint8_t> avcc;
    std::vector<uint32_t> sizes;
    std::vector<uint64_t> chunk_offsets;
    struct StscEntry {
        uint32_t first_chunk = 0;
        uint32_t samples_per_chunk = 0;
    };
    std::vector<StscEntry> stsc;
    struct SttsEntry {
        uint32_t count = 0;
        uint32_t delta = 0;
    };
    std::vector<SttsEntry> stts;
    std::vector<uint32_t> sync_samples;
    bool has_stss = false;
};

void parse_mdhd(const View &box, uint32_t &timescale)
{
    if (box.size < 20) {
        return;
    }
    const uint8_t version = box.data[0];
    if (version == 1) {
        if (box.size >= 28) {
            timescale = rd32(box.data + 20);
        }
    } else {
        timescale = rd32(box.data + 12);
    }
}

bool parse_stsd(const View &box, RawTrack &raw)
{
    if (box.size < 8) {
        return false;
    }
    const uint32_t entry_count = rd32(box.data + 4);
    size_t pos = 8;
    for (uint32_t i = 0; i < entry_count && pos + 8 <= box.size; i++) {
        const uint32_t entry_size = rd32(box.data + pos);
        const uint32_t format = rd32(box.data + pos + 4);
        if (entry_size < 8 || pos + entry_size > box.size) {
            return false;
        }
        if (format == fourcc('a', 'v', 'c', '1') || format == fourcc('a', 'v', 'c', '3')) {
            raw.has_video = true;
            raw.codec = "H.264";
            // VisualSampleEntry: 6 reserved + 2 data ref + 16 pre-defined,
            // then width / height, 78 bytes after the box header.
            if (entry_size >= 8 + 78) {
                raw.width = rd16(box.data + pos + 8 + 24);
                raw.height = rd16(box.data + pos + 8 + 26);
                const View children{box.data + pos + 8 + 78, entry_size - 8 - 78};
                (void)for_each_box(children, [&raw](uint32_t type, const View &child) {
                    if (type == fourcc('a', 'v', 'c', 'C')) {
                        raw.avcc.assign(child.data, child.data + child.size);
                    }
                    return true;
                });
            }
        } else if (format == fourcc('h', 'v', 'c', '1') || format == fourcc('h', 'e', 'v', '1')) {
            raw.has_video = true;
            raw.codec = "HEVC";
            raw.unsupported_codec = true;
        } else if (format == fourcc('m', 'p', '4', 'v') || format == fourcc('v', 'p', '0', '9') ||
                   format == fourcc('a', 'v', '0', '1')) {
            raw.has_video = true;
            raw.codec = (format == fourcc('m', 'p', '4', 'v')) ? "MPEG-4" : "VP9/AV1";
            raw.unsupported_codec = true;
        }
        pos += entry_size;
    }
    // Audio tracks also have an stsd box: never fail the whole parse here,
    // `finish_track` reports a missing video sample description later.
    return true;
}

void parse_stsz(const View &box, RawTrack &raw)
{
    if (box.size < 12) {
        return;
    }
    const uint32_t sample_size = rd32(box.data + 4);
    const uint32_t sample_count = rd32(box.data + 8);
    if (sample_count > 300000) {
        return;
    }
    raw.sizes.resize(sample_count);
    if (sample_size != 0) {
        std::fill(raw.sizes.begin(), raw.sizes.end(), sample_size);
        return;
    }
    if (box.size < 12 + static_cast<size_t>(sample_count) * 4) {
        raw.sizes.clear();
        return;
    }
    for (uint32_t i = 0; i < sample_count; i++) {
        raw.sizes[i] = rd32(box.data + 12 + static_cast<size_t>(i) * 4);
    }
}

void parse_stco(const View &box, bool large, RawTrack &raw)
{
    if (box.size < 8) {
        return;
    }
    const uint32_t count = rd32(box.data + 4);
    const size_t entry_size = large ? 8 : 4;
    if (box.size < 8 + static_cast<size_t>(count) * entry_size || count > 300000) {
        return;
    }
    raw.chunk_offsets.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        raw.chunk_offsets[i] = large ? rd64(box.data + 8 + static_cast<size_t>(i) * 8) :
                               rd32(box.data + 8 + static_cast<size_t>(i) * 4);
    }
}

void parse_stsc(const View &box, RawTrack &raw)
{
    if (box.size < 8) {
        return;
    }
    const uint32_t count = rd32(box.data + 4);
    if (box.size < 8 + static_cast<size_t>(count) * 12 || count > 100000) {
        return;
    }
    raw.stsc.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = box.data + 8 + static_cast<size_t>(i) * 12;
        raw.stsc[i].first_chunk = rd32(entry);
        raw.stsc[i].samples_per_chunk = rd32(entry + 4);
    }
}

void parse_stts(const View &box, RawTrack &raw)
{
    if (box.size < 8) {
        return;
    }
    const uint32_t count = rd32(box.data + 4);
    if (box.size < 8 + static_cast<size_t>(count) * 8 || count > 100000) {
        return;
    }
    raw.stts.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = box.data + 8 + static_cast<size_t>(i) * 8;
        raw.stts[i].count = rd32(entry);
        raw.stts[i].delta = rd32(entry + 4);
    }
}

void parse_stss(const View &box, RawTrack &raw)
{
    if (box.size < 8) {
        return;
    }
    const uint32_t count = rd32(box.data + 4);
    if (box.size < 8 + static_cast<size_t>(count) * 4 || count > 300000) {
        return;
    }
    raw.has_stss = true;
    raw.sync_samples.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        raw.sync_samples[i] = rd32(box.data + 8 + static_cast<size_t>(i) * 4);
    }
}

bool parse_stbl(const View &box, RawTrack &raw)
{
    return for_each_box(box, [&raw](uint32_t type, const View &child) {
        if (type == fourcc('s', 't', 's', 'd')) {
            return parse_stsd(child, raw);
        }
        if (type == fourcc('s', 't', 's', 'z')) {
            parse_stsz(child, raw);
        } else if (type == fourcc('s', 't', 'c', 'o')) {
            parse_stco(child, false, raw);
        } else if (type == fourcc('c', 'o', '6', '4')) {
            parse_stco(child, true, raw);
        } else if (type == fourcc('s', 't', 's', 'c')) {
            parse_stsc(child, raw);
        } else if (type == fourcc('s', 't', 't', 's')) {
            parse_stts(child, raw);
        } else if (type == fourcc('s', 't', 's', 's')) {
            parse_stss(child, raw);
        }
        return true;
    });
}

bool parse_minf(const View &box, RawTrack &raw)
{
    return for_each_box(box, [&raw](uint32_t type, const View &child) {
        if (type == fourcc('s', 't', 'b', 'l')) {
            return parse_stbl(child, raw);
        }
        return true;
    });
}

bool parse_mdia(const View &box, RawTrack &raw, bool &is_video)
{
    return for_each_box(box, [&raw, &is_video](uint32_t type, const View &child) {
        if (type == fourcc('m', 'd', 'h', 'd')) {
            parse_mdhd(child, raw.timescale);
        } else if (type == fourcc('h', 'd', 'l', 'r')) {
            if (child.size >= 12) {
                is_video = (rd32(child.data + 8) == fourcc('v', 'i', 'd', 'e'));
            }
        } else if (type == fourcc('m', 'i', 'n', 'f')) {
            return parse_minf(child, raw);
        }
        return true;
    });
}

bool build_extradata(RawTrack &raw, std::vector<uint8_t> &out, uint16_t &width, uint16_t &height,
                     uint8_t &nal_length_size)
{
    const auto &avcc = raw.avcc;
    if (avcc.size() < 7 || avcc[0] != 1) {
        return false;
    }
    nal_length_size = static_cast<uint8_t>((avcc[4] & 0x03) + 1);
    if (nal_length_size != 4) {
        // The sample reader only implements 4-byte lengths; reject the rest.
        return false;
    }
    size_t pos = 6;
    const uint8_t num_sps = avcc[5] & 0x1F;
    for (uint8_t i = 0; i < num_sps; i++) {
        if (pos + 2 > avcc.size()) {
            return false;
        }
        const uint16_t length = rd16(avcc.data() + pos);
        pos += 2;
        if (pos + length > avcc.size()) {
            return false;
        }
        static const uint8_t start_code[4] = {0, 0, 0, 1};
        out.insert(out.end(), start_code, start_code + 4);
        out.insert(out.end(), avcc.begin() + static_cast<std::ptrdiff_t>(pos),
                   avcc.begin() + static_cast<std::ptrdiff_t>(pos + length));
        if (i == 0) {
            uint16_t w = 0;
            uint16_t h = 0;
            if (parse_sps_dimensions(avcc.data() + pos, length, w, h)) {
                width = w;
                height = h;
            }
        }
        pos += length;
    }
    if (pos >= avcc.size()) {
        return false;
    }
    const uint8_t num_pps = avcc[pos++];
    for (uint8_t i = 0; i < num_pps; i++) {
        if (pos + 2 > avcc.size()) {
            return false;
        }
        const uint16_t length = rd16(avcc.data() + pos);
        pos += 2;
        if (pos + length > avcc.size()) {
            return false;
        }
        static const uint8_t start_code[4] = {0, 0, 0, 1};
        out.insert(out.end(), start_code, start_code + 4);
        out.insert(out.end(), avcc.begin() + static_cast<std::ptrdiff_t>(pos),
                   avcc.begin() + static_cast<std::ptrdiff_t>(pos + length));
        pos += length;
    }
    return !out.empty();
}

bool finish_track(RawTrack &raw, Track &track)
{
    if (raw.sizes.empty()) {
        track.error = "样本表为空";
        return false;
    }
    if (raw.chunk_offsets.empty() || raw.stsc.empty()) {
        track.error = "样本偏移表缺失";
        return false;
    }
    if (raw.avcc.empty()) {
        track.error = "缺少 avcC 配置";
        return false;
    }

    uint16_t width = raw.width;
    uint16_t height = raw.height;
    std::vector<uint8_t> extradata;
    uint8_t nal_length_size = 4;
    if (!build_extradata(raw, extradata, width, height, nal_length_size)) {
        track.error = "无法解析 H.264 参数集";
        return false;
    }
    if (width == 0 || height == 0) {
        track.error = "无法获取视频分辨率";
        return false;
    }
    track.width = width;
    track.height = height;
    track.nal_length_size = nal_length_size;
    track.extradata = std::move(extradata);

    // Expand the sample-to-chunk table into per-sample file offsets.
    const size_t count = raw.sizes.size();
    track.samples.resize(count);
    size_t sample_index = 0;
    size_t stsc_index = 0;
    for (size_t chunk = 0; chunk < raw.chunk_offsets.size() && sample_index < count; chunk++) {
        while (stsc_index + 1 < raw.stsc.size() && raw.stsc[stsc_index + 1].first_chunk <= chunk + 1) {
            ++stsc_index;
        }
        const uint32_t per_chunk = raw.stsc[stsc_index].samples_per_chunk;
        uint64_t offset = raw.chunk_offsets[chunk];
        for (uint32_t i = 0; i < per_chunk && sample_index < count; i++) {
            track.samples[sample_index].offset = static_cast<uint32_t>(offset);
            track.samples[sample_index].size = raw.sizes[sample_index];
            offset += raw.sizes[sample_index];
            ++sample_index;
        }
    }
    if (sample_index < count) {
        track.samples.resize(sample_index);
    }
    if (track.samples.empty()) {
        track.error = "没有可用的视频样本";
        return false;
    }

    // Per-sample durations from stts (milliseconds).
    const uint32_t timescale = raw.timescale == 0 ? 1000 : raw.timescale;
    size_t index = 0;
    uint32_t last_duration = 0;
    for (const auto &entry : raw.stts) {
        const uint32_t duration_ms = static_cast<uint32_t>(
                                         (static_cast<uint64_t>(entry.delta) * 1000U + timescale / 2U) / timescale
                                     );
        if (duration_ms > 0) {
            last_duration = duration_ms;
        }
        for (uint32_t i = 0; i < entry.count && index < track.samples.size(); i++) {
            track.samples[index++].duration_ms = duration_ms;
        }
    }
    if (last_duration == 0) {
        last_duration = 33;
    }
    for (; index < track.samples.size(); index++) {
        track.samples[index].duration_ms = last_duration;
    }

    // Keyframes: without an stss box every sample is a sync sample.
    if (raw.has_stss) {
        for (uint32_t number : raw.sync_samples) {
            if (number >= 1 && number <= track.samples.size()) {
                track.samples[number - 1].keyframe = true;
            }
        }
    } else {
        for (auto &sample : track.samples) {
            sample.keyframe = true;
        }
    }

    uint64_t total_ms = 0;
    for (const auto &sample : track.samples) {
        total_ms += sample.duration_ms;
    }
    if (total_ms == 0) {
        total_ms = static_cast<uint64_t>(track.samples.size()) * last_duration;
    }
    track.duration_ms = static_cast<uint32_t>(total_ms);
    uint32_t fps = static_cast<uint32_t>(
                       (static_cast<uint64_t>(track.samples.size()) * 1000U + total_ms / 2U) / total_ms
                   );
    track.fps = std::clamp<uint32_t>(fps, 1, 120);
    track.valid = true;
    track.h264 = true;
    return true;
}

bool parse_trak(const View &trak, Track &track, bool &found_video)
{
    RawTrack raw;
    bool is_video = false;
    if (!for_each_box(trak, [&raw, &is_video](uint32_t type, const View &child) {
    if (type == fourcc('m', 'd', 'i', 'a')) {
            return parse_mdia(child, raw, is_video);
        }
        return true;
    })) {
        return false;
    }
    if (!is_video) {
        return true;
    }
    found_video = true;
    if (!raw.has_video) {
        track.error = "没有找到视频轨";
        return false;
    }
    if (raw.unsupported_codec) {
        track.error = "不支持 " + raw.codec + " 编码（仅支持 H.264）";
        return false;
    }
    return finish_track(raw, track);
}

bool load_moov(std::FILE *file, uint64_t file_size, std::vector<uint8_t> &out)
{
    uint64_t pos = 0;
    while (pos + 8 <= file_size) {
        uint8_t header[16] = {};
        if (std::fseek(file, static_cast<long>(pos), SEEK_SET) != 0 ||
                std::fread(header, 1, 8, file) != 8) {
            return false;
        }
        uint64_t box_size = rd32(header);
        const uint32_t type = rd32(header + 4);
        size_t header_size = 8;
        if (box_size == 1) {
            if (std::fread(header + 8, 1, 8, file) != 8) {
                return false;
            }
            box_size = rd64(header + 8);
            header_size = 16;
        } else if (box_size == 0) {
            box_size = file_size - pos;
        }
        if (box_size < header_size || pos + box_size > file_size) {
            return false;
        }
        if (type == fourcc('m', 'o', 'o', 'v')) {
            const uint64_t payload = box_size - header_size;
            if (payload > 32ULL * 1024ULL * 1024ULL) {
                return false;
            }
            out.resize(static_cast<size_t>(payload));
            return std::fread(out.data(), 1, out.size(), file) == out.size();
        }
        pos += box_size;
    }
    return false;
}

} // namespace

bool parse(std::FILE *file, uint64_t file_size, Track &track)
{
    track = Track{};
    if (file == nullptr || file_size < 16) {
        track.error = "文件太小";
        return false;
    }

    std::vector<uint8_t> moov;
    if (!load_moov(file, file_size, moov)) {
        track.error = "未找到 moov 元数据";
        return false;
    }

    bool found_video = false;
    bool ok = false;
    const View moov_view{moov.data(), moov.size()};
    if (!for_each_box(moov_view, [&](uint32_t type, const View &child) {
    if (type != fourcc('t', 'r', 'a', 'k')) {
            return true;
        }
        if (parse_trak(child, track, found_video)) {
            // Keep scanning until the video track is found; once it has been
            // parsed (successfully or with an error) stop.
            return !found_video;
        }
        return false;
    })) {
        ok = false;
    } else {
        ok = track.valid;
    }

    if (!track.valid && track.error.empty()) {
        track.error = found_video ? "视频轨解析失败" : "没有找到视频轨";
    }
    if (!track.valid) {
        ESP_LOGW(TAG, "MP4: %s", track.error.c_str());
    }
    return track.valid;
}

} // namespace esp_brookesia::app::video::mp4
