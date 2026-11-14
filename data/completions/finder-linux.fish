complete -c finder-linux -s s -l select -d 'Select a file or folder in its parent' -r -F
complete -c finder-linux -s n -l new-window -d 'Open a new window'
complete -c finder-linux -l help -d 'Show help'
complete -c finder-linux -l version -d 'Show version'
complete -c finder-linux -f -a '(__fish_complete_directories)'
