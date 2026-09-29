param(
    [string]$ProjectDir = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) {
    throw 'xmake must be available on PATH.'
}

& xmake lua (Join-Path $PSScriptRoot 'setup_vscode.lua') $ProjectDir
exit $LASTEXITCODE
