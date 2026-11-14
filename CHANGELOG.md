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

### Search index support
- Added optional Baloo, Tracker 3, and Recoll backend detection.
- Added an **Index** search toggle that appears when a supported index is available.
- Indexed searches still apply File Manager's metadata, tag, size, date, and content filters, with a full-scan fallback on failure.

### Desktop-style window shell
- Added a frameless window with rounded outer corners and traffic-light close/minimize/maximize controls.
- Added toolbar drag-to-move, toolbar double-click maximize, and system edge resizing.
- Added a **Use native window title bar** preference for compositor-managed fallback.

### Custom tag management
- Added custom tag creation, color selection, rename, and delete.
- Added a **Manage Tags** dialog from the sidebar and toolbar tag menu.
- Custom tags participate in sidebar counts, file tags, Get Info, and tag-based search.
- Tag changes are written to `user.xdg.tags` and the persistent tag registry.

### Quick Look previews
- Added rendered Markdown and HTML previews.
- Added archive contents listing inside Quick Look.
- Added font specimen previews.
- Added PDF/Office in-document text search.
- Added RAW/HEIC previews through an ImageMagick fallback.
- Added Open With from Quick Look.

### File metadata
- Added persistent **Date Added** and **Date Last Opened** tracking.
- Added a **Metadata** tab to Get Info with version and comments editing.
- Added image dimensions/EXIF and audio/video duration, codec, and bitrate information.
- Added an editable extended-attribute table with add/edit/remove actions.
- Added a **Date Added** list column and sort order.

### Disk images and removable media
- Added ISO/IMG/RAW/SquashFS/UDF mounting through UDisks2 loop devices.
- Added optional DMG conversion and mounting via `dmg2img`.
- Added disk image mounting to double-click and the File menu.
- Added ISO creation from folders using `xorriso`, `genisoimage`, or `mkisofs`.
- Loop devices are now detached automatically after unmount/eject.

### Smart sidebar locations
- Added a **Recents** smart location backed by the freedesktop recent-files database.
- Added **Applications** and **Shared** sidebar locations.
- Added **Go → Recent Folders** and **Go → Recent Servers** menus.
- Added a **Servers** sidebar section with reconnect and remove actions.
- Recent folders and servers persist between sessions.

### Desktop-style UI polish
- Restyled the toolbar and sidebar with rounded surfaces and spacing.
- Combined back/forward controls and the breadcrumb path into one rounded path capsule.
- Replaced glyph-only toolbar controls with theme icons and fallbacks.
- Renamed sidebar sections to **Favorites** and **Locations**.
- Moved search behind a magnifier button; the search field opens on demand and still supports `Ctrl+F`.
- Moved secondary actions into the More menu and added compact Tag and View Options buttons.

### Multi-item Get Info and permissions
- Reorganized Get Info into **General**, **Permissions**, **Open With**, and **Checksums** tabs.
- Added aggregate Get Info for multiple selected items with combined size, kinds, locations, permissions, and tags.
- Multi-item permission, tag, ownership, and lock changes apply to all selected items.
- Added editable owner and group fields with recursive ownership support for folders.
- Added immutable/locked file support through filesystem flags.
- Added an ACL editor using `getfacl` / `setfacl`.
- Permissions can be applied recursively across multiple selected folders.

### Preview pane and richer Quick Look
- Added a docked preview pane for icon, list, and gallery views (`Ctrl+Shift+P`).
- Added preview pane metadata, tags, and Quick Look/Get Info actions.
- Added full-screen Quick Look (`F11`).
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
- Added Quick Look video/audio playback via Qt Multimedia.
- Added filesystem metadata and checksum sections to Get Info.
- Added Filelight and Plasma wallpaper integration.

### Packaging
- Added GPL-3.0-or-later license.
- Added AppStream metadata, man page, and Bash/Zsh/Fish completions.
- Added Arch Linux CI workflow.
- Added CMake presets and clang-format/clang-tidy configuration.
