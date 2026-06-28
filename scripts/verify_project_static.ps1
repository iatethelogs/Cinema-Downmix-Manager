﻿# статическая проверка проекта cdm
# не компилирует wdk, а проверяет структуру, версии и кодировку

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")

$required = @(
  "CDMStudio.sln",
  "ControlApp\main.cpp",
  "ControlApp\CDMControlApp.vcxproj",
  "Common\cdm_realtime_shared.hpp",
  "Common\cdm_core_realtime.hpp",
  "ApoPatch\cdm_apo_process.hpp",
  "ApoPatch\cdm_sysvad_swapapo_bridge.hpp",
  "docs\APO_INSTALL_FULL_RU.md"
)

foreach ($f in $required) {
  $p = Join-Path $root $f
  if (!(Test-Path $p)) {
    throw "нет файла: $f"
  }
}

$shared = Get-Content (Join-Path $root "Common\cdm_realtime_shared.hpp") -Raw
if ($shared -notmatch "CDM_APO_SHARED_STATE_V19") {
  throw "shared memory version не v19"
}

$vcx = Get-Content (Join-Path $root "ControlApp\CDMControlApp.vcxproj") -Raw
if ($vcx -notmatch "/utf-8") {
  throw "в vcxproj нет /utf-8"
}

if ($vcx -notmatch "avrt.lib") {
  throw "в vcxproj нет avrt.lib"
}

Write-Host "ok: структура проекта нормальная"
Write-Host "ok: shared memory v19"
Write-Host "ok: /utf-8 включен"
Write-Host "ok: avrt.lib подключен"
