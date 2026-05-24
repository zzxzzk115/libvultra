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

function Get-VultraExecutableName {
    $os = [System.Runtime.InteropServices.RuntimeInformation]::OSDescription.ToLowerInvariant()
    if ($os.Contains('windows')) {
        return 'vultra.exe'
    }
    return 'vultra'
}

function Find-VultraExecutable {
    param([string]$RepoRootPath)

    if ($env:VULTRA -and (Test-Path -LiteralPath $env:VULTRA)) {
        return (Resolve-Path -LiteralPath $env:VULTRA).Path
    }

    $name = Get-VultraExecutableName
    $candidates = @(
        (Join-Path $RepoRootPath "build/install/bin/$name"),
        (Join-Path $RepoRootPath "build/install/vultra-app/bin/$name"),
        (Join-Path $RepoRootPath "build/windows/x64/release/vultra-app/$name"),
        (Join-Path $RepoRootPath "build/windows/x64/debug/vultra-app/$name")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "vultra executable not found. Build vultra-app first, or set VULTRA to the executable path."
}

$repoRootPath = (Resolve-Path -LiteralPath $RepoRoot).Path
$assetRootPath = if ([System.IO.Path]::IsPathRooted($AssetRoot)) { $AssetRoot } else { Join-Path $repoRootPath $AssetRoot }
$outVpkPath = if ([System.IO.Path]::IsPathRooted($OutVpk)) { $OutVpk } else { Join-Path $repoRootPath $OutVpk }
$vultra = Find-VultraExecutable $repoRootPath

& $vultra asset import $assetRootPath --reimport
if ($LASTEXITCODE -ne 0) {
    throw "vultra asset import failed"
}

& $vultra asset pack $assetRootPath $outVpkPath --zstd 6 @ExtraArgs
exit $LASTEXITCODE
