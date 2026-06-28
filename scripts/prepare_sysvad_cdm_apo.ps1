﻿# запускай из корня проекта cdm
# скачивает windows-driver-samples и копирует cdm файлы рядом с sysvad

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$external = Join-Path $root "external"
$samples = Join-Path $external "Windows-driver-samples"
$sysvad = Join-Path $samples "audio\sysvad"
$cdmTarget = Join-Path $sysvad "CDM"

New-Item -ItemType Directory -Force -Path $external | Out-Null

if (!(Test-Path $samples)) {
    git clone --depth 1 https://github.com/microsoft/Windows-driver-samples.git $samples
}

if (!(Test-Path $sysvad)) {
    throw "sysvad не найден: $sysvad"
}

New-Item -ItemType Directory -Force -Path $cdmTarget | Out-Null

Copy-Item (Join-Path $root "Common\cdm_realtime_shared.hpp") $cdmTarget -Force
Copy-Item (Join-Path $root "Common\cdm_core_realtime.hpp") $cdmTarget -Force
Copy-Item (Join-Path $root "ApoPatch\cdm_apo_process.hpp") $cdmTarget -Force
Copy-Item (Join-Path $root "ApoPatch\cdm_sysvad_swapapo_bridge.hpp") $cdmTarget -Force
Copy-Item (Join-Path $root "ApoPatch\cdm_swapapo_hook_example.cpp") $cdmTarget -Force

Write-Host ""
Write-Host "готово"
Write-Host "sysvad: $sysvad"
Write-Host "cdm files: $cdmTarget"
Write-Host ""
Write-Host "дальше открой sysvad solution и добавь папку CDM в include dirs проекта SwapAPO."

Write-Host "cdm v19 files copied"
