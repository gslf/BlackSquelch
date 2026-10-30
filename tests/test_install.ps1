# Exercise a real Windows package, without changing user PATH or preferences.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Archive)
$ErrorActionPreference = 'Stop'
function Assert-True($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
foreach ($script in Get-ChildItem "$PSScriptRoot/../scripts/*.ps1") {
    $tokens = $null; $errors = $null
    [Management.Automation.Language.Parser]::ParseFile($script.FullName, [ref]$tokens, [ref]$errors) | Out-Null
    Assert-True ($errors.Count -eq 0) "PowerShell syntax error: $($script.Name): $errors"
}
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('blacksquelch-test-' + [Guid]::NewGuid().ToString('N'))
$oldPath = $env:Path
try {
    Expand-Archive -LiteralPath $Archive -DestinationPath "$temporary/package"
    $package = "$temporary/package/blacksquelch"
    $destination = "$temporary/install path with spaces"
    $info = ConvertFrom-StringData (Get-Content "$package/BUILD_INFO" -Raw)
    # No compiler/runtime directory on PATH: every non-system DLL must be bundled.
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    & "$package/install.ps1" -InstallDir $destination -NoPath
    $version = & "$destination/bin/blacksquelch.exe" --version
    Assert-True ($LASTEXITCODE -eq 0 -and $version -eq "BlackSquelch $($info.version)") 'Installed executable cannot run.'
    Set-Content "$destination/bin/unrelated.txt" 'keep'
    Set-Content "$destination/received-file.txt" 'keep'
    # An update removes old managed DLLs, while preserving unrelated files.
    Set-Content "$destination/bin/obsolete.dll" 'old'
    $manifest = Get-Content "$destination/install-manifest.json" -Raw | ConvertFrom-Json
    $manifest.files = @($manifest.files) + 'obsolete.dll'
    $manifest | ConvertTo-Json | Set-Content "$destination/install-manifest.json"
    & "$package/install.ps1" -InstallDir $destination -NoPath
    Assert-True (-not (Test-Path "$destination/bin/obsolete.dll")) 'Stale DLL survived update.'
    Assert-True (Test-Path "$destination/bin/unrelated.txt") 'Update removed an unrelated file.'
    # Force a failure after replacing bin; the previous install must be restored.
    Move-Item "$package/README.md" "$package/README.saved"
    $failed = $false
    try { & "$package/install.ps1" -InstallDir $destination -NoPath } catch { $failed = $true }
    Move-Item "$package/README.saved" "$package/README.md"
    Assert-True $failed 'Incomplete package was accepted.'
    $version = & "$destination/bin/blacksquelch.exe" --version
    Assert-True ($LASTEXITCODE -eq 0 -and $version -eq "BlackSquelch $($info.version)") 'Rollback did not restore executable.'
    Assert-True (Test-Path "$destination/README.md") 'Rollback lost metadata.'
    # A malicious manifest must fail before deleting any files.
    $savedManifest = Get-Content "$destination/install-manifest.json" -Raw
    '{"files":["blacksquelch.exe","../../received-file.txt"]}' | Set-Content "$destination/install-manifest.json"
    $failed = $false
    try { & "$destination/uninstall.ps1" -NoPath } catch { $failed = $true }
    Assert-True $failed 'Invalid uninstall manifest was accepted.'
    Assert-True (Test-Path "$destination/bin/blacksquelch.exe") 'Invalid manifest caused partial removal.'
    $savedManifest | Set-Content "$destination/install-manifest.json"
    & "$destination/uninstall.ps1" -NoPath
    Assert-True (-not (Test-Path "$destination/bin/blacksquelch.exe")) 'Executable was not removed.'
    Assert-True (Test-Path "$destination/bin/unrelated.txt") 'Uninstall removed an unrelated file.'
    Assert-True (Test-Path "$destination/received-file.txt") 'Uninstall removed a received file.'

    # Mock GitHub only; run the real downloader, hash check and installer.
    $downloads = "$temporary/downloads"
    New-Item -ItemType Directory $downloads | Out-Null
    $asset = 'blacksquelch-windows-x86_64.zip'
    Copy-Item -LiteralPath $Archive -Destination "$downloads/$asset"
    $hash = (Get-FileHash -LiteralPath "$downloads/$asset" -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $asset" | Set-Content "$downloads/checksums.sha256" -Encoding ASCII
    $expectedTag = "v$($info.version)"
    function Invoke-RestMethod($Uri, $Headers) { return @{ tag_name = $expectedTag } }
    function Invoke-WebRequest([switch]$UseBasicParsing, [string]$Uri, [string]$OutFile) {
        Assert-True ($Uri.Contains("/releases/download/$expectedTag/")) 'Downloader selected the wrong version.'
        Copy-Item -LiteralPath "$downloads/$($Uri.Split('/')[-1])" -Destination $OutFile
    }
    $bootstrapDestination = "$temporary/download install"
    $bootstrap = "$PSScriptRoot/../scripts/install-release.ps1"
    foreach ($selectedVersion in @('latest', $expectedTag)) {
        & $bootstrap -Version $selectedVersion -InstallDir $bootstrapDestination -NoPath
        Assert-True (Test-Path "$bootstrapDestination/bin/blacksquelch.exe") 'Download install failed.'
        & "$bootstrapDestination/uninstall.ps1" -NoPath
    }
    Add-Content "$downloads/$asset" 'tampered'
    $failed = $false
    try { & $bootstrap -InstallDir $bootstrapDestination -NoPath } catch {
        Assert-True ($_.Exception.Message -match 'Checksum mismatch') 'Wrong download failure.'
        $failed = $true
    }
    Assert-True $failed 'Corrupted download was accepted.'
    Assert-True (-not (Test-Path "$bootstrapDestination/bin/blacksquelch.exe")) 'Corrupted download changed installation.'
    Write-Host 'Install, update, rollback, runtime, uninstall and download checks passed.'
} finally {
    $env:Path = $oldPath
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
