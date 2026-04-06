param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$AssetRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutVpk,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = 'Stop'

function Get-VultraHostPlatform {
    $platformId = [System.Runtime.InteropServices.RuntimeInformation]::OSDescription.ToLowerInvariant()
    if ($platformId.Contains('windows')) {
        return 'windows'
    }
    if ($platformId.Contains('mac') -or $platformId.Contains('darwin') -or $platformId.Contains('os x')) {
        return 'macos'
    }
    return 'linux'
}

function Get-VultraHostArch {
    switch ($env:PROCESSOR_ARCHITECTURE.ToLowerInvariant()) {
        'amd64' { return 'x64' }
        'x86_64' { return 'x64' }
        'arm64' { return 'arm64' }
        default { return $env:PROCESSOR_ARCHITECTURE.ToLowerInvariant() }
    }
}

$platform = Get-VultraHostPlatform
$arch = Get-VultraHostArch
$vassetName = if ($platform -eq 'windows') { 'vasset-cli.exe' } else { 'vasset-cli' }
$vassetCli = Join-Path $RepoRoot "prebuilt/$platform/$arch/$vassetName"
if (-not (Test-Path $vassetCli)) {
    throw "Missing prebuilt vasset-cli: $vassetCli"
}

& $vassetCli pack $AssetRoot $OutVpk --zstd 6 @ExtraArgs
exit $LASTEXITCODE
