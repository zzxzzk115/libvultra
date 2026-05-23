param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$AssetRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutVpk,

    [switch]$NoBootstrap,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = 'Stop'

function Get-VultraHostPlatform {
    $os = [System.Runtime.InteropServices.RuntimeInformation]::OSDescription.ToLowerInvariant()
    if ($os.Contains('windows')) {
        return 'windows'
    }
    if ($os.Contains('mac') -or $os.Contains('darwin') -or $os.Contains('os x')) {
        return 'macosx'
    }
    return 'linux'
}

function Get-VultraHostArch {
    switch ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()) {
        'x64' { return 'x64' }
        'arm64' { return 'arm64' }
        default { return $env:PROCESSOR_ARCHITECTURE.ToLowerInvariant() }
    }
}

$repoRootPath = (Resolve-Path -LiteralPath $RepoRoot).Path
$assetRootPath = if ([System.IO.Path]::IsPathRooted($AssetRoot)) { $AssetRoot } else { Join-Path $repoRootPath $AssetRoot }
$outVpkPath = if ([System.IO.Path]::IsPathRooted($OutVpk)) { $OutVpk } else { Join-Path $repoRootPath $OutVpk }

$platform = Get-VultraHostPlatform
$arch = Get-VultraHostArch
$installRoot = Join-Path $repoRootPath "build/.generated/vasset-host/$platform/$arch/release"

if (-not $NoBootstrap) {
    & (Join-Path $repoRootPath "scripts/bootstrap_vasset_cli.ps1") $repoRootPath
    if ($LASTEXITCODE -ne 0) {
        throw "bootstrap_vasset_cli.ps1 failed"
    }
}

$vassetName = if ($platform -eq 'windows') { 'vasset-cli.exe' } else { 'vasset-cli' }
$vassetCli = Join-Path $installRoot "bin/$vassetName"
if (-not (Test-Path -LiteralPath $vassetCli)) {
    if ($NoBootstrap) {
        throw "Installed vasset-cli not found: $vassetCli. Bootstrap once outside xmake: powershell -ExecutionPolicy Bypass -File `"$repoRootPath/scripts/bootstrap_vasset_cli.ps1`" `"$repoRootPath`""
    }
    throw "Installed vasset-cli not found: $vassetCli"
}

$oldPath = $env:PATH
try {
    $env:PATH = (Join-Path $installRoot 'bin') + ';' + (Join-Path $installRoot 'lib') + ';' + $oldPath
    & $vassetCli import $assetRootPath --reimport
    if ($LASTEXITCODE -ne 0) {
        throw "vasset-cli import failed"
    }
    & $vassetCli pack $assetRootPath $outVpkPath --zstd 6 @ExtraArgs
    exit $LASTEXITCODE
}
finally {
    $env:PATH = $oldPath
}
