@echo off
if "%~1"=="" (
  echo укажи oem*.inf из pnputil /enum-drivers
  echo пример:
  echo remove_oem_driver_example.bat oem123.inf
  exit /b 1
)

pnputil /delete-driver "%~1" /uninstall /force
pause
