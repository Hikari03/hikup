# bash completion for hikup                               -*- shell-script -*-

_hikup_get_servers() {
    local servers=""
    if [[ -f /usr/share/hikup ]]; then
        servers=$(sort -u /usr/share/hikup 2>/dev/null)
    elif [[ -d /usr/share/hikup ]]; then
        servers=$(sort -u /usr/share/hikup/* /usr/share/hikup/servers 2>/dev/null)
    fi
    echo "$servers"
}

_hikup() {
    local cur prev words cword
    if declare -F _init_completion >/dev/null 2>&1; then
        _init_completion -n : || return
    else
        COMPREPLY=()
        cur="${COMP_WORDS[COMP_CWORD]}"
        prev="${COMP_WORDS[COMP_CWORD-1]}"
        words=("${COMP_WORDS[@]}")
        cword=$COMP_CWORD
    fi

    local commands=(
        up qup vup qvup vqup
        down qdown vdown qvdown vqdown
        rm qrm vrm qvrm vqrm
        ls qls vls qvls vqls
    )

    # First argument: command
    if [[ $cword -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${commands[*]}" -- "$cur") )
        return 0
    fi

    local cmd="${words[1]}"

    case "$cmd" in
        *up*)
            if [[ $cword -eq 2 ]]; then
                if [[ "$cur" == -* ]]; then
                    COMPREPLY=( $(compgen -W "-" -- "$cur") )
                else
                    if declare -F _filedir >/dev/null 2>&1; then
                        _filedir
                    else
                        compopt -o default 2>/dev/null || compopt -o filenames 2>/dev/null
                        COMPREPLY=( $(compgen -f -- "$cur") )
                    fi
                fi
                return 0
            elif [[ $cword -eq 3 ]]; then
                local servers
                servers=$(_hikup_get_servers)
                COMPREPLY=( $(compgen -W "$servers" -- "$cur") )
                [[ "$cur" == *:* ]] && declare -F __ltrim_colon_completions >/dev/null 2>&1 && __ltrim_colon_completions "$cur"
                return 0
            fi
            ;;
        *down*|*rm*)
            if [[ $cword -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "-" -- "$cur") )
                return 0
            elif [[ $cword -eq 3 ]]; then
                local servers
                servers=$(_hikup_get_servers)
                COMPREPLY=( $(compgen -W "$servers" -- "$cur") )
                [[ "$cur" == *:* ]] && declare -F __ltrim_colon_completions >/dev/null 2>&1 && __ltrim_colon_completions "$cur"
                return 0
            fi
            ;;
        *ls*)
            if [[ $cword -eq 2 || $cword -eq 3 ]]; then
                return 0
            elif [[ $cword -eq 4 ]]; then
                local servers
                servers=$(_hikup_get_servers)
                COMPREPLY=( $(compgen -W "$servers" -- "$cur") )
                [[ "$cur" == *:* ]] && declare -F __ltrim_colon_completions >/dev/null 2>&1 && __ltrim_colon_completions "$cur"
                return 0
            fi
            ;;
    esac
}

complete -F _hikup hikup
