# Install an extracted Windows package for the current user.
[CmdletBinding()]
param([string]$InstallDir = "$env:LOCALAPPDATA\Programs\BlackSquelch", [switch]$NoPath)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Use install.sh on Linux or macOS.' }
$architecture = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
if ($architecture -ne 'AMD64') { throw 'This package requires Windows x64.' }
$info = ConvertFrom-StringData (Get-Content -LiteralPath "$PSScriptRoot/BUILD_INFO" -Raw)
if ($info.platform -ne 'windows' -or $info.arch -ne 'x86_64') { throw 'Wrong package for this computer.' }
$sourceBin = Join-Path $PSScriptRoot 'bin'
$actual = & "$sourceBin/blacksquelch.exe" --version
if ($LASTEXITCODE -ne 0 -or $actual -ne "BlackSquelch $($info.version)") { throw 'Package executable cannot run or has the wrong version.' }
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
if ($InstallDir.TrimEnd('\') -eq [IO.Path]::GetPathRoot($InstallDir).TrimEnd('\')) { throw 'Do not install in a drive root.' }
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$stage = Join-Path $InstallDir ('.install-' + [Guid]::NewGuid().ToString('N'))
$backup = Join-Path $InstallDir ('.previous-' + [Guid]::NewGuid().ToString('N'))
$targetBin = Join-Path $InstallDir 'bin'
$metadata = @('README.md', 'LICENSE-miniaudio.txt', 'BUILD_INFO', 'uninstall.ps1', 'install-manifest.json')
$moved = $false
$installed = $false
$metadataTouched = $false
$committed = $false
try {
    New-Item -ItemType Directory -Path "$stage/bin", "$stage/metadata" -Force | Out-Null
    if (Test-Path -LiteralPath $targetBin) {
        if (-not (Test-Path -LiteralPath $targetBin -PathType Container)) { throw 'Install bin path is not a directory.' }
        Get-ChildItem -LiteralPath $targetBin -Force | Copy-Item -Destination "$stage/bin" -Recurse -Force
    }
    $oldManifest = Join-Path $InstallDir 'install-manifest.json'
    if (Test-Path -LiteralPath $oldManifest) {
        foreach ($name in (Get-Content -LiteralPath $oldManifest -Raw | ConvertFrom-Json).files) {
            if ($name -notmatch '^(blacksquelch\.exe|[A-Za-z0-9_.+-]+\.dll)$') { throw 'Invalid previous install manifest.' }
            Remove-Item -LiteralPath "$stage/bin/$name" -Force -ErrorAction SilentlyContinue
        }
    }
    $names = @()
    foreach ($file in Get-ChildItem -LiteralPath $sourceBin -File) {
        if ($file.Name -notmatch '^(blacksquelch\.exe|[A-Za-z0-9_.+-]+\.dll)$') { throw 'Unexpected package file.' }
        Copy-Item -LiteralPath $file.FullName -Destination "$stage/bin" -Force
        $names += $file.Name
    }
    foreach ($name in $metadata) {
        if (Test-Path -LiteralPath "$InstallDir/$name") {
            Copy-Item -LiteralPath "$InstallDir/$name" -Destination "$stage/metadata/$name"
        }
    }
    if (Test-Path -LiteralPath $targetBin) {
        Move-Item -LiteralPath $targetBin -Destination $backup
        $moved = $true
    }
    Move-Item -LiteralPath "$stage/bin" -Destination $targetBin
    $installed = $true
    $metadataTouched = $true
    foreach ($name in $metadata | Where-Object { $_ -ne 'install-manifest.json' }) {
        Copy-Item -LiteralPath "$PSScriptRoot/$name" -Destination "$InstallDir/$name" -Force
    }
    @{ files = @($names) } | ConvertTo-Json | Set-Content -LiteralPath "$InstallDir/install-manifest.json" -Encoding UTF8
    $committed = $true
} catch {
    if ($installed -and (Test-Path -LiteralPath $targetBin)) { Remove-Item -LiteralPath $targetBin -Recurse -Force }
    if ($moved) {
        Move-Item -LiteralPath $backup -Destination $targetBin
    }
    if ($metadataTouched) {
        foreach ($name in $metadata) {
            if (Test-Path -LiteralPath "$stage/metadata/$name") {
                Copy-Item -LiteralPath "$stage/metadata/$name" -Destination "$InstallDir/$name" -Force
            } elseif (Test-Path -LiteralPath "$InstallDir/$name") {
                Remove-Item -LiteralPath "$InstallDir/$name" -Force
            }
        }
    }
    throw
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    if ($committed -and (Test-Path -LiteralPath $backup)) { Remove-Item -LiteralPath $backup -Recurse -Force }
}
if (-not $NoPath) {
    try {
        $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
        $parts = @($userPath -split ';' | Where-Object { $_ })
        if (-not ($parts | Where-Object { [Environment]::ExpandEnvironmentVariables($_).TrimEnd('\') -ieq $targetBin.TrimEnd('\') })) {
            [Environment]::SetEnvironmentVariable('Path', (($parts + $targetBin) -join ';'), 'User')
        }
        $env:Path = "$targetBin;$env:Path"
    } catch { Write-Warning "Installed, but PATH could not be updated. Run $targetBin\blacksquelch.exe directly." }
}
Write-Host "Installed BlackSquelch $($info.version)"
Write-Host "Run: $targetBin\blacksquelch.exe"
if (-not $NoPath) { Write-Host 'User PATH updated for future sessions: blacksquelch' }
Write-Host "Uninstall: powershell -NoProfile -ExecutionPolicy Bypass -File `"$InstallDir\uninstall.ps1`""
