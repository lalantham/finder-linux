# bash completion for finder-linux
_finder_linux()
{
    local cur prev
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    case "${prev}" in
        -s|--select)
            if declare -F _filedir >/dev/null 2>&1; then
                _filedir
            else
                COMPREPLY=( $(compgen -f -- "${cur}") )
            fi
            return 0
            ;;
        --help|--version)
            return 0
            ;;
    esac

    if [[ "${cur}" == -* ]]; then
        COMPREPLY=( $(compgen -W "--select --new-window --help --version" -- "${cur}") )
        return 0
    fi

    if declare -F _filedir >/dev/null 2>&1; then
        _filedir
    else
        COMPREPLY=( $(compgen -f -- "${cur}") )
    fi
}

complete -o filenames -F _finder_linux finder-linux
