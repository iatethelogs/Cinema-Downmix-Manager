﻿#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>

#include "cdm_apo_process.hpp"

namespace cdm_sysvad_bridge {

// обертка для sysvad swapapo, чтобы в чужой callback вставлять один короткий вызов

// это тупая точка входа для sysvad swapapo
// сюда должен прилетать interleaved float32 buffer из apo process callback

inline cdm_apo::apo_processor_context& context() noexcept {
    static cdm_apo::apo_processor_context ctx;
    return ctx;
}

inline void set_endpoint(const wchar_t* endpoint_id, const wchar_t* endpoint_name) noexcept {
    context().set_endpoint(endpoint_id, endpoint_name);
}

inline void process_float32(float* buffer,
                            uint32_t frame_count,
                            uint32_t channel_count,
                            uint32_t sample_rate) noexcept {
    cdm_apo::cdm_process_apo_interleaved_float32(context(),
                                                buffer,
                                                frame_count,
                                                channel_count,
                                                sample_rate);
}

inline void process_float32_no_endpoint(float* buffer,
                                         uint32_t frame_count,
                                         uint32_t channel_count,
                                         uint32_t sample_rate) noexcept {
    cdm_apo::cdm_process_apo_float32_no_endpoint(context(),
                                                 buffer,
                                                 frame_count,
                                                 channel_count,
                                                 sample_rate);
}

} // namespace cdm_sysvad_bridge
