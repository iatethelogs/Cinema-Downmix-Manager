﻿# создает тестовый сертификат в currentuser\my
# для реального подписания cat используй signtool из windows sdk

$ErrorActionPreference = "Stop"

$cert = New-SelfSignedCertificate `
  -Type CodeSigningCert `
  -Subject "CN=CDM Test Driver Cert" `
  -CertStoreLocation "Cert:\CurrentUser\My" `
  -KeyExportPolicy Exportable `
  -KeyLength 2048 `
  -HashAlgorithm sha256

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$out = Join-Path $root "cdm_test_driver.cer"

Export-Certificate -Cert $cert -FilePath $out | Out-Null

Write-Host "сертификат создан:"
Write-Host $out
Write-Host ""
Write-Host "установи его в root и trustedpublisher:"
Write-Host "certutil -addstore -f Root $out"
Write-Host "certutil -addstore -f TrustedPublisher $out"
Write-Host ""
Write-Host "thumbprint:"
Write-Host $cert.Thumbprint
