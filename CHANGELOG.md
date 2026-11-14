# Changelog

All notable changes to File Manager for Linux are documented here.

## [Unreleased]

### Search and Smart Folders
- Added real metadata, tag, comment, size, date, extension, and content search.
- Added structured search tokens such as `kind:`, `tag:`, `size:`, `modified:`, `ext:`,
  `name:`, `content:`, and `-exclude`.
- Added a **This PC** scope that searches local mounted filesystems with system-directory pruning.
- Added content search for text/source files and PDFs (via `pdftotext` when available).
- Added Smart Folders: save, edit, rename, delete, and open saved searches from the sidebar.
- Added active-filter summaries and a Save Search action to the search bar.
- `F5` re-runs an active search to pick up filesystem changes.

### Preview pane and richer Preview
- Added a docked preview pane for icon, list, and gallery views (`Ctrl+Shift+P`).
- Added preview pane metadata, tags, and Preview/Properties actions.
- Added full-screen Preview (`F11`).
- Images now open fitted to the preview window, with optional zoom, fit/reset, rotate, and Ctrl+wheel zoom.
- Added slideshow playback for multi-item previews (`S`).
- Added PDF and converted Office document page navigation.
- Preview pane visibility persists across sessions.

### Document previews
- Added on-demand Office document previews through LibreOffice headless conversion to cached PDFs.
- Supported formats include DOC/DOCX, XLS/XLSX, PPT/PPTX, ODT/ODS/ODP, RTF, and related extensions.
- Previews are asynchronous; the first conversion may take a few seconds and subsequent views use the cache.

### File operation controls
- Added explicit folder **Merge** behavior for copy and move, with Replace/KeepBoth/Skip/Cancel.
- Added separate **Apply to all folder conflicts** and **Apply to all file conflicts** handling.
- Added pause/resume for copy, move, duplicate, Trash, and delete operations.
- Added **Skip File** for the current file without cancelling the whole task.
- Added live speed, current-file display, elapsed time, and estimated time remaining.

### View options and list columns
- Added a per-folder **Show View Options** dialog (`Ctrl+J`) for icon size, text size,
  label position, grid spacing, item info, icon previews, and manual icon arrangement.
- Added **Use as Defaults**, **Clean Up**, and **Align to Grid**.
- Added metadata list columns: Date Created, Date Last Opened, Tags, Permissions,
  Owner, Group, and Comment, with a header menu to show/hide and reorder columns.
- Persisted list column visibility, order, and widths.
- Added locale-aware natural sorting (`file2` before `file10`) and sorting for the new columns.
- Added show/hide controls for the sidebar, toolbar, path bar, and status bar.
- Added `F11` full-screen mode.

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
- Added Preview video/audio playback via Qt Multimedia.
- Added filesystem metadata and checksum sections to Properties.
- Added Filelight and Plasma wallpaper integration.

### Packaging
- Added GPL-3.0-or-later license.
- Added AppStream metadata, man page, and Bash/Zsh/Fish completions.
- Added Arch Linux CI workflow.
- Added CMake presets and clang-format/clang-tidy configuration.
