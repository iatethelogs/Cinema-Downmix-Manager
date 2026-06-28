﻿#include "cdm_sysvad_swapapo_bridge.hpp"

// это пример, а не готовый файл sysvad
// вставлять надо не целиком, а вызов process_float32 в реальный apo process callback

void example_swapapo_process_callback(float* interleaved_float32_buffer,
                                      unsigned int frame_count,
                                      unsigned int channel_count,
                                      unsigned int sample_rate) {
    cdm_sysvad_bridge::process_float32_no_endpoint(interleaved_float32_buffer,
                                                   frame_count,
                                                   channel_count,
                                                   sample_rate);
}
