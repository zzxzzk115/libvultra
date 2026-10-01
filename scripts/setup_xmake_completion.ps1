# Dot-source this file from PowerShell to register completion for the current session.
# Reuse the completion implementation shipped with the xmake found on PATH.
& {
    $ErrorActionPreference = 'Stop'
    if (-not (Get-Command Register-ArgumentCompleter).Parameters.ContainsKey('Native')) {
        throw 'This script requires PowerShell with native argument completion (PowerShell 7 recommended).'
    }
    $xmakeCommand = Get-Command xmake -ErrorAction Stop
    $xmakeProgramDir = & $xmakeCommand lua -c 'print(os.programdir())'
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not determine the xmake installation directory.'
    }

    $completionScript = Join-Path $xmakeProgramDir 'scripts/profile-win.ps1'
    if (-not (Test-Path -LiteralPath $completionScript -PathType Leaf)) {
        throw "Cannot read xmake completion script: $completionScript"
    }
    . $completionScript
}
