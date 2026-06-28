@echo off
if "%~1"=="" (
  echo укажи путь к cat
  echo пример:
  echo sign_cat_example.bat C:\dev\sysvad\Package\x64\Release\sysvad.cat
  exit /b 1
)

signtool sign /v /fd SHA256 /s My /n "CDM Test Driver Cert" "%~1"
pause
