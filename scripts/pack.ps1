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
    Push-Location $repoRootPath
    try {
        & xmake f -p $platform -a $arch -m release -y
        if ($LASTEXITCODE -ne 0) {
            throw "xmake configure failed"
        }

        & xmake install -y -o $installRoot vasset-cli
        if ($LASTEXITCODE -ne 0) {
            throw "xmake install vasset-cli failed"
        }
    }
    finally {
        Pop-Location
    }
}

$vassetName = if ($platform -eq 'windows') { 'vasset-cli.exe' } else { 'vasset-cli' }
$vassetCli = Join-Path $installRoot "bin/$vassetName"
if (-not (Test-Path -LiteralPath $vassetCli)) {
    if ($NoBootstrap) {
        throw "Installed vasset-cli not found: $vassetCli. Bootstrap once outside xmake: xmake f -p $platform -a $arch -m release -y && xmake install -y -o `"$installRoot`" vasset-cli"
    }
    throw "Installed vasset-cli not found: $vassetCli"
}

$oldPath = $env:PATH
try {
    $env:PATH = (Join-Path $installRoot 'bin') + ';' + (Join-Path $installRoot 'lib') + ';' + $oldPath
    & $vassetCli pack $assetRootPath $outVpkPath --zstd 6 @ExtraArgs
    exit $LASTEXITCODE
}
finally {
    $env:PATH = $oldPath
}
