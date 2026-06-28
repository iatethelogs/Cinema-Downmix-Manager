﻿#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cwchar>

#include "../Common/cdm_realtime_shared.hpp"
#include "../Common/cdm_core_realtime.hpp"

namespace cdm_apo {

// адаптер между настоящим windows apo и общим cdm core
// здесь нет gui и нет тяжелых операций в процессе обработки буфера

class shared_memory_view {
public:
    shared_memory_view() noexcept = default;

    ~shared_memory_view() noexcept {
        close();
    }

    bool open_or_create() noexcept {
        if (state_) {
            return true;
        }

        // первым делом пробуем global, чтобы gui и audiodg.exe могли увидеть один объект
        if (open_name(cdm_shared::kSharedMemoryNameGlobal, true)) {
            return true;
        }

        // если global не сработал, пробуем local для отладки в одном сеансе
        return open_name(cdm_shared::kSharedMemoryNameLocal, true);
    }

    cdm_shared::shared_state* state() noexcept {
        return state_;
    }

    void close() noexcept {
        if (state_) {
            UnmapViewOfFile(state_);
            state_ = nullptr;
        }

        if (mapping_) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
    }

private:
    bool open_name(const wchar_t* name, bool create_if_missing) noexcept {
        HANDLE mapping = nullptr;
        bool created = false;

        if (create_if_missing) {
            mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                         nullptr,
                                         PAGE_READWRITE,
                                         0,
                                         sizeof(cdm_shared::shared_state),
                                         name);
            created = mapping && GetLastError() != ERROR_ALREADY_EXISTS;
        } else {
            mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
        }

        if (!mapping) {
            return false;
        }

        auto* state = static_cast<cdm_shared::shared_state*>(
            MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(cdm_shared::shared_state)));

        if (!state) {
            CloseHandle(mapping);
            return false;
        }

        mapping_ = mapping;
        state_ = state;

        if (created || !cdm_shared::is_valid(*state_)) {
            cdm_shared::init_defaults(*state_);
        }

        return true;
    }

    HANDLE mapping_ = nullptr;
    cdm_shared::shared_state* state_ = nullptr;
};

struct apo_processor_context {
    shared_memory_view shared;
    cdm_core::cdm_realtime_processor processor;

    wchar_t endpoint_id[cdm_shared::kEndpointIdCapacity]{};
    wchar_t endpoint_name[cdm_shared::kEndpointNameCapacity]{};

    bool shared_ready = false;

    void set_endpoint(const wchar_t* id, const wchar_t* name) noexcept {
        cdm_shared::copy_wide(endpoint_id, cdm_shared::kEndpointIdCapacity, id);
        cdm_shared::copy_wide(endpoint_name, cdm_shared::kEndpointNameCapacity, name);
    }
};

inline bool wide_equals(const wchar_t* a, const wchar_t* b) noexcept {
    if (!a || !b) {
        return false;
    }

    return wcscmp(a, b) == 0;
}

// фильтр по endpoint: gui выбирает устройство, apo проверяет совпадение

inline bool endpoint_allowed(cdm_shared::shared_state* state, apo_processor_context& ctx) noexcept {
    if (!state) {
        return true;
    }

    cdm_shared::copy_wide(state->endpoint.active_endpoint_id,
                          cdm_shared::kEndpointIdCapacity,
                          ctx.endpoint_id);

    cdm_shared::copy_wide(state->endpoint.active_endpoint_name,
                          cdm_shared::kEndpointNameCapacity,
                          ctx.endpoint_name);

    state->endpoint.active_valid = ctx.endpoint_id[0] != L'\0' ? 1 : 0;

    const LONG mode = InterlockedCompareExchange(&state->params.endpoint_mode_value, 0, 0);
    if (mode == static_cast<LONG>(cdm_shared::endpoint_mode::all_devices)) {
        state->endpoint.match_active = 1;
        return true;
    }

    if (state->endpoint.selected_valid == 0) {
        state->endpoint.match_active = 0;
        return false;
    }

    const bool match = wide_equals(state->endpoint.selected_endpoint_id, ctx.endpoint_id);
    state->endpoint.match_active = match ? 1 : 0;
    return match;
}

// главный вызов из apo callback для interleaved float32 буфера

inline void cdm_process_apo_interleaved_float32(apo_processor_context& ctx,
                                                float* buffer,
                                                uint32_t frame_count,
                                                uint32_t channel_count,
                                                uint32_t sample_rate) noexcept {
    if (!buffer || frame_count == 0 || channel_count == 0) {
        return;
    }

    if (!ctx.shared_ready) {
        ctx.shared_ready = ctx.shared.open_or_create();
    }

    cdm_shared::shared_state* state = ctx.shared_ready ? ctx.shared.state() : nullptr;

    if (!endpoint_allowed(state, ctx)) {
        return;
    }

    ctx.processor.process_interleaved(buffer, frame_count, channel_count, sample_rate, state);
}

inline void cdm_process_apo_float32_no_endpoint(apo_processor_context& ctx,
                                                float* buffer,
                                                uint32_t frame_count,
                                                uint32_t channel_count,
                                                uint32_t sample_rate) noexcept {
    ctx.set_endpoint(L"", L"apo endpoint");
    cdm_process_apo_interleaved_float32(ctx, buffer, frame_count, channel_count, sample_rate);
}

} // namespace cdm_apo
