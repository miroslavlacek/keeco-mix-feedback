# Installs the Antigravity CLI (agy) for the current user if it is missing. Used by the MixSight installer.
$agy = Join-Path $env:LOCALAPPDATA "agy\bin\agy.exe"
if (Test-Path $agy) { Write-Host "Antigravity CLI is already installed: $agy"; exit 0 }
try {
    Invoke-RestMethod https://antigravity.google/cli/install.ps1 | Invoke-Expression
} catch {
    Write-Host "Antigravity CLI installation failed: $_"
}
exit 0
