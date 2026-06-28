﻿# короткая шпаргалка установки apo

```bat
scripts\enable_test_mode.bat
reboot
```

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\create_test_cert.ps1
```

```bat
scripts\install_test_cert.bat
```

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\prepare_sysvad_cdm_apo.ps1
```

дальше руками в visual studio:

```text
открыть external\Windows-driver-samples\audio\sysvad\*.sln
найти SwapAPO
добавить include $(SolutionDir)CDM
добавить include "cdm_sysvad_swapapo_bridge.hpp"
в APOProcess вставить cdm_sysvad_bridge::process_float32_no_endpoint(...)
собрать Release x64
подписать .cat
установить .inf через pnputil
перезапустить audiosrv
```
