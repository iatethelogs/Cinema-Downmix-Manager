#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <cwchar>

namespace cdm_shared {

// структура общей памяти между gui, live loopback и реальным apo
// все версии должны совпадать, иначе gui и apo будут смотреть в разные объекты

static constexpr uint32_t kMagic = 0x43444D31u;
static constexpr uint32_t kVersion = 19u;

static constexpr wchar_t kSharedMemoryNameGlobal[] = L"Global\CDM_APO_SHARED_STATE_V19";
static constexpr wchar_t kSharedMemoryNameLocal[] = L"Local\CDM_APO_SHARED_STATE_V19";
static constexpr uint32_t kInputChannelCount = 8u;
static constexpr uint32_t kBusCount = 4u;
static constexpr uint32_t kOutputChannelCount = 3u;
static constexpr uint32_t kEndpointIdCapacity = 512u;
static constexpr uint32_t kEndpointNameCapacity = 256u;

// режим выбора устройства: обрабатывать все endpoint или только выбранный

enum class endpoint_mode : uint32_t {
    all_devices = 0,
    selected_device = 1
};

enum class output_mode : uint32_t {
    stereo_2_0 = 0,
    stereo_2_1 = 1
};

// параметры обработки, которые gui меняет слайдерами
// apo читает их в audio callback без блокировок

struct runtime_params {
    volatile LONG bypass;
    volatile LONG simulation_enabled;
    volatile LONG output_mode_value;
    volatile LONG endpoint_mode_value;

    float dialogue_boost_db;
    float effects_duck_db;
    float bass_duck_db;
    float bass_gain_db;
    float stereo_width;
    float output_gain_db;
};

// снимок уровней для интерфейса
// это не аудиобуфер, а только легкая телеметрия

struct meter_snapshot {
    float input_peak[kInputChannelCount];
    float bus_peak[kBusCount];
    float output_peak[kOutputChannelCount];

    float dialogue_confidence;
    float voice_gain;
    float effects_gain;
    float bass_gain;
    float limiter_reduction_db;

    uint32_t frame_counter;
};

// выбранное и активное устройство
// selected пишет gui, active пишет apo/live capture

struct endpoint_selection {
    wchar_t selected_endpoint_id[kEndpointIdCapacity];
    wchar_t selected_endpoint_name[kEndpointNameCapacity];

    wchar_t active_endpoint_id[kEndpointIdCapacity];
    wchar_t active_endpoint_name[kEndpointNameCapacity];

    volatile LONG selected_valid;
    volatile LONG active_valid;
    volatile LONG match_active;
};

struct shared_state {
    uint32_t magic;
    uint32_t version;

    volatile LONG owner_alive;
    volatile LONG ui_sequence;
    volatile LONG endpoint_sequence;
    volatile LONG meter_sequence;

    runtime_params params;
    endpoint_selection endpoint;
    meter_snapshot meter;
};

inline void clear_wide(wchar_t* buffer, uint32_t capacity) noexcept {
    if (!buffer || capacity == 0) {
        return;
    }
    buffer[0] = L'\0';
}

inline void copy_wide(wchar_t* dst, uint32_t capacity, const wchar_t* src) noexcept {
    if (!dst || capacity == 0) {
        return;
    }

    if (!src) {
        dst[0] = L'\0';
        return;
    }

    wcsncpy_s(dst, capacity, src, _TRUNCATE);
}

inline void init_defaults(shared_state& state) noexcept {
    state.magic = kMagic;
    state.version = kVersion;

    state.owner_alive = 1;
    state.ui_sequence = 0;
    state.endpoint_sequence = 0;
    state.meter_sequence = 0;

    state.params.bypass = 0;
    state.params.simulation_enabled = 1;
    state.params.output_mode_value = static_cast<LONG>(output_mode::stereo_2_1);
    state.params.endpoint_mode_value = static_cast<LONG>(endpoint_mode::all_devices);

    state.params.dialogue_boost_db = 7.0f;
    state.params.effects_duck_db = -5.0f;
    state.params.bass_duck_db = -7.0f;
    state.params.bass_gain_db = 0.0f;
    state.params.stereo_width = 1.05f;
    state.params.output_gain_db = -1.5f;

    clear_wide(state.endpoint.selected_endpoint_id, kEndpointIdCapacity);
    clear_wide(state.endpoint.selected_endpoint_name, kEndpointNameCapacity);
    clear_wide(state.endpoint.active_endpoint_id, kEndpointIdCapacity);
    clear_wide(state.endpoint.active_endpoint_name, kEndpointNameCapacity);

    state.endpoint.selected_valid = 0;
    state.endpoint.active_valid = 0;
    state.endpoint.match_active = 1;

    for (uint32_t i = 0; i < kInputChannelCount; ++i) {
        state.meter.input_peak[i] = 0.0f;
    }
    for (uint32_t i = 0; i < kBusCount; ++i) {
        state.meter.bus_peak[i] = 0.0f;
    }
    for (uint32_t i = 0; i < kOutputChannelCount; ++i) {
        state.meter.output_peak[i] = 0.0f;
    }

    state.meter.dialogue_confidence = 0.0f;
    state.meter.voice_gain = 1.0f;
    state.meter.effects_gain = 1.0f;
    state.meter.bass_gain = 1.0f;
    state.meter.limiter_reduction_db = 0.0f;
    state.meter.frame_counter = 0;
}

inline bool is_valid(const shared_state& state) noexcept {
    return state.magic == kMagic && state.version == kVersion;
}

inline float db_to_gain(float db) noexcept {
    return powf(10.0f, db / 20.0f);
}

} // namespace cdm_shared
