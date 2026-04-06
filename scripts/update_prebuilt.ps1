param(
    [string]$TargetName = 'vasset-cli',
    [string]$Platform,
    [string]$Arch,
    [string]$Mode = 'release'
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

function Get-VultraHostPlatform {
    $platformId = [System.Runtime.InteropServices.RuntimeInformation]::OSDescription.ToLowerInvariant()
    if ($platformId.Contains('windows')) {
        return 'windows'
    }
    if ($platformId.Contains('mac') -or $platformId.Contains('darwin') -or $platformId.Contains('os x')) {
        return 'macosx'
    }
    return 'linux'
}

function Get-VultraHostArch {
    $archValue = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
    switch ($archValue) {
        'x64' { return 'x64' }
        'arm64' { return 'arm64' }
        default { return $archValue }
    }
}

if ([string]::IsNullOrWhiteSpace($Platform)) {
    $Platform = Get-VultraHostPlatform
}

if ([string]::IsNullOrWhiteSpace($Arch)) {
    $Arch = Get-VultraHostArch
}

$BinName = if ($Platform -eq 'windows') { "$TargetName.exe" } else { $TargetName }
$SourceBin = Join-Path $RepoRoot "build/$Platform/$Arch/$Mode/$TargetName/$BinName"
$DestDir = Join-Path $RepoRoot "prebuilt/$Platform/$Arch"
$DestRaw = if ($Platform -eq 'windows') {
    Join-Path $DestDir "$TargetName-raw.exe"
} else {
    Join-Path $DestDir "$TargetName-raw"
}

Write-Host "Configuring xmake for $Platform/$Arch ..."
& xmake f -p $Platform -a $Arch -m $Mode
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Building $TargetName ($Mode) ..."
& xmake build $TargetName
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not (Test-Path $SourceBin)) {
    throw "Built binary not found: $SourceBin"
}

New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
Copy-Item -Force $SourceBin $DestRaw

Write-Host "Updated prebuilt binary:"
Write-Host "  source: $SourceBin"
Write-Host "  dest:   $DestRaw"
