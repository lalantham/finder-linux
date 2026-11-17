# Changelog

All notable changes to File Manager for Linux are documented here.

## [Unreleased]

## [1.1.4] - 2026-11-17

### Icon theme fix (custom themes)
- **Your configured icon theme is now used instead of a guessed default.** Qt only learns the
  icon theme from the platform-theme plugin; inside an AppImage that plugin is missing, so Qt
  falls back to a hard-coded guess of `breeze` on KDE hosts. That guess was trusted whenever the
  theme happened to be installed, so the AppImage showed Breeze icons even though `kdeglobals`
  specified a custom theme such as Tela. An explicitly configured theme now always takes
  precedence; Qt's guess is only used when nothing is configured.
- The lookup reads KDE (`kdeglobals`, home and `/etc/xdg`), GTK 4, GTK 3 and LXQt, preferring the
  newer GTK 4 value, and ignores a configured theme that is not actually installed.
- The precedence logic moved into `IconTheme` so it is unit-tested; a regression test pins
  "configured theme beats Qt's guess".

## [1.1.3] - 2026-11-17

### Fedora and distribution packaging
- Added a **native RPM package** for Fedora and the RHEL family, built on a Fedora container so
  its library sonames match the target. Install with
  `sudo dnf install ./finder-linux-<version>-1.x86_64.rpm`.
- **The Debian package now recommends the helper tools** that unlock features: `udisks2` for
  mounting and ejecting media, `libglib2.0-bin` for Trash and remote mounts, `xdg-utils`, `acl`
  for the permissions editor, and `poppler-utils` for searching inside PDFs. Previously the package
  declared no external tool dependencies at all, so a fresh install silently lacked those features
  with nothing to indicate why. They are Recommends rather than Depends, so a minimal system still
  installs and runs.
- CI now builds and tests on **Debian 12, Ubuntu 24.04, Fedora 41 and Arch Linux**, and asserts
  that each package installs and that its payload does not contain the shared
  `org.freedesktop.FileManager1` D-Bus name.

### Documentation
- README documents per-distribution install commands, what each optional helper tool enables, and
  a supported-platform table. Released binaries target glibc 2.34, covering Debian 12,
  Ubuntu 22.04+ and Fedora 35+.

## [1.1.2] - 2026-11-17

### Package install fix
- **The Debian package now installs alongside nautilus, dolphin and elementary Files.**
  `org.freedesktop.FileManager1` is a shared freedesktop name that those packages already
  provide, so shipping `/usr/share/dbus-1/services/org.freedesktop.FileManager1.service`
  aborted the install with "trying to overwrite ... which is also in package nautilus".
  The name is no longer installed system-wide: the user-scoped activation file that
  **Set as default file manager** writes to `~/.local/share/dbus-1/services` already takes
  precedence over the distribution's, per dbus-daemon(1).
- `uninstall.sh` no longer deletes the system-wide FileManager1 file unless it points at
  Finder, so uninstalling cannot break "Show in folder" for another file manager.

## [1.1.1] - 2026-11-17

### Icon theme fix
- **The AppImage now uses your desktop's icon theme.** It previously rendered every icon from
  Qt's built-in fallback set, so it looked like a different, poorer icon pack than the same binary
  installed system-wide. Two causes: the AppImage bundled no `iconengines` plugin, and almost every
  Linux icon theme ships SVG icons, so without `libqsvgicon.so` not one of them could be drawn; and
  Qt only seeds its icon search paths from the platform theme, which an AppImage cannot load, so
  the paths collapsed to Qt's internal resources. The host icon directories are now added
  explicitly and the SVG icon engine is bundled.

## [1.1.0] - 2026-11-17

### Reliability fixes
- **Saved sort order now survives restarts.** Attaching the model to the list view sorted through
  a default header indicator, which discarded the restored order; the header arrow now also
  follows sorting done from the menu or a header click.
- **Drag and drop into a folder whose name shares a prefix with the dragged item now works.**
  Dropping `report` onto `report-2026` was treated as dropping a folder into itself and silently
  discarded.
- **Mounting a disk image, creating an ISO, emptying the Trash and registering as the default file
  manager no longer freeze the window**; each could block for up to several minutes.
- **The terminal panel no longer freezes on chatty commands.** Output is parsed in a single pass
  instead of re-laying out an HTML document per append (2000 lines: 7.1 s → 54 ms), scrollback is
  capped, and `Ctrl+C` now signals the whole process group instead of killing only the shell and
  leaving children running. The panel also follows the light/dark theme.
- **Thumbnail failures are retried** instead of blacklisting a path for the session, and changing
  the icon size no longer flushes Qt's global pixmap cache.

### Performance
- **Copying is dramatically faster.** Files are cloned with `FICLONE` or copied with
  `copy_file_range` before falling back to a buffered loop, and the per-file `fsync` is now a
  preference. Copying 386 MB across 502 files: 691 ms → 16 ms. Sparse files are preserved instead
  of being materialised (a 1 GiB file with 8 KiB of data stayed at 8 KiB).
- **PDF pages render asynchronously** and the open document is cached, so page turns no longer
  block the window (271 ms → 0 ms) and each page no longer re-parses the file.
- **Metadata saves are linear rather than quadratic** (504 ms → 30 ms at 42k entries), and stale
  entries are pruned at startup so the store no longer grows without bound.
- Release builds use link-time optimization (~10% faster on model, sort and classify paths).

### Per-folder view modes
- Added a **global default view mode** with **per-folder overrides**. A folder without an override
  now falls back to the default instead of inheriting the mode of the folder you just left.
- Overrides apply to the exact folder only; subfolders use the default unless set explicitly.
- Added **View → Remember This Folder's View** (`Ctrl+Shift+D`): uncheck it to forget the folder's
  view and return to the default.
- Added **View → Use This View as Default for All Folders**.
- Added **Preferences → “View buttons change:”** choosing whether `Ctrl+1-4`, the view pills and
  View → as … apply to *only the current folder* (default) or to *all folders*.
- Per-folder view modes are now enabled by default; existing overrides are preserved and migrated.
- Overrides are capped (least-recently-used first) so settings cannot grow without bound.

### Diagnostics
- Added **Help → About File Manager** showing the version, Qt version, build timestamp and
  executable path, so an out-of-date install is immediately identifiable.

### Internal
- The file-kind extension tables shared by the instant filter and the search engine now live in one
  place, with a parity test so the two cannot drift apart.

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

### AppImage Wayland fix
- Bundled Qt's `wayland-graphics-integration-client` plugins in the AppImage.
- Fixes `Failed to load client buffer integration: "wayland-egl"` on KDE Wayland.
- AppImages now select Qt's FFmpeg media backend, avoiding missing host GStreamer elements.

### Release packaging
- Added Debian package generation via CPack.
- Added AppImage and relocatable tarball packaging scripts.
- Added a tag-triggered GitHub Release workflow with SHA-256 checksums.
- Added Qt 6.4 compatibility guards for system color-scheme detection.
- Release builds target Debian 12 / Ubuntu 23.04+ (glibc 2.36 baseline).

### Search polish
- Added removable search token chips for kind, tag, extension, size, date, content, and exclusions.
- Added live recursive search refresh (opt-out) without clobbering results on folder changes.
- Added relevance/name/date/size result sorting and a "Search in results" refinement field.
- Remembered search scope, subfolder recursion, live-update preference, and sort mode.
- Preserved selection across live refreshes and result refinements.
- Added regression tests for chip parsing, relevance sorting, refinement, and directory-change safety.

### Performance polish
- Moved thumbnail disk-cache probing and PNG metadata parsing off the UI thread; huge PNGs are no longer read in full.
- Coalesced thumbnail repaints, skipped hidden views, and cached scaled thumbnails.
- Split thumbnail, Office-conversion, and folder-size work into bounded thread pools.
- Folder-size walks survive unrelated directory refreshes, share in-flight work across Column View, and invalidate on directory changes.
- Search uses cached tags first, shortens TagManager lock scope, and budgets cold xattr probes.
- Debounced AppSettings writes and flush pending settings on exit.
- Async Quick Look image/PDF rendering and gallery/column preview thumbnails.
- Debounced preview pane selection updates and moved thumbnail generation off the UI thread.
- Moved Get Info media metadata probing to a background task.
- Render PDF thumbnails at target-appropriate DPI instead of fixed 150 DPI.
- Reduced Quick Look font leaks and duplicate fit operations.
- Copy now uses 1 MB chunks with 100 ms progress throttling.
- Same-filesystem moves skip recursive pre-walks.
- Search stop no longer blocks the UI on slow scans or conversion processes.
- Coalesced sidebar refreshes after tag and volume changes.

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
- Added a per-item progress table and **Cancel Item** action for pending operations.
- Added pause/resume support for compression and extraction.
- Added a completion summary with processed, failed, skipped, and cancelled counts.
- Added per-item status tracking for copy, move, duplicate, trash, delete, compress, and extract.
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

## [1.0.1] - 2026-11-15

Released with these changes recorded under "Unreleased" at the time:

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
- Indexed searches still apply File Manager's metadata, tag, size, date, and content filters, with a
  full-scan fallback on failure.

### AppImage Wayland fix
- Bundled Qt's `wayland-graphics-integration-client` plugins in the AppImage.
- Fixes `Failed to load client buffer integration: "wayland-egl"` on KDE Wayland.
- AppImages now select Qt's FFmpeg media backend, avoiding missing host GStreamer elements.

### Release packaging
- Added Debian package generation via CPack.
- Added AppImage and relocatable tarball packaging scripts.
- Added a tag-triggered GitHub Release workflow with SHA-256 checksums.
- Added Qt 6.4 compatibility guards for system color-scheme detection.
- Release builds target Debian 12 / Ubuntu 23.04+ (glibc 2.36 baseline).

### Search polish
- Added removable search token chips for kind, tag, extension, size, date, content, and exclusions.
- Added live recursive search refresh (opt-out) without clobbering results on folder changes.
- Added relevance/name/date/size result sorting and a "Search in results" refinement field.
- Remembered search scope, subfolder recursion, live-update preference, and sort mode.
- Preserved selection across live refreshes and result refinements.
- Added regression tests for chip parsing, relevance sorting, refinement, and directory-change safety.

### Performance polish
- Moved thumbnail disk-cache probing and PNG metadata parsing off the UI thread.
- Coalesced thumbnail repaints, skipped hidden views, and cached scaled thumbnails.
- Split thumbnail, Office-conversion, and folder-size work into bounded thread pools.
- Folder-size walks survive unrelated directory refreshes and share in-flight work across Column View.
- Search uses cached tags first, shortens TagManager lock scope, and budgets cold xattr probes.
- Debounced AppSettings writes and flush pending settings on exit.
- Async Quick Look image/PDF rendering and gallery/column preview thumbnails.
- Moved Get Info media metadata probing to a background task.
- Render PDF thumbnails at target-appropriate DPI instead of fixed 150 DPI.
- Copy now uses 1 MB chunks with 100 ms progress throttling.
- Same-filesystem moves skip recursive pre-walks.
- Coalesced sidebar refreshes after tag and volume changes.

### Custom tag management
- Added custom tag creation, color selection, rename, and delete.

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
