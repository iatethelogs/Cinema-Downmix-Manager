﻿# включение cdm через apo — пошагово

## 0. что сейчас есть

в v17/v18 уже работает live loopback:

```text
windows sound -> wasapi loopback -> cdm core -> gui meters
```

это нужно для проверки, что ядро и визуализация живые.

но это не меняет звук на выходе.

чтобы реально менять звук всей системы, нужен путь:

```text
windows audio engine -> apo -> cdm core -> render endpoint
```

для этого нужен windows apo. он грузится не из нашего exe, а из audio driver package.

## 1. что поставить

нужно:

```text
visual studio 2022
desktop development with c++
windows 10/11 sdk
windows driver kit
git
```

wdk должен быть установлен после visual studio, чтобы появились driver templates и build targets.

## 2. включить test mode

открой cmd от администратора:

```bat
scripts\enable_test_mode.bat
```

или руками:

```bat
bcdedit /set testsigning on
```

перезагрузка обязательна.

если windows ругается на secure boot, отключи secure boot в bios/uefi на время разработки.

## 3. создать тестовый сертификат

powershell от администратора:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\create_test_cert.ps1
```

потом cmd от администратора:

```bat
scripts\install_test_cert.bat
```

## 4. подготовить sysvad

из корня проекта cdm:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\prepare_sysvad_cdm_apo.ps1
```

скрипт скачает:

```text
external\Windows-driver-samples
```

и скопирует наши файлы в:

```text
external\Windows-driver-samples\audio\sysvad\CDM
```

## 5. открыть sysvad

открой solution из:

```text
external\Windows-driver-samples\audio\sysvad
```

название solution может отличаться, ищи `.sln`.

тебе нужен проект `SwapAPO`.

## 6. добавить cdm в SwapAPO

в свойствах проекта `SwapAPO`:

```text
C/C++ -> General -> Additional Include Directories
```

добавь:

```text
$(SolutionDir)CDM
```

если не находится — добавь абсолютный путь:

```text
C:\...\external\Windows-driver-samples\audio\sysvad\CDM
```

потом добавь в проект файлы из папки `CDM`:

```text
cdm_realtime_shared.hpp
cdm_core_realtime.hpp
cdm_apo_process.hpp
cdm_sysvad_swapapo_bridge.hpp
```

## 7. найти process callback

в проекте `SwapAPO` ищи по словам:

```text
APOProcess
IAudioProcessingObjectRT
Process
SwapAPOSFX
SwapAPOMFX
```

тебе нужен realtime callback, где sysvad уже получает аудиоблок.

примерно это место выглядит по смыслу так:

```cpp
void ...::APOProcess(...)
{
    ...
    // input/output frames
    ...
}
```

или через структуру APO_CONNECTION_PROPERTY.

## 8. вставить вызов cdm

в файл, где лежит process callback, добавь include:

```cpp
#include "cdm_sysvad_swapapo_bridge.hpp"
```

внутри обработки, когда у тебя есть float32 interleaved output buffer, вставь:

```cpp
cdm_sysvad_bridge::process_float32_no_endpoint(
    reinterpret_cast<float*>(buffer),
    frame_count,
    channel_count,
    sample_rate
);
```

где:

```text
buffer        — указатель на interleaved float32 samples
frame_count   — количество аудиокадров
channel_count — количество каналов
sample_rate   — частота, обычно 48000
```

если в sysvad переменные называются иначе — подставь свои.

## 9. важный момент про формат

первый проход делай только для float32 path.

если buffer не float32, а int16/int32, cdm напрямую туда не вставлять. нужен конвертер:

```text
int16/int32 -> float32 -> cdm -> int16/int32
```

сначала добейся, чтобы заработал float32.

## 10. собрать sysvad / swapapo

в visual studio:

```text
configuration: Release
platform: x64
build solution
```

если сборка падает на подписи, сначала собери без установки, потом подпиши cat.

## 11. подписать cat

найди `.cat` в output/package папке sysvad.

пример:

```bat
scripts\sign_cat_example.bat C:\path\to\sysvad.cat
```

если signtool не найден, открой `Developer Command Prompt for VS`.

## 12. установить inf

cmd от администратора:

```bat
scripts\install_driver_package_example.bat C:\path\to\sysvad.inf
```

или руками:

```bat
pnputil /add-driver C:\path\to\sysvad.inf /install
```

## 13. перезапустить audio service

```bat
scripts\restart_audio_service.bat
```

иногда проще перезагрузить windows.

## 14. проверить

1. запусти `CDMControlApp.exe` от администратора.
2. выключи `live loopback захват`, чтобы не путать диагностику с реальным apo.
3. включи звук в windows.
4. сверху должно стать не `apo пока не пишет`, а active endpoint.
5. должны двигаться метры.
6. если выбран режим “только выбранное устройство”, endpoint должен совпасть.

## 15. если звука нет

сразу включи bypass клавишей `b`.

если звук появился — проблема в dsp.

если не появился — проблема в apo integration / format / inf / endpoint.

## 16. если метры есть, а звук не меняется

значит cdm вызван не в output path, а в side path или копии буфера.

надо убедиться, что ты меняешь именно output buffer, который windows потом отдаёт дальше.

## 17. если gui показывает loopback, но apo не пишет

это нормально, если ты не установил sysvad/swapapo.

loopback — отдельный диагностический прием звука.
apo — отдельная системная точка обработки.

## 18. как откатить

посмотреть установленные драйверы:

```bat
pnputil /enum-drivers
```

найти свой `oem*.inf`.

удалить:

```bat
scripts\remove_oem_driver_example.bat oem123.inf
```

выключить test mode:

```bat
scripts\disable_test_mode.bat
```

перезагрузиться.

## 19. что делать дальше

порядок нормальной отладки такой:

```text
1. live loopback двигает метры
2. sysvad swapapo собрался
3. apo грузится и пишет active endpoint
4. bypass работает
5. cdm меняет output buffer
6. endpoint filter работает
7. убираем loopback как костыль
```
