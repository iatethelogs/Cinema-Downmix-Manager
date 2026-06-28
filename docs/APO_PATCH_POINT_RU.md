﻿# куда вставлять cdm в sysvad swapapo

## искать

в проекте `SwapAPO` ищи по словам:

```text
APOProcess
IAudioProcessingObjectRT
APO_CONNECTION_PROPERTY
pInputConnections
pOutputConnections
```

## включить header

```cpp
#include "cdm_sysvad_swapapo_bridge.hpp"
```

## вставить вызов

в самом конце обработки output buffer, но до возврата из callback:

```cpp
cdm_sysvad_bridge::process_float32_no_endpoint(
    reinterpret_cast<float*>(buffer),
    frame_count,
    channel_count,
    sample_rate
);
```

## не вставлять

не вставлять в constructor, initialize, property page, registration code.

нужен именно realtime audio buffer path.

## если не знаешь какой buffer output

временно сделай жесткий тест:

```cpp
float* x = reinterpret_cast<float*>(buffer);
for (UINT32 i = 0; i < frame_count * channel_count; ++i) {
    x[i] *= 0.1f;
}
```

если звук стал тише — это тот буфер.

если не изменился — ты обрабатываешь не тот path.
