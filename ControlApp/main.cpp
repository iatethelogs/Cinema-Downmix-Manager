#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

#include "../Common/cdm_realtime_shared.hpp"
#include "../Common/cdm_core_realtime.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "avrt.lib")

namespace {

constexpr int kTimerId = 1;
constexpr int kTimerMs = 16;

constexpr int kEndpointComboId = 1001;
constexpr int kAllDevicesButtonId = 1002;
constexpr int kSelectedDeviceButtonId = 1003;
constexpr int kLiveCaptureButtonId = 1004;

constexpr int kSliderDialogueId = 2001;
constexpr int kSliderEffectsId = 2002;
constexpr int kSliderBassDuckId = 2003;
constexpr int kSliderBassGainId = 2004;
constexpr int kSliderWidthId = 2005;
constexpr int kSliderOutputId = 2006;

struct endpoint_item {
    std::wstring id;
    std::wstring name;
};

struct gui_meter_cache {
    float input_peak[cdm_shared::kInputChannelCount]{};
    float bus_peak[cdm_shared::kBusCount]{};
    float output_peak[cdm_shared::kOutputChannelCount]{};
    float confidence = 0.0f;
    float voice_gain = 1.0f;
    float effects_gain = 1.0f;
    float bass_gain = 1.0f;
    float limiter_db = 0.0f;
    LONG last_meter_sequence = -1;
};

struct app_state {
    HWND hwnd = nullptr;
    HWND endpoint_combo = nullptr;
    HWND all_devices_radio = nullptr;
    HWND selected_device_radio = nullptr;
    HWND live_capture_check = nullptr;
    HWND sliders[6]{};

    std::vector<endpoint_item> endpoints;

    HANDLE mapping = nullptr;
    cdm_shared::shared_state* shared = nullptr;
    cdm_shared::shared_state fallback_shared{};
    bool using_global_mapping = false;
    bool using_local_mapping = false;
    bool using_fallback_memory = false;
    DWORD last_mapping_error = 0;

    HANDLE capture_thread = nullptr;
    HANDLE capture_stop = nullptr;
    std::atomic<bool> capture_running{ false };
    std::atomic<bool> capture_enabled{ true };
    std::atomic<uint32_t> capture_frames{ 0 };
    std::atomic<uint32_t> capture_channels{ 0 };
    std::atomic<uint32_t> capture_rate{ 0 };
    std::atomic<long> capture_error{ 0 };

    uint32_t simulation_frame = 0;
    gui_meter_cache meters;
};

app_state g_app;
cdm_core::cdm_realtime_processor g_live_processor;

COLORREF rgb(uint8_t r, uint8_t g, uint8_t b) {
    return RGB(r, g, b);
}

void fill_rect(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

HFONT make_font(int height, int weight = FW_NORMAL) {
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void draw_text(HDC dc, int x, int y, const wchar_t* text, COLORREF color, int height = 16, int weight = FW_NORMAL) {
    HFONT font = make_font(height, weight);
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text, static_cast<int>(wcslen(text)));
    SelectObject(dc, old);
    DeleteObject(font);
}

void draw_text_s(HDC dc, int x, int y, const std::wstring& text, COLORREF color, int height = 16, int weight = FW_NORMAL) {
    draw_text(dc, x, y, text.c_str(), color, height, weight);
}

float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

void draw_outline_rect(HDC dc, const RECT& rc, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_panel(HDC dc, const RECT& rc, const wchar_t* title) {
    fill_rect(dc, rc, rgb(13, 20, 33));
    draw_outline_rect(dc, rc, rgb(38, 52, 70));
    draw_text(dc, rc.left + 14, rc.top + 10, title, rgb(226, 232, 240), 17, FW_BOLD);
}

void draw_meter_bar(HDC dc, int x, int y, int w, int h, float value, const wchar_t* label, COLORREF fill) {
    value = clamp01(value);

    RECT bg{ x, y, x + w, y + h };
    fill_rect(dc, bg, rgb(17, 24, 39));

    RECT fg{ x, y, x + static_cast<int>(w * value), y + h };
    fill_rect(dc, fg, fill);

    draw_outline_rect(dc, bg, rgb(51, 65, 85));

    wchar_t value_text[64]{};
    swprintf_s(value_text, L"%s  %3.0f%%", label, value * 100.0f);
    draw_text(dc, x + 8, y + 2, value_text, rgb(248, 250, 252), 14, FW_SEMIBOLD);
}

bool map_shared_name(const wchar_t* name, bool mark_global, bool mark_local) {
    g_app.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                       nullptr,
                                       PAGE_READWRITE,
                                       0,
                                       sizeof(cdm_shared::shared_state),
                                       name);
    if (!g_app.mapping) {
        g_app.last_mapping_error = GetLastError();
        return false;
    }

    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;

    g_app.shared = static_cast<cdm_shared::shared_state*>(
        MapViewOfFile(g_app.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(cdm_shared::shared_state)));

    if (!g_app.shared) {
        g_app.last_mapping_error = GetLastError();
        CloseHandle(g_app.mapping);
        g_app.mapping = nullptr;
        return false;
    }

    if (created || !cdm_shared::is_valid(*g_app.shared)) {
        cdm_shared::init_defaults(*g_app.shared);
    }

    g_app.using_global_mapping = mark_global;
    g_app.using_local_mapping = mark_local;
    g_app.using_fallback_memory = false;
    return true;
}

bool ensure_shared_memory() {
    if (g_app.shared) {
        return true;
    }

    if (map_shared_name(cdm_shared::kSharedMemoryNameGlobal, true, false)) {
        return true;
    }

    if (map_shared_name(cdm_shared::kSharedMemoryNameLocal, false, true)) {
        return true;
    }

    cdm_shared::init_defaults(g_app.fallback_shared);
    g_app.shared = &g_app.fallback_shared;
    g_app.using_global_mapping = false;
    g_app.using_local_mapping = false;
    g_app.using_fallback_memory = true;
    return true;
}

void close_shared_memory() {
    if (g_app.using_fallback_memory) {
        g_app.shared = nullptr;
        g_app.using_fallback_memory = false;
        return;
    }

    if (g_app.shared) {
        UnmapViewOfFile(g_app.shared);
        g_app.shared = nullptr;
    }

    if (g_app.mapping) {
        CloseHandle(g_app.mapping);
        g_app.mapping = nullptr;
    }

    g_app.using_global_mapping = false;
    g_app.using_local_mapping = false;
}

float slider_to_float(HWND slider, float min_value, float max_value) {
    const int pos = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
    const float t = static_cast<float>(pos) / 1000.0f;
    return min_value + (max_value - min_value) * t;
}

void set_slider(HWND slider, float value, float min_value, float max_value) {
    const float t = (value - min_value) / (max_value - min_value);
    SendMessageW(slider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::clamp(t, 0.0f, 1.0f) * 1000.0f));
}

void sync_params_from_controls() {
    if (!g_app.shared) {
        return;
    }

    g_app.shared->params.dialogue_boost_db = slider_to_float(g_app.sliders[0], 0.0f, 14.0f);
    g_app.shared->params.effects_duck_db = slider_to_float(g_app.sliders[1], -14.0f, 0.0f);
    g_app.shared->params.bass_duck_db = slider_to_float(g_app.sliders[2], -18.0f, 0.0f);
    g_app.shared->params.bass_gain_db = slider_to_float(g_app.sliders[3], -10.0f, 8.0f);
    g_app.shared->params.stereo_width = slider_to_float(g_app.sliders[4], 0.5f, 1.8f);
    g_app.shared->params.output_gain_db = slider_to_float(g_app.sliders[5], -12.0f, 3.0f);

    InterlockedIncrement(&g_app.shared->ui_sequence);
}

void select_endpoint_to_shared(int index) {
    if (!g_app.shared) {
        return;
    }

    if (index < 0 || index >= static_cast<int>(g_app.endpoints.size())) {
        g_app.shared->endpoint.selected_valid = 0;
        cdm_shared::clear_wide(g_app.shared->endpoint.selected_endpoint_id, cdm_shared::kEndpointIdCapacity);
        cdm_shared::clear_wide(g_app.shared->endpoint.selected_endpoint_name, cdm_shared::kEndpointNameCapacity);
        return;
    }

    const endpoint_item& item = g_app.endpoints[static_cast<size_t>(index)];

    cdm_shared::copy_wide(g_app.shared->endpoint.selected_endpoint_id,
                          cdm_shared::kEndpointIdCapacity,
                          item.id.c_str());

    cdm_shared::copy_wide(g_app.shared->endpoint.selected_endpoint_name,
                          cdm_shared::kEndpointNameCapacity,
                          item.name.c_str());

    g_app.shared->endpoint.selected_valid = 1;

    InterlockedExchange(&g_app.shared->params.endpoint_mode_value,
                        static_cast<LONG>(cdm_shared::endpoint_mode::selected_device));
    InterlockedIncrement(&g_app.shared->endpoint_sequence);
}

std::wstring property_string(IPropertyStore* store, REFPROPERTYKEY key) {
    PROPVARIANT var;
    PropVariantInit(&var);
    std::wstring result;

    if (SUCCEEDED(store->GetValue(key, &var)) && var.vt == VT_LPWSTR && var.pwszVal) {
        result = var.pwszVal;
    }

    PropVariantClear(&var);
    return result;
}

void enumerate_render_endpoints() {
    g_app.endpoints.clear();

    if (g_app.endpoint_combo) {
        SendMessageW(g_app.endpoint_combo, CB_RESETCONTENT, 0, 0);
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;

    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                        nullptr,
                                        CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator),
                                        reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        return;
    }

    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) && collection) {
        UINT count = 0;
        collection->GetCount(&count);

        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(i, &device)) || !device) {
                continue;
            }

            LPWSTR raw_id = nullptr;
            std::wstring endpoint_id;
            if (SUCCEEDED(device->GetId(&raw_id)) && raw_id) {
                endpoint_id = raw_id;
                CoTaskMemFree(raw_id);
            }

            IPropertyStore* store = nullptr;
            std::wstring name = L"неизвестное устройство";
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
                const std::wstring friendly = property_string(store, PKEY_Device_FriendlyName);
                if (!friendly.empty()) {
                    name = friendly;
                }
                store->Release();
            }

            endpoint_item item{ endpoint_id, name };
            g_app.endpoints.push_back(item);

            if (g_app.endpoint_combo) {
                const LRESULT combo_index = SendMessageW(g_app.endpoint_combo,
                                                         CB_ADDSTRING,
                                                         0,
                                                         reinterpret_cast<LPARAM>(name.c_str()));
                SendMessageW(g_app.endpoint_combo,
                             CB_SETITEMDATA,
                             combo_index,
                             static_cast<LPARAM>(g_app.endpoints.size() - 1));
            }

            device->Release();
        }
    }

    if (collection) {
        collection->Release();
    }

    enumerator->Release();

    if (g_app.endpoint_combo && !g_app.endpoints.empty()) {
        SendMessageW(g_app.endpoint_combo, CB_SETCURSEL, 0, 0);
        select_endpoint_to_shared(0);
    }
}

int selected_endpoint_index() {
    if (!g_app.endpoint_combo) {
        return -1;
    }

    const int sel = static_cast<int>(SendMessageW(g_app.endpoint_combo, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(g_app.endpoints.size())) {
        return -1;
    }

    return sel;
}

std::wstring selected_endpoint_id_copy() {
    const int idx = selected_endpoint_index();
    if (idx < 0) {
        return {};
    }

    return g_app.endpoints[static_cast<size_t>(idx)].id;
}

template <class T>
void safe_release(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

void process_float_interleaved(float* data, uint32_t frames, uint32_t channels, uint32_t rate) {
    if (!data || frames == 0 || channels == 0 || !g_app.shared) {
        return;
    }

    g_live_processor.process_interleaved(data, frames, channels, rate, g_app.shared);
}

void process_int16_interleaved(const BYTE* raw, uint32_t frames, uint32_t channels, uint32_t rate) {
    if (!raw || frames == 0 || channels == 0 || channels > 8) {
        return;
    }

    std::vector<float> temp(static_cast<size_t>(frames) * channels);
    const int16_t* src = reinterpret_cast<const int16_t*>(raw);
    for (size_t i = 0; i < temp.size(); ++i) {
        temp[i] = static_cast<float>(src[i]) / 32768.0f;
    }

    process_float_interleaved(temp.data(), frames, channels, rate);
}

void process_int32_interleaved(const BYTE* raw, uint32_t frames, uint32_t channels, uint32_t rate) {
    if (!raw || frames == 0 || channels == 0 || channels > 8) {
        return;
    }

    std::vector<float> temp(static_cast<size_t>(frames) * channels);
    const int32_t* src = reinterpret_cast<const int32_t*>(raw);
    for (size_t i = 0; i < temp.size(); ++i) {
        temp[i] = static_cast<float>(src[i]) / 2147483648.0f;
    }

    process_float_interleaved(temp.data(), frames, channels, rate);
}

DWORD WINAPI capture_thread_proc(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    DWORD task_index = 0;
    HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);

    g_app.capture_running.store(true);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audio_client = nullptr;
    IAudioCaptureClient* capture_client = nullptr;
    WAVEFORMATEX* mix_format = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        g_app.capture_error.store(static_cast<long>(hr));
        goto cleanup;
    }

    {
        const std::wstring endpoint_id = selected_endpoint_id_copy();
        if (!endpoint_id.empty()) {
            hr = enumerator->GetDevice(endpoint_id.c_str(), &device);
        } else {
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        }
    }

    if (FAILED(hr) || !device) {
        g_app.capture_error.store(static_cast<long>(hr));
        goto cleanup;
    }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audio_client));
    if (FAILED(hr) || !audio_client) {
        g_app.capture_error.store(static_cast<long>(hr));
        goto cleanup;
    }

    hr = audio_client->GetMixFormat(&mix_format);
    if (FAILED(hr) || !mix_format) {
        g_app.capture_error.store(static_cast<long>(hr));
        goto cleanup;
    }

    {
        const REFERENCE_TIME buffer_duration = 1000000; // 100 мс, это буфер захвата, не dsp lookahead
        hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                      AUDCLNT_STREAMFLAGS_LOOPBACK,
                                      buffer_duration,
                                      0,
                                      mix_format,
                                      nullptr);
    }

    if (FAILED(hr)) {
        g_app.capture_error.store(static_cast<long>(hr));
        goto cleanup;
    }

    hr = audio_client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture_client));
    if (FAILED(hr) || !capture_client) {
        g_app.capture_error.store(static_cast<long>(hr));
        goto cleanup;
    }

    hr = audio_client->Start();
    if (FAILED(hr)) {
        g_app.capture_error.store(static_cast<long>(hr));
        goto cleanup;
    }

    g_app.capture_channels.store(mix_format->nChannels);
    g_app.capture_rate.store(mix_format->nSamplesPerSec);
    g_app.capture_error.store(0);

    while (WaitForSingleObject(g_app.capture_stop, 2) == WAIT_TIMEOUT) {
        UINT32 packet_frames = 0;
        hr = capture_client->GetNextPacketSize(&packet_frames);
        if (FAILED(hr)) {
            g_app.capture_error.store(static_cast<long>(hr));
            break;
        }

        while (packet_frames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;

            hr = capture_client->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                g_app.capture_error.store(static_cast<long>(hr));
                break;
            }

            const uint32_t channels = mix_format->nChannels;
            const uint32_t rate = mix_format->nSamplesPerSec;

            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data && frames > 0) {
                if (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                    (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                     reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                    std::vector<float> copy(static_cast<size_t>(frames) * channels);
                    const float* src = reinterpret_cast<const float*>(data);
                    std::copy(src, src + copy.size(), copy.data());
                    process_float_interleaved(copy.data(), frames, channels, rate);
                } else if (mix_format->wBitsPerSample == 16) {
                    process_int16_interleaved(data, frames, channels, rate);
                } else if (mix_format->wBitsPerSample == 32) {
                    process_int32_interleaved(data, frames, channels, rate);
                }
            }

            g_app.capture_frames.fetch_add(frames);

            capture_client->ReleaseBuffer(frames);

            hr = capture_client->GetNextPacketSize(&packet_frames);
            if (FAILED(hr)) {
                g_app.capture_error.store(static_cast<long>(hr));
                break;
            }
        }
    }

cleanup:
    if (audio_client) {
        audio_client->Stop();
    }

    if (mix_format) {
        CoTaskMemFree(mix_format);
        mix_format = nullptr;
    }

    safe_release(capture_client);
    safe_release(audio_client);
    safe_release(device);
    safe_release(enumerator);

    if (avrt) {
        AvRevertMmThreadCharacteristics(avrt);
    }

    g_app.capture_running.store(false);
    CoUninitialize();
    return 0;
}

void stop_capture() {
    if (g_app.capture_thread) {
        SetEvent(g_app.capture_stop);
        WaitForSingleObject(g_app.capture_thread, 2000);
        CloseHandle(g_app.capture_thread);
        g_app.capture_thread = nullptr;
    }

    if (g_app.capture_stop) {
        CloseHandle(g_app.capture_stop);
        g_app.capture_stop = nullptr;
    }

    g_app.capture_running.store(false);
}

void start_capture() {
    stop_capture();

    if (!g_app.capture_enabled.load()) {
        return;
    }

    g_app.capture_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_app.capture_stop) {
        return;
    }

    DWORD tid = 0;
    g_app.capture_thread = CreateThread(nullptr, 0, capture_thread_proc, nullptr, 0, &tid);
    if (!g_app.capture_thread) {
        CloseHandle(g_app.capture_stop);
        g_app.capture_stop = nullptr;
        g_app.capture_running.store(false);
    }
}

void restart_capture() {
    if (g_app.capture_enabled.load()) {
        start_capture();
    }
}

void copy_meter_snapshot() {
    if (!g_app.shared) {
        return;
    }

    const LONG seq = InterlockedCompareExchange(&g_app.shared->meter_sequence, 0, 0);
    if (seq == g_app.meters.last_meter_sequence) {
        return;
    }

    g_app.meters.last_meter_sequence = seq;

    for (uint32_t i = 0; i < cdm_shared::kInputChannelCount; ++i) {
        g_app.meters.input_peak[i] = g_app.shared->meter.input_peak[i];
    }

    for (uint32_t i = 0; i < cdm_shared::kBusCount; ++i) {
        g_app.meters.bus_peak[i] = g_app.shared->meter.bus_peak[i];
    }

    for (uint32_t i = 0; i < cdm_shared::kOutputChannelCount; ++i) {
        g_app.meters.output_peak[i] = g_app.shared->meter.output_peak[i];
    }

    g_app.meters.confidence = g_app.shared->meter.dialogue_confidence;
    g_app.meters.voice_gain = g_app.shared->meter.voice_gain;
    g_app.meters.effects_gain = g_app.shared->meter.effects_gain;
    g_app.meters.bass_gain = g_app.shared->meter.bass_gain;
    g_app.meters.limiter_db = g_app.shared->meter.limiter_reduction_db;
}

void update_simulation() {
    if (!g_app.shared) {
        return;
    }

    if (g_app.capture_running.load()) {
        copy_meter_snapshot();
        return;
    }

    if (InterlockedCompareExchange(&g_app.shared->params.simulation_enabled, 0, 0) == 0) {
        copy_meter_snapshot();
        return;
    }

    const float t = static_cast<float>(g_app.simulation_frame) * 0.07f;
    g_app.simulation_frame++;

    for (uint32_t i = 0; i < cdm_shared::kInputChannelCount; ++i) {
        g_app.shared->meter.input_peak[i] = clamp01(0.08f + 0.75f * std::fabs(std::sin(t * (0.55f + i * 0.08f))));
    }

    const float speech = 0.5f + 0.5f * std::sin(t * 0.75f);
    const float lfe = 0.25f + 0.70f * std::fabs(std::sin(t * 0.30f));

    g_app.shared->meter.bus_peak[0] = clamp01(0.15f + speech * 0.75f);
    g_app.shared->meter.bus_peak[1] = clamp01(0.18f + (1.0f - speech) * 0.55f);
    g_app.shared->meter.bus_peak[2] = clamp01(0.18f + (1.0f - speech) * 0.58f);
    g_app.shared->meter.bus_peak[3] = clamp01(lfe);

    g_app.shared->meter.dialogue_confidence = clamp01(speech);
    g_app.shared->meter.voice_gain = 1.0f + speech * 1.2f;
    g_app.shared->meter.effects_gain = 1.0f - speech * 0.45f;
    g_app.shared->meter.bass_gain = 1.0f - speech * 0.55f;
    g_app.shared->meter.limiter_reduction_db = -std::max(0.0f, lfe - 0.75f) * 8.0f;

    g_app.shared->meter.output_peak[0] = clamp01(0.22f + speech * 0.48f);
    g_app.shared->meter.output_peak[1] = clamp01(0.22f + speech * 0.45f);
    g_app.shared->meter.output_peak[2] = clamp01(lfe * (1.0f - speech * 0.45f));

    g_app.shared->meter.frame_counter++;
    InterlockedIncrement(&g_app.shared->meter_sequence);

    copy_meter_snapshot();
}

void draw_status(HDC dc, int x, int y) {
    if (!g_app.shared) {
        draw_text(dc, x, y, L"общая память: нет", rgb(248, 113, 113), 16, FW_BOLD);
        return;
    }

    std::wstring map_name;
    if (g_app.using_global_mapping) {
        map_name = L"global";
    } else if (g_app.using_local_mapping) {
        map_name = L"local";
    } else if (g_app.using_fallback_memory) {
        map_name = L"fallback";
    } else {
        map_name = L"unknown";
    }

    const bool sim = InterlockedCompareExchange(&g_app.shared->params.simulation_enabled, 0, 0) != 0;
    const bool bypass = InterlockedCompareExchange(&g_app.shared->params.bypass, 0, 0) != 0;
    const bool selected = InterlockedCompareExchange(&g_app.shared->params.endpoint_mode_value, 0, 0) ==
        static_cast<LONG>(cdm_shared::endpoint_mode::selected_device);

    std::wstring text = L"общая память: ";
    text += map_name;
    text += L"  |  ";
    text += g_app.capture_running.load() ? L"live loopback работает" : L"live loopback остановлен";
    text += sim ? L"  |  симуляция on" : L"  |  симуляция off";
    text += bypass ? L"  |  bypass on" : L"  |  bypass off";
    text += selected ? L"  |  выбранное устройство" : L"  |  все устройства";

    COLORREF status_color = g_app.using_fallback_memory ? rgb(250, 204, 21) : rgb(203, 213, 225);
    draw_text_s(dc, x, y, text, status_color, 15, FW_NORMAL);

    wchar_t line[256]{};
    swprintf_s(line,
               L"захват: %u ch / %u hz / frames %u / err 0x%08x",
               g_app.capture_channels.load(),
               g_app.capture_rate.load(),
               g_app.capture_frames.load(),
               static_cast<unsigned>(g_app.capture_error.load()));
    draw_text(dc, x, y + 22, line, rgb(148, 163, 184), 14, FW_NORMAL);
}

void draw_controls_labels(HDC dc) {
    const int sx = 24;
    const int sy = 570;
    const wchar_t* labels[] = {
        L"усиление речи",
        L"приглушение эффектов",
        L"приглушение баса",
        L"бас",
        L"ширина стерео",
        L"выход"
    };

    for (int i = 0; i < 6; ++i) {
        draw_text(dc, sx + i * 300, sy - 20, labels[i], rgb(203, 213, 225), 14, FW_SEMIBOLD);
    }
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT client;
    GetClientRect(hwnd, &client);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);

    fill_rect(mem, client, rgb(7, 11, 20));

    draw_text(mem, 22, 18, L"cdm control app v19", rgb(248, 250, 252), 28, FW_BOLD);
    draw_status(mem, 24, 56);

    RECT in_panel{ 22, 110, 430, 500 };
    RECT bus_panel{ 452, 110, 860, 500 };
    RECT out_panel{ 882, 110, 1290, 500 };
    RECT ctrl_panel{ 1312, 110, client.right - 22, 500 };

    draw_panel(mem, in_panel, L"входные каналы");
    draw_panel(mem, bus_panel, L"внутренние шины");
    draw_panel(mem, out_panel, L"выход");
    draw_panel(mem, ctrl_panel, L"устройство и обработка");

    const wchar_t* input_labels[] = { L"FL", L"FR", L"FC", L"LFE", L"SL", L"SR", L"BL", L"BR" };
    const wchar_t* bus_labels[] = { L"VOICE", L"EFF L", L"EFF R", L"BASS" };
    const wchar_t* out_labels[] = { L"L", L"R", L"SUB" };

    for (int i = 0; i < 8; ++i) {
        draw_meter_bar(mem, in_panel.left + 18, in_panel.top + 52 + i * 38, 360, 24,
                       g_app.meters.input_peak[i],
                       input_labels[i],
                       rgb(56, 189, 248));
    }

    for (int i = 0; i < 4; ++i) {
        draw_meter_bar(mem, bus_panel.left + 18, bus_panel.top + 52 + i * 48, 360, 28,
                       g_app.meters.bus_peak[i],
                       bus_labels[i],
                       i == 0 ? rgb(167, 139, 250) : rgb(251, 146, 60));
    }

    for (int i = 0; i < 3; ++i) {
        draw_meter_bar(mem, out_panel.left + 18, out_panel.top + 52 + i * 56, 360, 34,
                       g_app.meters.output_peak[i],
                       out_labels[i],
                       rgb(192, 132, 252));
    }

    wchar_t line[128]{};
    swprintf_s(line, L"confidence: %.2f", g_app.meters.confidence);
    draw_text(mem, ctrl_panel.left + 18, ctrl_panel.top + 105, line, rgb(226, 232, 240), 15, FW_SEMIBOLD);

    swprintf_s(line, L"voice gain: %.2f", g_app.meters.voice_gain);
    draw_text(mem, ctrl_panel.left + 18, ctrl_panel.top + 130, line, rgb(226, 232, 240), 15, FW_SEMIBOLD);

    swprintf_s(line, L"effects gain: %.2f", g_app.meters.effects_gain);
    draw_text(mem, ctrl_panel.left + 18, ctrl_panel.top + 155, line, rgb(226, 232, 240), 15, FW_SEMIBOLD);

    swprintf_s(line, L"bass gain: %.2f", g_app.meters.bass_gain);
    draw_text(mem, ctrl_panel.left + 18, ctrl_panel.top + 180, line, rgb(226, 232, 240), 15, FW_SEMIBOLD);

    swprintf_s(line, L"limiter: %.1f db", g_app.meters.limiter_db);
    draw_text(mem, ctrl_panel.left + 18, ctrl_panel.top + 205, line, rgb(226, 232, 240), 15, FW_SEMIBOLD);

    draw_controls_labels(mem);
    draw_text(mem, 24, 522, L"s — симуляция, b — bypass, m — 2.0/2.1, r — обновить устройства, l — live loopback", rgb(148, 163, 184), 15, FW_NORMAL);

    BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);

    EndPaint(hwnd, &ps);
}

HWND create_slider(HWND parent, int id, int x, int y, int w) {
    HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                                  WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                  x, y, w, 32,
                                  parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr),
                                  nullptr);
    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
    SendMessageW(slider, TBM_SETTICFREQ, 100, 0);
    return slider;
}

void set_child_font(HWND hwnd) {
    HFONT font = make_font(16, FW_NORMAL);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void create_controls(HWND hwnd) {
    g_app.endpoint_combo = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
                                           WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                           1330, 158, 470, 300,
                                           hwnd,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEndpointComboId)),
                                           GetModuleHandleW(nullptr),
                                           nullptr);
    set_child_font(g_app.endpoint_combo);

    g_app.all_devices_radio = CreateWindowExW(0, WC_BUTTONW, L"обрабатывать все устройства",
                                              WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                              1330, 198, 270, 24,
                                              hwnd,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAllDevicesButtonId)),
                                              GetModuleHandleW(nullptr),
                                              nullptr);
    set_child_font(g_app.all_devices_radio);

    g_app.selected_device_radio = CreateWindowExW(0, WC_BUTTONW, L"только выбранное устройство",
                                                  WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                                  1330, 226, 290, 24,
                                                  hwnd,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSelectedDeviceButtonId)),
                                                  GetModuleHandleW(nullptr),
                                                  nullptr);
    set_child_font(g_app.selected_device_radio);

    g_app.live_capture_check = CreateWindowExW(0, WC_BUTTONW, L"live loopback захват",
                                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                               1330, 254, 250, 24,
                                               hwnd,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLiveCaptureButtonId)),
                                               GetModuleHandleW(nullptr),
                                               nullptr);
    set_child_font(g_app.live_capture_check);

    Button_SetCheck(g_app.all_devices_radio, BST_CHECKED);
    Button_SetCheck(g_app.live_capture_check, BST_CHECKED);

    const int sx = 24;
    const int sy = 570;

    for (int i = 0; i < 6; ++i) {
        g_app.sliders[i] = create_slider(hwnd, kSliderDialogueId + i, sx + i * 300, sy + 6, 260);
    }

    if (g_app.shared) {
        set_slider(g_app.sliders[0], g_app.shared->params.dialogue_boost_db, 0.0f, 14.0f);
        set_slider(g_app.sliders[1], g_app.shared->params.effects_duck_db, -14.0f, 0.0f);
        set_slider(g_app.sliders[2], g_app.shared->params.bass_duck_db, -18.0f, 0.0f);
        set_slider(g_app.sliders[3], g_app.shared->params.bass_gain_db, -10.0f, 8.0f);
        set_slider(g_app.sliders[4], g_app.shared->params.stereo_width, 0.5f, 1.8f);
        set_slider(g_app.sliders[5], g_app.shared->params.output_gain_db, -12.0f, 3.0f);
    }

    enumerate_render_endpoints();
}

void apply_endpoint_mode_from_radios() {
    if (!g_app.shared) {
        return;
    }

    if (Button_GetCheck(g_app.all_devices_radio) == BST_CHECKED) {
        InterlockedExchange(&g_app.shared->params.endpoint_mode_value,
                            static_cast<LONG>(cdm_shared::endpoint_mode::all_devices));
    } else {
        const int sel = static_cast<int>(SendMessageW(g_app.endpoint_combo, CB_GETCURSEL, 0, 0));
        select_endpoint_to_shared(sel);
    }

    InterlockedIncrement(&g_app.shared->endpoint_sequence);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE:
        g_app.hwnd = hwnd;
        ensure_shared_memory();
        create_controls(hwnd);
        start_capture();
        SetTimer(hwnd, kTimerId, kTimerMs, nullptr);
        return 0;

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, rgb(226, 232, 240));
        SetBkColor(dc, rgb(13, 20, 33));
        static HBRUSH brush = CreateSolidBrush(rgb(13, 20, 33));
        return reinterpret_cast<LRESULT>(brush);
    }

    case WM_HSCROLL:
        sync_params_from_controls();
        return 0;

    case WM_COMMAND:
        if (LOWORD(wparam) == kEndpointComboId && HIWORD(wparam) == CBN_SELCHANGE) {
            Button_SetCheck(g_app.selected_device_radio, BST_CHECKED);
            Button_SetCheck(g_app.all_devices_radio, BST_UNCHECKED);
            const int sel = static_cast<int>(SendMessageW(g_app.endpoint_combo, CB_GETCURSEL, 0, 0));
            select_endpoint_to_shared(sel);
            restart_capture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (LOWORD(wparam) == kAllDevicesButtonId || LOWORD(wparam) == kSelectedDeviceButtonId) {
            apply_endpoint_mode_from_radios();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (LOWORD(wparam) == kLiveCaptureButtonId) {
            const bool enabled = Button_GetCheck(g_app.live_capture_check) == BST_CHECKED;
            g_app.capture_enabled.store(enabled);
            if (enabled) {
                start_capture();
            } else {
                stop_capture();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        return 0;

    case WM_KEYDOWN:
        if (g_app.shared) {
            if (wparam == 'S') {
                const LONG cur = InterlockedCompareExchange(&g_app.shared->params.simulation_enabled, 0, 0);
                InterlockedExchange(&g_app.shared->params.simulation_enabled, cur ? 0 : 1);
            } else if (wparam == 'B') {
                const LONG cur = InterlockedCompareExchange(&g_app.shared->params.bypass, 0, 0);
                InterlockedExchange(&g_app.shared->params.bypass, cur ? 0 : 1);
            } else if (wparam == 'M') {
                const LONG cur = InterlockedCompareExchange(&g_app.shared->params.output_mode_value, 0, 0);
                const LONG next = cur == static_cast<LONG>(cdm_shared::output_mode::stereo_2_1)
                    ? static_cast<LONG>(cdm_shared::output_mode::stereo_2_0)
                    : static_cast<LONG>(cdm_shared::output_mode::stereo_2_1);
                InterlockedExchange(&g_app.shared->params.output_mode_value, next);
            } else if (wparam == 'R') {
                enumerate_render_endpoints();
                restart_capture();
            } else if (wparam == 'L') {
                const bool enabled = !g_app.capture_enabled.load();
                g_app.capture_enabled.store(enabled);
                Button_SetCheck(g_app.live_capture_check, enabled ? BST_CHECKED : BST_UNCHECKED);
                if (enabled) {
                    start_capture();
                } else {
                    stop_capture();
                }
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_TIMER:
        update_simulation();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        stop_capture();
        close_shared_memory();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpszClassName = L"cdm_control_app_v19";
    wc.lpfnWndProc = wnd_proc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0,
                                wc.lpszClassName,
                                L"cdm control app v19",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                1840,
                                760,
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);

    ShowWindow(hwnd, show_cmd);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}
