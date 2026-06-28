#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "cdm_realtime_shared.hpp"

namespace cdm_core {

// основное realtime dsp ядро
// сюда нельзя добавлять файловый ввод-вывод, com, логи, sleep и тяжелые аллокации в callback

static constexpr float kEpsilon = 0.000001f;
static constexpr uint32_t kMaxChannels = 8u;

struct channel_view {
    const float* input[kMaxChannels]{};
    float* output[kMaxChannels]{};

    uint32_t channel_count = 0;
    uint32_t frame_count = 0;
    uint32_t sample_rate = 48000;
};

// однополюсный сглаживатель коэффициентов, чтобы gain не щелкал

struct one_pole_smoother {
    float value = 1.0f;
    float coeff = 0.03f;

    float process(float target) noexcept {
        value += (target - value) * coeff;
        return value;
    }
};

struct dc_blocker {
    float x1 = 0.0f;
    float y1 = 0.0f;

    float process(float x) noexcept {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

// простой limiter на выходе, чтобы после усиления речи не словить клиппинг

struct peak_limiter {
    float release = 0.995f;
    float gain = 1.0f;
    float last_reduction_db = 0.0f;

    float process(float x) noexcept {
        const float abs_x = std::fabs(x);
        const float target = abs_x > 0.96f ? 0.96f / std::max(abs_x, kEpsilon) : 1.0f;
        gain = std::min(target, gain / release);
        gain = std::min(gain, 1.0f);
        last_reduction_db = gain < 0.999f ? 20.0f * std::log10(std::max(gain, kEpsilon)) : 0.0f;
        return x * gain;
    }
};

class cdm_realtime_processor {
public:
    // главная функция обработки interleaved float32 буфера
    // ожидаемый порядок каналов: fl, fr, fc, lfe, sl, sr, bl, br
    void process_interleaved(float* interleaved,
                             uint32_t frame_count,
                             uint32_t channel_count,
                             uint32_t sample_rate,
                             cdm_shared::shared_state* shared) noexcept {
        if (!interleaved || frame_count == 0 || channel_count == 0) {
            return;
        }

        channel_count = std::min<uint32_t>(channel_count, kMaxChannels);

        // если включен bypass, звук не трогаем, но метры входа обновляем
        if (shared && InterlockedCompareExchange(&shared->params.bypass, 0, 0) != 0) {
            fill_bypass_meters(interleaved, frame_count, channel_count, shared);
            return;
        }

        const auto params = shared ? shared->params : cdm_shared::runtime_params{};
        const bool use_sub = params.output_mode_value == static_cast<LONG>(cdm_shared::output_mode::stereo_2_1);

        const float dialogue_boost = cdm_shared::db_to_gain(params.dialogue_boost_db);
        const float effects_duck = cdm_shared::db_to_gain(params.effects_duck_db);
        const float bass_duck = cdm_shared::db_to_gain(params.bass_duck_db);
        const float bass_gain = cdm_shared::db_to_gain(params.bass_gain_db);
        const float output_gain = cdm_shared::db_to_gain(params.output_gain_db);
        const float width = std::clamp(params.stereo_width, 0.25f, 2.0f);

        float in_peak[cdm_shared::kInputChannelCount]{};
        float bus_peak[cdm_shared::kBusCount]{};
        float out_peak[cdm_shared::kOutputChannelCount]{};

        float confidence_accum = 0.0f;
        float voice_gain_accum = 0.0f;
        float effects_gain_accum = 0.0f;
        float bass_gain_accum = 0.0f;

        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            float ch[kMaxChannels]{};
            for (uint32_t c = 0; c < channel_count; ++c) {
                ch[c] = interleaved[frame * channel_count + c];
                if (c < cdm_shared::kInputChannelCount) {
                    in_peak[c] = std::max(in_peak[c], std::fabs(ch[c]));
                }
            }

            const float fl = read_channel(ch, channel_count, 0);
            const float fr = read_channel(ch, channel_count, 1);
            const float fc = read_channel(ch, channel_count, 2);
            const float lfe = read_channel(ch, channel_count, 3);
            const float sl = read_channel(ch, channel_count, 4);
            const float sr = read_channel(ch, channel_count, 5);
            const float bl = read_channel(ch, channel_count, 6);
            const float br = read_channel(ch, channel_count, 7);

            const float phantom_center = 0.5f * (fl + fr);
            // строим три смысловые шины: голос, эффекты и бас
            const float voice = build_dialogue_candidate(fc, phantom_center, channel_count);
            const float effects_l = build_effects_left(fl, sl, bl, voice);
            const float effects_r = build_effects_right(fr, sr, br, voice);
            const float bass = build_bass_bus(lfe, fl, fr) * bass_gain;

            // детектор не смешивает звук, он только считает насколько активна речь
            const float confidence = speech_detector(voice, effects_l, effects_r, bass);
            const float target_voice = 1.0f + confidence * (dialogue_boost - 1.0f);
            const float target_effects = 1.0f + confidence * (effects_duck - 1.0f);
            const float target_bass = 1.0f + confidence * (bass_duck - 1.0f);

            const float voice_gain = voice_smoother_.process(target_voice);
            const float effects_gain = effects_smoother_.process(target_effects);
            const float bass_duck_gain = bass_smoother_.process(target_bass);

            const float balanced_voice = voice * voice_gain;
            const float balanced_effects_l = effects_l * effects_gain;
            const float balanced_effects_r = effects_r * effects_gain;
            const float balanced_bass = bass * bass_duck_gain;

            float out_l = 0.0f;
            float out_r = 0.0f;
            float out_sub = 0.0f;
            // после баланса собираем выход l/r/sub
            downmix_matrix(balanced_voice,
                           balanced_effects_l,
                           balanced_effects_r,
                           balanced_bass,
                           use_sub,
                           out_l,
                           out_r,
                           out_sub);

            apply_stereo_width(out_l, out_r, width);

            out_l = limiter_l_.process(out_l * output_gain);
            out_r = limiter_r_.process(out_r * output_gain);
            out_sub = limiter_s_.process(out_sub * output_gain);

            write_output(interleaved, frame, channel_count, out_l, out_r, out_sub, use_sub);

            bus_peak[0] = std::max(bus_peak[0], std::fabs(voice));
            bus_peak[1] = std::max(bus_peak[1], std::fabs(effects_l));
            bus_peak[2] = std::max(bus_peak[2], std::fabs(effects_r));
            bus_peak[3] = std::max(bus_peak[3], std::fabs(bass));

            out_peak[0] = std::max(out_peak[0], std::fabs(out_l));
            out_peak[1] = std::max(out_peak[1], std::fabs(out_r));
            out_peak[2] = std::max(out_peak[2], std::fabs(out_sub));

            confidence_accum += confidence;
            voice_gain_accum += voice_gain;
            effects_gain_accum += effects_gain;
            bass_gain_accum += bass_duck_gain;
        }

        if (shared) {
            for (uint32_t i = 0; i < cdm_shared::kInputChannelCount; ++i) {
                shared->meter.input_peak[i] = in_peak[i];
            }
            for (uint32_t i = 0; i < cdm_shared::kBusCount; ++i) {
                shared->meter.bus_peak[i] = bus_peak[i];
            }
            for (uint32_t i = 0; i < cdm_shared::kOutputChannelCount; ++i) {
                shared->meter.output_peak[i] = out_peak[i];
            }

            const float inv = 1.0f / static_cast<float>(frame_count);
            shared->meter.dialogue_confidence = confidence_accum * inv;
            shared->meter.voice_gain = voice_gain_accum * inv;
            shared->meter.effects_gain = effects_gain_accum * inv;
            shared->meter.bass_gain = bass_gain_accum * inv;
            shared->meter.limiter_reduction_db = std::min({limiter_l_.last_reduction_db,
                                                           limiter_r_.last_reduction_db,
                                                           limiter_s_.last_reduction_db,
                                                           0.0f});
            shared->meter.frame_counter++;
            InterlockedIncrement(&shared->meter_sequence);
        }
    }

private:
    one_pole_smoother voice_smoother_{};
    one_pole_smoother effects_smoother_{};
    one_pole_smoother bass_smoother_{};
    peak_limiter limiter_l_{};
    peak_limiter limiter_r_{};
    peak_limiter limiter_s_{};

    static float read_channel(const float* ch, uint32_t count, uint32_t index) noexcept {
        return index < count ? ch[index] : 0.0f;
    }

    static float build_dialogue_candidate(float fc, float phantom_center, uint32_t channel_count) noexcept {
        if (channel_count >= 3) {
            return fc * 0.85f + phantom_center * 0.15f;
        }
        return phantom_center;
    }

    static float build_effects_left(float fl, float sl, float bl, float voice) noexcept {
        return fl * 0.70f + sl * 0.23f + bl * 0.17f - voice * 0.08f;
    }

    static float build_effects_right(float fr, float sr, float br, float voice) noexcept {
        return fr * 0.70f + sr * 0.23f + br * 0.17f - voice * 0.08f;
    }

    static float build_bass_bus(float lfe, float fl, float fr) noexcept {
        const float derived = (fl + fr) * 0.10f;
        return lfe * 0.85f + derived;
    }

    static float speech_detector(float voice, float effects_l, float effects_r, float bass) noexcept {
        const float voice_energy = std::fabs(voice);
        const float mask_energy = std::fabs(effects_l) * 0.35f + std::fabs(effects_r) * 0.35f + std::fabs(bass) * 0.55f;
        const float ratio = voice_energy / (voice_energy + mask_energy + 0.025f);
        return std::clamp(ratio * 1.8f, 0.0f, 1.0f);
    }

    static void downmix_matrix(float voice,
                               float effects_l,
                               float effects_r,
                               float bass,
                               bool use_sub,
                               float& out_l,
                               float& out_r,
                               float& out_sub) noexcept {
        out_l = effects_l + voice * 0.72f;
        out_r = effects_r + voice * 0.72f;

        if (use_sub) {
            out_sub = bass;
            out_l += bass * 0.08f;
            out_r += bass * 0.08f;
        } else {
            out_sub = 0.0f;
            out_l += bass * 0.28f;
            out_r += bass * 0.28f;
        }
    }

    static void apply_stereo_width(float& l, float& r, float width) noexcept {
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * width;
        l = mid + side;
        r = mid - side;
    }

    static void write_output(float* interleaved,
                             uint32_t frame,
                             uint32_t channel_count,
                             float l,
                             float r,
                             float sub,
                             bool use_sub) noexcept {
        interleaved[frame * channel_count + 0] = l;
        if (channel_count > 1) {
            interleaved[frame * channel_count + 1] = r;
        }
        if (use_sub && channel_count > 2) {
            interleaved[frame * channel_count + 2] = 0.5f * (l + r);
        }
        if (use_sub && channel_count > 3) {
            interleaved[frame * channel_count + 3] = sub;
        }
    }

    static void fill_bypass_meters(float* interleaved,
                                   uint32_t frame_count,
                                   uint32_t channel_count,
                                   cdm_shared::shared_state* shared) noexcept {
        float peaks[cdm_shared::kInputChannelCount]{};
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            for (uint32_t c = 0; c < std::min<uint32_t>(channel_count, cdm_shared::kInputChannelCount); ++c) {
                peaks[c] = std::max(peaks[c], std::fabs(interleaved[frame * channel_count + c]));
            }
        }

        for (uint32_t c = 0; c < cdm_shared::kInputChannelCount; ++c) {
            shared->meter.input_peak[c] = peaks[c];
        }

        shared->meter.dialogue_confidence = 0.0f;
        shared->meter.voice_gain = 1.0f;
        shared->meter.effects_gain = 1.0f;
        shared->meter.bass_gain = 1.0f;
        shared->meter.limiter_reduction_db = 0.0f;
        shared->meter.frame_counter++;
        InterlockedIncrement(&shared->meter_sequence);
    }
};

} // namespace cdm_core
