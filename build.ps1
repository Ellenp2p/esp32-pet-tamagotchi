# Unset MSYS env so the ESP-IDF profile doesn't bail out
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYSCON -ErrorAction SilentlyContinue
# Suppress the MSYS-not-supported error in the profile (line 135 eim call)
. 'C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1' 2>$null | Out-Null
Set-Location 'C:\Users\bujih\Desktop\code\github\ellenp2p\esp32-pet'
# Verify env
Write-Host "IDF_PATH=$env:IDF_PATH"
Write-Host "IDF_TOOLS_PATH=$env:IDF_TOOLS_PATH"
$env:MSYSTEM = ''
$env:MSYSCON = ''
$ErrorActionPreference = 'Continue'
idf.py build 2>&1 | Out-File -FilePath 'build.log' -Encoding utf8
"=== EXIT $LASTEXITCODE ==="