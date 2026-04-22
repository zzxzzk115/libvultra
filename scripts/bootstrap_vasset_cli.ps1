param(
    [string]$RepoRoot = (Join-Path $PSScriptRoot "..")
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
$platform = Get-VultraHostPlatform
$arch = Get-VultraHostArch
$installRoot = Join-Path $repoRootPath "build/.generated/vasset-host/$platform/$arch/release"
$vassetProjectDir = Join-Path $repoRootPath "external/vasset"
$vassetCliPath = Join-Path $installRoot ("bin/" + $(if ($platform -eq 'windows') { 'vasset-cli.exe' } else { 'vasset-cli' }))

if (-not (Test-Path -LiteralPath $vassetProjectDir)) {
    throw "vasset project not found: $vassetProjectDir"
}

& xmake f -P $vassetProjectDir -p $platform -a $arch -m release --vasset_build_examples=n --vasset_build_tests=n -y
if ($LASTEXITCODE -ne 0) {
    throw "xmake configure failed"
}

& xmake build -P $vassetProjectDir -y vasset-cli
if ($LASTEXITCODE -ne 0) {
    throw "xmake build vasset-cli failed"
}

& xmake install -P $vassetProjectDir -y -o $installRoot vasset-cli
if ($LASTEXITCODE -ne 0) {
    throw "xmake install vasset-cli failed"
}

Write-Host "Installed host vasset-cli to: $vassetCliPath"
