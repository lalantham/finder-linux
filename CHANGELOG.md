# Changelog

All notable changes to Finder Linux are documented here.

## [1.0.0] - 2026-11-14

### Desktop integration
- Added `org.freedesktop.FileManager1` support for browser “Show in folder”.
- Added `org.freedesktop.Application` activation with `DBusActivatable=true`.
- Added Wayland activation-token handling and a KDE KWin fallback for window focus.
- Added KDE service-menu actions under an **Actions** context menu.
- Added XDG recent-file integration and **File → Open Recent**.
- Added UDisks2 D-Bus mount/unmount/eject with shell fallbacks.
- Added portal file-transfer clipboard support.

### Everyday features
- Added Copy To / Move To destination picker.
- Added New Folder with Selection.
- Added Select by Pattern and Invert Selection.
- Added Open in Terminal for files.
- Added multi-file Open With using `%F` / `%U`.
- Added tab tear-off into a new window.
- Added split-view synchronization and pane-to-pane copy/move.
- Added persistent per-folder Column View widths.

### File handling
- Copy/move preserves permissions, timestamps, xattrs, ACLs, and ownership where permitted.
- Added SHA-256 checksum calculation.
- Added ZIP AES password protection, compression levels, and Extract with Password.
- Added AppImage direct execution and architecture diagnostics.
- Added Extract AppImage.
- Added Trash auto-purge and restore-conflict handling.

### UI
- Added active-pane highlighting in split view.
- Added Quick Look video/audio playback via Qt Multimedia.
- Added filesystem metadata and checksum sections to Get Info.
- Added Filelight and Plasma wallpaper integration.

### Packaging
- Added GPL-3.0-or-later license.
- Added AppStream metadata, man page, and Bash/Zsh/Fish completions.
- Added Arch Linux CI workflow.
- Added CMake presets and clang-format/clang-tidy configuration.
