# Remove application files only. Keep preferences, macros, logs and received files.
[CmdletBinding()]
param([string]$InstallDir = '', [switch]$NoPath)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Use uninstall.sh on Linux or macOS.' }
if (-not $InstallDir) {
    $InstallDir = if (Test-Path -LiteralPath "$PSScriptRoot/install-manifest.json") { $PSScriptRoot } else { "$env:LOCALAPPDATA\Programs\BlackSquelch" }
}
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
if ($InstallDir.TrimEnd('\') -eq [IO.Path]::GetPathRoot($InstallDir).TrimEnd('\')) { throw 'Invalid install directory.' }
$manifest = Join-Path $InstallDir 'install-manifest.json'
if (-not (Test-Path -LiteralPath $manifest)) { throw 'Install manifest not found; no files were removed.' }
$names = @((Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).files)
foreach ($name in $names) {
    if ($name -notmatch '^(blacksquelch\.exe|[A-Za-z0-9_.+-]+\.dll)$') { throw 'Invalid install manifest; no files were removed.' }
}
$targetBin = Join-Path $InstallDir 'bin'
foreach ($name in $names) {
    if (Test-Path -LiteralPath "$targetBin/$name") { Remove-Item -LiteralPath "$targetBin/$name" -Force }
}
if (-not $NoPath) {
    $parts = @([Environment]::GetEnvironmentVariable('Path', 'User') -split ';' | Where-Object {
        $_ -and [Environment]::ExpandEnvironmentVariables($_).TrimEnd('\') -ine $targetBin.TrimEnd('\')
    })
    [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')
}
foreach ($name in @('README.md', 'LICENSE-miniaudio.txt', 'BUILD_INFO', 'install-manifest.json', 'uninstall.ps1')) {
    if (Test-Path -LiteralPath "$InstallDir/$name") { Remove-Item -LiteralPath "$InstallDir/$name" -Force }
}
if ((Test-Path -LiteralPath $targetBin) -and -not (Get-ChildItem -LiteralPath $targetBin -Force)) { Remove-Item -LiteralPath $targetBin }
if ((Test-Path -LiteralPath $InstallDir) -and -not (Get-ChildItem -LiteralPath $InstallDir -Force)) { Remove-Item -LiteralPath $InstallDir }
Write-Host 'BlackSquelch removed. Preferences, macros and received files were kept.'
