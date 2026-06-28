﻿# подробная установка cdm apo драйвера через sysvad swapapo

## 1. что мы ставим

`CDMControlApp.exe` — это только gui, параметры, метры и диагностический live loopback.

реальная обработка системного звука появляется только когда cdm core вызывается из audio processing object:

```text
windows audio engine -> apo -> cdm core -> render endpoint
```

на этапе разработки используем `SysVAD SwapAPO`, потому что это официальный пример виртуального аудиодрайвера с APO-расширением.

## 2. требования

поставь на машине разработки:

```text
visual studio 2022
desktop development with c++
windows 10/11 sdk
windows driver kit, wdk
git
```

wdk ставь после visual studio.

проверка:

```bat
where msbuild
where signtool
where pnputil
```

`pnputil` есть в windows. `signtool` обычно доступен из `Developer Command Prompt for VS`.

## 3. распаковать проект

пример:

```bat
mkdir C:\dev
cd /d C:\dev
```

распакуй архив в:

```text
C:\dev\cdm_vs_project_v19_apo_checked
```

## 4. проверить gui

сначала проверь, что gui живой:

```bat
cd /d C:\dev\cdm_vs_project_v19_apo_checked
scripts\build_control_app.bat
bin\x64\Release\CDMControlApp.exe
```

ожидание:

```text
live loopback работает
метры двигаются от реального звука
кодировка нормальная
```

## 5. включить test signing

cmd от администратора:

```bat
cd /d C:\dev\cdm_vs_project_v19_apo_checked
scripts\enable_test_mode.bat
```

или руками:

```bat
bcdedit /set testsigning on
```

потом перезагрузка.

если команда не проходит из-за secure boot, временно отключи secure boot в bios/uefi.

## 6. создать и установить тестовый сертификат

powershell от администратора:

```powershell
cd C:\dev\cdm_vs_project_v19_apo_checked
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\create_test_cert.ps1
```

cmd от администратора:

```bat
cd /d C:\dev\cdm_vs_project_v19_apo_checked
scripts\install_test_cert.bat
```

после этого сертификат будет в:

```text
Trusted Root Certification Authorities
Trusted Publishers
```

## 7. скачать и подготовить sysvad

powershell:

```powershell
cd C:\dev\cdm_vs_project_v19_apo_checked
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\prepare_sysvad_cdm_apo.ps1
```

скрипт создаст:

```text
C:\dev\cdm_vs_project_v19_apo_checked\external\Windows-driver-samples
```

и скопирует cdm-файлы сюда:

```text
external\Windows-driver-samples\audio\sysvad\CDM
```

## 8. открыть sysvad solution

ищи `.sln` здесь:

```text
external\Windows-driver-samples\audio\sysvad
```

открой solution в visual studio.

configuration:

```text
Release
x64
```

## 9. найти проект SwapAPO

в solution explorer найди проект:

```text
SwapAPO
```

или похожий apo-проект внутри sysvad.

именно туда надо встраивать cdm.

## 10. добавить include directory

правый клик по `SwapAPO`:

```text
Properties
C/C++
General
Additional Include Directories
```

добавь:

```text
$(SolutionDir)CDM
```

если не сработает, добавь абсолютный путь:

```text
C:\dev\cdm_vs_project_v19_apo_checked\external\Windows-driver-samples\audio\sysvad\CDM
```

## 11. добавить cdm-файлы в проект

в проект `SwapAPO` добавь как existing items:

```text
CDM\cdm_realtime_shared.hpp
CDM\cdm_core_realtime.hpp
CDM\cdm_apo_process.hpp
CDM\cdm_sysvad_swapapo_bridge.hpp
```

`cdm_swapapo_hook_example.cpp` не обязательно добавлять в сборку. это пример, куда смотреть.

## 12. найти realtime process callback

в проекте `SwapAPO` ищи:

```text
APOProcess
IAudioProcessingObjectRT
APO_CONNECTION_PROPERTY
SwapAPO
SFX
MFX
```

нужна функция, где реально проходит аудиобуфер.

главная цель: найти место, где есть:

```text
указатель на буфер samples
количество кадров frame_count
количество каналов channel_count
sample_rate
```

## 13. добавить include

в файл с callback добавь:

```cpp
#include "cdm_sysvad_swapapo_bridge.hpp"
```

## 14. вставить вызов cdm

когда нашел interleaved float32 output buffer, вставь:

```cpp
cdm_sysvad_bridge::process_float32_no_endpoint(
    reinterpret_cast<float*>(buffer),
    frame_count,
    channel_count,
    sample_rate
);
```

важно:

```text
buffer должен быть именно float32
buffer должен быть именно output path
buffer должен быть interleaved
frame_count — число audio frames, не число float samples
channel_count — число каналов
```

если у тебя буфер не float32, нельзя просто кастовать в `float*`.

## 15. если формат не float32

сначала не трогай этот путь.

нужно либо найти float32 path в sysvad, либо сделать временный конвертер:

```text
int16/int32 -> float32 -> cdm -> int16/int32
```

но для первого запуска лучше добиться float32.

## 16. собрать sysvad

в visual studio:

```text
Build -> Build Solution
```

configuration:

```text
Release x64
```

если падает сборка cdm include — проверь `Additional Include Directories`.

если падает на `Interlocked...` или windows headers — проверь, что файлы добавлены в C++ project, а не в странный фильтр без include path.

## 17. найти выходной package

после сборки ищи output с файлами:

```text
*.inf
*.cat
*.sys
*.dll
```

обычно это где-то в:

```text
external\Windows-driver-samples\audio\sysvad\...\x64\Release
```

точный путь зависит от версии sample и visual studio.

## 18. подписать cat

cmd `Developer Command Prompt for VS` от администратора:

```bat
cd /d C:\dev\cdm_vs_project_v19_apo_checked
scripts\sign_cat_example.bat C:\path\to\sysvad.cat
```

если `signtool` не найден — ты не в developer command prompt или не установлен windows sdk.

## 19. установить driver package

cmd от администратора:

```bat
cd /d C:\dev\cdm_vs_project_v19_apo_checked
scripts\install_driver_package_example.bat C:\path\to\sysvad.inf
```

или руками:

```bat
pnputil /add-driver C:\path\to\sysvad.inf /install
```

## 20. перезапустить аудио

```bat
scripts\restart_audio_service.bat
```

если после этого странности — перезагрузи windows.

## 21. выбрать sysvad endpoint

открой:

```text
control panel -> sound -> playback
```

или:

```bat
mmsys.cpl
```

найди устройство типа:

```text
SYSVAD
SYSVAD with APO Extensions
```

выставь его default playback device.

## 22. проверить через gui

запусти:

```text
CDMControlApp.exe
```

рекомендую от администратора.

в gui:

```text
выключи live loopback захват
выбери режим обрабатывать все устройства
запусти звук
```

ожидание:

```text
сверху появится active apo endpoint
метры двигаются
bypass меняет поведение
```

если включен live loopback, он может маскировать проблему. для проверки настоящего apo выключай loopback.

## 23. понять, что именно работает

вариант 1:

```text
live loopback работает
active apo endpoint пустой
```

это значит: gui принимает звук сам, но apo не установлен или не вызывает cdm.

вариант 2:

```text
live loopback выключен
active apo endpoint появился
метры двигаются
```

это значит: настоящий apo пишет в shared memory.

вариант 3:

```text
active apo endpoint есть
метры двигаются
звук не меняется
```

значит cdm стоит не в output path или обрабатывает копию буфера.

вариант 4:

```text
после установки нет звука
```

сразу нажми `b` для bypass. если звук не вернулся, ошибка в apo glue/format/install, не в dsp.

## 24. откат

посмотреть драйверы:

```bat
pnputil /enum-drivers
```

найди `oem*.inf`, который относится к sysvad/cdm.

удалить:

```bat
scripts\remove_oem_driver_example.bat oem123.inf
```

выключить test mode:

```bat
scripts\disable_test_mode.bat
```

перезагрузка.

## 25. что сейчас не автоматизировано

в v19 не делается автоматический patch исходников sysvad, потому что у разных версий sample разные имена файлов и callback может отличаться.

мы кладем готовый bridge и точку вызова:

```text
ApoPatch\cdm_sysvad_swapapo_bridge.hpp
ApoPatch\cdm_swapapo_hook_example.cpp
```

тебе нужно руками вставить один вызов в реальный `APOProcess`.
