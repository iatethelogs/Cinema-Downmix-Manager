@echo off
setlocal
cd /d "%~dp0.."
where msbuild >nul 2>nul
if errorlevel 1 (
  echo msbuild не найден. открой CDMStudio.sln в visual studio и собери release x64.
  exit /b 1
)
msbuild CDMStudio.sln /p:Configuration=Release /p:Platform=x64
