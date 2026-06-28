@echo off
set CERT=%~dp0..\cdm_test_driver.cer
if not exist "%CERT%" (
  echo не найден %CERT%
  echo сначала запусти scripts\create_test_cert.ps1
  exit /b 1
)

certutil -addstore -f Root "%CERT%"
certutil -addstore -f TrustedPublisher "%CERT%"
pause
