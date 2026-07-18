param(
    [string]$IdfProfile = "$env:IDF_TOOLS_PATH\Microsoft.v6.0.2.PowerShell_profile.ps1"
)

# Unset MSYS env so the ESP-IDF profile doesn't bail out
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYSCON -ErrorAction SilentlyContinue

# Source the ESP-IDF PowerShell profile if it exists
if (Test-Path -LiteralPath $IdfProfile) {
    . $IdfProfile 2>$null | Out-Null
} elseif ($env:IDF_PATH) {
    # Fallback: run export.ps1 from IDF_PATH
    $exportPs1 = Join-Path -Path $env:IDF_PATH -ChildPath "export.ps1"
    if (Test-Path -LiteralPath $exportPs1) {
        . $exportPs1 2>$null | Out-Null
    }
}

$ErrorActionPreference = 'Continue'
idf.py build 2>&1 | Out-File -FilePath 'build.log' -Encoding utf8
"=== EXIT $LASTEXITCODE ==="