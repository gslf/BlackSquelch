# Download a Windows release, verify SHA-256, then run its installer.
[CmdletBinding()]
param([string]$Version = 'latest', [string]$InstallDir = "$env:LOCALAPPDATA\Programs\BlackSquelch", [switch]$NoPath)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Use install-release.sh on Linux or macOS.' }
$architecture = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
if ($architecture -ne 'AMD64') { throw 'This installer supports Windows x64.' }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$repo = 'https://github.com/gslf/BlackSquelch'
if ($Version -eq 'latest') {
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/gslf/BlackSquelch/releases/latest' -Headers @{ 'User-Agent' = 'BlackSquelch installer' }
    $Version = $release.tag_name
}
if ($Version -notmatch '^v\d+\.\d+\.\d+$') { throw 'Use a version such as v1.0.0.' }
$asset = 'blacksquelch-windows-x86_64.zip'
$base = "$repo/releases/download/$Version"
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('blacksquelch-download-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    Invoke-WebRequest -UseBasicParsing -Uri "$base/checksums.sha256" -OutFile "$temporary/checksums.sha256"
    Invoke-WebRequest -UseBasicParsing -Uri "$base/$asset" -OutFile "$temporary/$asset"
    $hashes = @()
    foreach ($line in Get-Content -LiteralPath "$temporary/checksums.sha256") {
        if ($line -match ('^([a-f0-9]{64})  ' + [Regex]::Escape($asset) + '$')) { $hashes += $Matches[1] }
    }
    if ($hashes.Count -ne 1) { throw 'Missing or invalid checksum.' }
    $actual = (Get-FileHash -LiteralPath "$temporary/$asset" -Algorithm SHA256).Hash
    if ($actual -ine $hashes[0]) { throw 'Checksum mismatch; nothing was installed.' }
    Expand-Archive -LiteralPath "$temporary/$asset" -DestinationPath "$temporary/extracted"
    & "$temporary/extracted/blacksquelch/install.ps1" -InstallDir $InstallDir -NoPath:$NoPath
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
