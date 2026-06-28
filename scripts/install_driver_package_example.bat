@echo off
if "%~1"=="" (
  echo укажи путь к inf
  echo пример:
  echo install_driver_package_example.bat C:\dev\sysvad\Package\x64\Release\sysvad.inf
  exit /b 1
)

pnputil /add-driver "%~1" /install
pause
