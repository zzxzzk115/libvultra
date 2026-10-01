# Source this file from zsh, after any shell framework such as Oh My Zsh.
# Reuse the completion implementation shipped with the xmake found on PATH.
() {
    if (( ! $+commands[xmake] )); then
        print -u2 -- 'xmake must be available on PATH.'
        return 1
    fi

    local xmakeProgramDir
    xmakeProgramDir=$(xmake lua -c 'print(os.programdir())') || return 1
    local completionScript="$xmakeProgramDir/scripts/completions/register-completions.zsh"
    if [[ ! -r "$completionScript" ]]; then
        print -u2 -- "Cannot read xmake completion script: $completionScript"
        return 1
    fi

    if (( ! $+functions[compdef] )); then
        autoload -Uz compinit
        compinit || return 1
    fi
    source "$completionScript" || return 1
    compdef _xmake_zsh_complete xmake
    compdef _xrepo_zsh_complete xrepo
}
