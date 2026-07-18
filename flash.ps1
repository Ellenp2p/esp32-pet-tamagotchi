param(
    [string]$IdfProfile = "$env:IDF_TOOLS_PATH\Microsoft.v6.0.2.PowerShell_profile.ps1",
    [string]$Port = "COM5"
)

Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYSCON -ErrorAction SilentlyContinue

# Source the ESP-IDF PowerShell profile if it exists
if (Test-Path -LiteralPath $IdfProfile) {
    . $IdfProfile 2>$null | Out-Null
} elseif ($env:IDF_PATH) {
    $exportPs1 = Join-Path -Path $env:IDF_PATH -ChildPath "export.ps1"
    if (Test-Path -LiteralPath $exportPs1) {
        . $exportPs1 2>$null | Out-Null
    }
}

idf.py -p $Port flash 2>&1 | Out-File -FilePath 'flash.log' -Encoding utf8
"=== EXIT $LASTEXITCODE ==="