#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "boost/format.hpp"
#include "brookesia/service_audio/macro_configs.h"
#if !BROOKESIA_SERVICE_AUDIO_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/dataflow_provider.hpp"
#include "private/utils.hpp"
#include "brookesia/lib_utils/plugin.hpp"
#include "brookesia/hal_interface/interface.hpp"
#include "brookesia/hal_interface/interfaces/audio/processor.hpp"
#include "brookesia/service_audio/service_audio.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/service/manager.hpp"

namespace esp_brookesia::service {

constexpr size_t ENCODER_FETCH_DATA_SIZE_MORE = 100;
constexpr const char *DECODER_OUTPUT_NAME = "Speaker0";
constexpr const char *DECODER_OUTPUT_ROLE = "speaker";
constexpr uint32_t DECODER_STREAM_DRAIN_INTERVAL_MS = 1;
constexpr uint32_t DECODER_STREAM_QUEUE_SIZE_DEFAULT = 32 * 1024;

namespace {

inline int64_t get_current_time_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// The registries below are intentionally leaked (heap-allocated, never deleted) to
// avoid a static destruction order fiasco: AudioDecoder/AudioEncoder instances are
// owned by the ServiceBase PluginRegistry (constructed during static init, destroyed
// late), while these function-local statistics are constructed at runtime and would
// otherwise be destroyed first. Their destructors would then access already-freed
// registries at program exit (heap-use-after-free).
std::mutex &get_decoder_registry_mutex()
{
    static std::mutex *mutex = new std::mutex();
    return *mutex;
}

std::vector<AudioDecoder *> &get_decoder_registry()
{
    static std::vector<AudioDecoder *> *registry = new std::vector<AudioDecoder *>();
    return *registry;
}

std::mutex &get_encoder_registry_mutex()
{
    static std::mutex *mutex = new std::mutex();
    return *mutex;
}

std::vector<AudioEncoder *> &get_encoder_registry()
{
    static std::vector<AudioEncoder *> *registry = new std::vector<AudioEncoder *>();
    return *registry;
}

std::string make_dataflow_provider_id(std::string_view model, int id)
{
    return "audio." + std::string(model) + "." + std::to_string(id);
}

} // namespace


} // namespace esp_brookesia::service

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
