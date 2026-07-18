Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYSCON -ErrorAction SilentlyContinue
. 'C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1' 2>$null | Out-Null
Set-Location 'C:\Users\bujih\Desktop\code\github\ellenp2p\esp32-pet'
$env:MSYSTEM = ''
$env:MSYSCON = ''
# COM5 from project memory
idf.py -p COM5 flash 2>&1 | Out-File -FilePath 'flash.log' -Encoding utf8
"=== EXIT $LASTEXITCODE ==="