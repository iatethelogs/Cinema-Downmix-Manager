# статический отчет v19

- `CDMStudio.sln`: ok
- `ControlApp/CDMControlApp.vcxproj`: ok
- `ControlApp/main.cpp`: ok
- `Common/cdm_realtime_shared.hpp`: ok
- `Common/cdm_core_realtime.hpp`: ok
- `ApoPatch/cdm_apo_process.hpp`: ok
- `ApoPatch/cdm_sysvad_swapapo_bridge.hpp`: ok
- `docs/APO_INSTALL_FULL_RU.md`: ok

## проверки текста

- shared v19: ok
- version 19: ok
- utf-8 option: ok
- avrt.lib: ok
- sysvad bridge: ok
- русские комментарии core: ok
- русские комментарии apo: ok

итог: структура выглядит нормально

примечание: wdk/sysvad здесь не компилировался, потому что среда выполнения не содержит visual studio и wdk.