param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$AssetRoot,

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

$vshadercPath = $env:VSHADERC
if (-not $vshadercPath) {
    $xmakePackageRoot = Join-Path $env:LOCALAPPDATA ".xmake/packages/v/vshadersystem"
    if (Test-Path -LiteralPath $xmakePackageRoot) {
        $vshadercName = if ($platform -eq 'windows') { 'vshaderc.exe' } else { 'vshaderc' }
        $candidate = Get-ChildItem -Path $xmakePackageRoot -Recurse -Filter $vshadercName -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            $vshadercPath = $candidate.FullName
        }
    }
}

$oldPath = $env:PATH
try {
    $shaderToolDir = if ($vshadercPath) { [System.IO.Path]::GetDirectoryName($vshadercPath) } else { "" }
    $env:PATH = (Join-Path $installRoot 'bin') + ';' + (Join-Path $installRoot 'lib') + ';' + $shaderToolDir + ';' + $oldPath
    & $vassetCli import $assetRootPath @ExtraArgs
    exit $LASTEXITCODE
}
finally {
    $env:PATH = $oldPath
}
