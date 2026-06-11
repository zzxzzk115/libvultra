<#
.SYNOPSIS
    Reclaim Runtime MCP smoke-test scratch under build/.tmp.

.DESCRIPTION
    Runtime MCP smoke tests create a fresh timestamped scratch directory on every run
    (build/.tmp/material-mcp-smoke-*, material-mcp-step-*, material-mcp-smoke-output-*,
    material-shader-frame-textures, ...). build/ is gitignored so these are never
    committed, but they accumulate unbounded on local disk. This script removes that
    scratch. It only ever touches build/.tmp, never tracked content.

.PARAMETER All
    Remove the whole build/.tmp directory instead of only the known smoke prefixes.

.PARAMETER DryRun
    List what would be removed (and how much it would reclaim) without deleting.

.EXAMPLE
    tools/clean-mcp-tmp.ps1
    tools/clean-mcp-tmp.ps1 -DryRun
    tools/clean-mcp-tmp.ps1 -All
#>
[CmdletBinding()]
param(
    [switch]$All,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# Repo root = parent of this script's tools/ directory.
$repoRoot = Split-Path -Parent $PSScriptRoot
$tmpRoot  = Join-Path $repoRoot 'build/.tmp'

if (-not (Test-Path -LiteralPath $tmpRoot)) {
    Write-Host "Nothing to clean: $tmpRoot does not exist."
    exit 0
}

# Known MCP smoke scratch prefixes. Extend here if new smoke flows add prefixes.
$smokePrefixes = @(
    'material-mcp-smoke',
    'material-mcp-step',
    'material-mcp-smoke-output',
    'material-shader-frame-textures',
    'streamline-diagnostics-smoke',
    'mcp_projects'
)

if ($All) {
    $targets = Get-ChildItem -LiteralPath $tmpRoot -Force -ErrorAction SilentlyContinue
} else {
    $targets = Get-ChildItem -LiteralPath $tmpRoot -Force -ErrorAction SilentlyContinue |
        Where-Object {
            $name = $_.Name
            $smokePrefixes | Where-Object { $name -like "$_*" }
        }
}

if (-not $targets) {
    Write-Host "Nothing to clean under $tmpRoot."
    exit 0
}

$totalBytes = 0
foreach ($t in $targets) {
    $size = (Get-ChildItem -LiteralPath $t.FullName -Recurse -Force -File -ErrorAction SilentlyContinue |
        Measure-Object -Property Length -Sum).Sum
    if ($null -eq $size) { $size = 0 }
    $totalBytes += $size
    $mb = [math]::Round($size / 1MB, 2)
    if ($DryRun) {
        Write-Host "would remove $($t.Name) ($mb MB)"
    } else {
        Write-Host "removing $($t.Name) ($mb MB)"
        Remove-Item -LiteralPath $t.FullName -Recurse -Force
    }
}

$totalMb = [math]::Round($totalBytes / 1MB, 2)
if ($DryRun) {
    Write-Host "Dry run: would reclaim $totalMb MB from $tmpRoot."
} else {
    Write-Host "Reclaimed $totalMb MB from $tmpRoot."
}
