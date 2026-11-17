# File Manager for Linux

[![CI](https://github.com/lalantham/finder-linux/actions/workflows/ci.yml/badge.svg)](https://github.com/lalantham/finder-linux/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

A premium, native Linux file manager built with modern **C++20** and **Qt 6 Widgets**.

Designed as a native desktop application with genuine filesystem operations, responsive non-blocking background workers, robust error handling, high visual fidelity to a classic desktop file-manager interface, and freedesktop.org standards compliance.

---

## 🌟 Key Features

### 1. The Four Browsing Views
* **Icon View (`Ctrl+1`)**:
  - Classic icon-grid layout with smooth wrapping and clean item spacing.
  - Multi-line text labels with middle-elision and selection highlight pills.
  - Configurable icon zoom levels and real-time layout adjustment.
* **List View (`Ctrl+2`)**:
  - Detailed hierarchical list with Name, Date Modified, Size, and Kind by default.
  - Optional metadata columns: Date Created, Date Last Opened, Tags, Permissions, Owner, Group, and Comment.
  - Header context menu to show/hide columns, reorder them, or reset to defaults.
  - Column visibility, order, and widths are persisted across sessions.
  - Natural, locale-aware sorting (`file2` sorts before `file10`) plus sort-by-date, size, kind, tags, permissions, owner, group, and comment.
  - Click-to-sort columns with sort direction indicators and directories-first grouping.
  - Interactive column resizing and reordering.
* **Column View (`Ctrl+3`)**:
  - Authentic implementation of **Miller Columns**.
  - Clicking a folder dynamically opens the next column to the right.
  - Selecting a file displays a live preview pane with file attributes and thumbnail.
  - Drag a column's right-edge divider to resize it; widths are remembered per folder.
  - Double-click a column divider to auto-fit the column to its contents.
  - Smooth auto-scrolling to the active column and keyboard-driven traversal (Left/Right/Up/Down).
* **Gallery View (`Ctrl+4`)**:
  - Large central hero preview viewport.
  - Horizontal filmstrip carousel at the bottom with real-time selection synchronization.
  - Displays instant image previews, text documents, or rendered PDF pages.

### View Options & Layout
* **Show View Options (`Ctrl+J`)** with per-folder settings:
  - Icon size, text size, label position (below or beside the icon), and grid spacing.
  - Show item info (size and item count) and icon preview thumbnails.
  - Manual icon arrangement with **Clean Up** and **Align to Grid**.
  - “Use as Defaults” applies the current options to folders without their own overrides.
* **View chrome controls**: show/hide the sidebar, toolbar, path bar, status bar, and preview pane; all states persist.
* **Preview Pane (`Ctrl+Shift+P`)**: docked preview for icon, list, and gallery views with image, text, PDF, Office, video, and audio metadata.
* **Full-screen mode (`F11`)**.

### Search & Smart Folders
* **Real search scopes**: current folder, current folder with subfolders, or **This PC**.
* **Filename and metadata search** across names, tags, and file comments.
* **Content search** (`Contents` checkbox or `content:term`) for text/source files and PDFs
  (PDF text extraction uses `pdftotext` when available).
* **Search tokens**:
  - `kind:image`, `kind:document`, `kind:video`, `kind:audio`, `kind:folder`, `kind:archive`
  - `tag:Red,Work`
  - `ext:pdf,md`
  - `size:>10MB`, `size:1MB..10MB`
  - `modified:>2025-01-01`, `modified:2025-01-01..2025-12-31`
  - `name:report`, `content:"annual report"`, and `-draft` to exclude
* **Live search updates**: recursive searches refresh when the current folder changes;
  This PC searches stay manual to avoid whole-disk rescans. Toggle with **Live**.
* **Search chips**: structured filters appear as removable chips above the results.
* **Result controls**: sort by relevance, name, date, or size, and use **Search in results…**
  to refine without rescanning. Search scope and sort mode are remembered.
* **Smart Folders**: save any search from **Save Search...** or **File → New Smart Folder...**.
  Saved searches appear in the sidebar and can be opened, edited, renamed, or deleted.
* Active filters are summarized below the search controls; `F5` re-runs the current search.
* Optional **Index** toggle uses Baloo, Tracker 3, or Recoll when installed, with automatic fallback to the filesystem walk.

### Smart Locations
* **Recents** — recent documents and folders from the freedesktop recent-files database.
* **Applications** — installed desktop applications as a browsable folder.
* **Shared** — mounted GVFS network locations when available.
* **Recent Folders** and **Recent Servers** menus under **Go**.
* **Servers** sidebar section for recently used network connections.

### 2. Quick Look (`Space`)
* Dedicated floating HUD overlay.
* Instant inspection of images, source code, text files, and multi-page PDFs (via `Poppler-Qt6`).
* Office documents (`.docx`, `.xlsx`, `.pptx`, `.odt`, `.ods`, `.odp`, `.rtf`, and related formats) are converted on demand through LibreOffice and rendered from a cached PDF. The first preview can take a few seconds; later previews are fast.
* Video playback and audio playback with play/pause, seek slider, and elapsed/total time (Qt Multimedia, when available).
* Previous (`◀`) and Next (`▶`) navigation arrows to flip through selected items.
* PDF/Office page navigation with page count and in-document text search.
* Rendered Markdown and HTML previews.
* Archive contents listing for ZIP/TAR and other supported formats.
* Font specimen previews.
* RAW/HEIC previews when ImageMagick is installed.
* Open With directly from Quick Look.
* Images open fitted to the preview window; zoom (`+` / `-`), fit/reset (`0`), rotate (`R`), and Ctrl+wheel zoom are available when needed.
* Slideshow toggle (`S`) for multi-item previews.
* Full-screen mode (`F11`).
* Dismissible instantly with `Space` or `Esc`.

### 3. File Operations & Data Integrity Engine
* **Asynchronous Execution**: Copy, move, trash, duplicate, and delete operations execute on dedicated worker threads without blocking the GUI.
* **Safe Chunked Streams**: Operations stream data in 1 MB buffers, allowing instant cancellation and real-time progress calculations (speed, transferred bytes, item counts).
* **Temporary File Strategy**: Data is written to hidden temporary files first and then atomically renamed into place upon completion to prevent corrupted partial files.
* **Cross-Filesystem Moves**: When moving across distinct mount points, atomic renames fall back to verified recursive copying and source cleanup. Sources are never deleted if destination transfers fail.
* **Conflict Resolution**: When a destination collision occurs, users are prompted with an interactive conflict dialog:
  - *Merge* for folders (keeps existing contents and merges the incoming tree)
  - *Replace*
  - *Keep Both* (auto-renames, e.g. `document copy.txt`)
  - *Skip*
  - *Cancel*
  - Separate *Apply to all folder conflicts* and *Apply to all file conflicts* options
* **Operation Controls**:
  - Per-item progress table with **Cancel Item** for pending items
  - Pause / Resume long-running copy, move, duplicate, Trash, delete, compress, and extract operations
  - Skip the current file without stopping the whole task
  - Live speed, elapsed progress, current file, and estimated time remaining
  - Queue-aware *Skip Task* and *Stop All* controls
  - Completion summary with processed, failed, skipped, and cancelled counts
* **Undo & Redo (`Ctrl+Z` / `Ctrl+Shift+Z`)**: Reversible operations (Folder creation, Renaming, Moving, and Trashing) are tracked on an bounded undo stack.

### 4. Freedesktop.org XDG Trash Specification
* Moves items to `$XDG_DATA_HOME/Trash/files` and records metadata in `$XDG_DATA_HOME/Trash/info/*.trashinfo`.
* Full restoration support back to the original filesystem path.
* Restore conflicts are handled safely by keeping both items and restoring the trashed copy with a "(restored)" name.
* Optional automatic purge of items older than a configurable number of days.
* Confirmation dialog before permanent deletion or emptying the trash.

### 5. Native UI Design & Theming
* Rounded, desktop-style toolbar and sidebar surfaces.
* Back/forward controls and the breadcrumb path share one rounded path capsule.
* Theme icon toolbar buttons for navigation, views, tag actions, view options, more actions, and search.
* Secondary actions such as New Folder, Quick Look, Get Info, hidden files, and terminal live in the More menu to keep the toolbar compact.
* A compact **search icon** that opens the search field only when needed.
* Segmented pill buttons for navigation and the four view modes.
* Interactive Breadcrumb path bar with direct segment navigation and `Ctrl+L` editable path mode.
* Rounded sidebar with **Recents**, **Shared**, **Favorites** (Applications, Home, Desktop, Documents, Downloads, custom pinned folders), **Locations** (Mounted drives, Root drive, Trash), **Servers**, Smart Folders, and Tags.
* Dynamic **Light** and **Dark** themes following modern desktop design conventions.

### 6. Desktop Integration & Tabs
* Tabbed browsing (`Ctrl+T` new tab, `Ctrl+W` close tab) with independent navigation history.
* Implements the freedesktop.org `org.freedesktop.FileManager1` D-Bus interface, so **Show in folder** / **Open containing folder** in browsers (Brave, Firefox, Chromium) and other applications opens File Manager instead of another installed file manager.
* **Show in folder** requests bring File Manager to the front using the Wayland xdg-activation protocol; on KDE Plasma Wayland a KWin fallback handles focus-stealing prevention when needed.
* Implements `org.freedesktop.Application` with `DBusActivatable=true`, so launchers can use the standard `Activate`, `Open`, and `ActivateAction` methods instead of spawning duplicate processes.
* Installing File Manager registers user-scoped FileManager1/Application D-Bus activation files that take precedence over Dolphin/elementary Files; `./install.sh --default` also makes it the desktop-wide handler for `inode/directory`.
* Integrates with the freedesktop `recently-used.xbel` database: files opened through File Manager are recorded and shown under **File → Open Recent**.
* **KDE Service Menu actions** (Dolphin-compatible) appear under an **Actions** submenu, including Filelight, Install Font, Run in Konsole, KDE Connect, and custom scripts from `kio/servicemenus` / `kservices5/ServiceMenus`.
* Desktop notifications identify the application via the standard `desktop-entry` hint, and the **New Window** launcher action uses `--new-window`.
* Native application launching with default Linux associations (`xdg-open` / `QDesktopServices`).
* Open in Terminal shortcut (`Ctrl+Alt+T`) supporting popular Linux terminal emulators (`ptyxis`, `gnome-terminal`, `konsole`, `kitty`, `alacritty`, `xfce4-terminal`).

---

## 🛠 Prerequisites & Dependencies

- **Compiler**: GCC 11+ or Clang 14+ supporting C++20.
- **Build System**: CMake 3.22+ and Ninja.
- **Qt Framework**: Qt 6.4+ (`QtCore`, `QtGui`, `QtWidgets`, `QtConcurrent`, `QtSvg`, `QtTest`).
- **Required**: `libarchive`.
- **Optional**: `poppler-qt6` (PDF preview), `qt6-multimedia` (Quick Look video/audio), `libreoffice` (Office document previews), and `kwindowsystem` (KF6, reliable window activation on KDE Plasma Wayland).

### Installing Dependencies on Arch / Manjaro / CachyOS:
```bash
sudo pacman -S cmake ninja qt6-base qt6-svg qt6-multimedia libarchive poppler-qt6 kwindowsystem
```

### Installing Dependencies on Ubuntu / Debian:
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-svg-dev qt6-multimedia-dev \
    libarchive-dev libpoppler-qt6-dev
```

### Installing Dependencies on Fedora:
```bash
sudo dnf install gcc-c++ cmake ninja-build \
    qt6-qtbase-devel qt6-qtsvg-devel qt6-qtmultimedia-devel \
    libarchive-devel poppler-qt6-devel
```

---

## 🚀 Building & Running

### 0. Clone
```bash
git clone https://github.com/lalantham/finder-linux.git
cd finder-linux
```

### 1. Build
```bash
cmake --preset release
cmake --build --preset release
```

Or without presets:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 2. Run
```bash
# Launch default (Home folder)
./build/finder-linux

# Or open a specific directory
./build/finder-linux /path/to/folder
```

### 3. Run Automated Tests
```bash
ctest --preset release
```

### 4. Install
```bash
./install.sh --user --default
```

This installs the binary, desktop entry, D-Bus activation files, icons,
AppStream metadata, man page, and Bash/Zsh/Fish completions.

---

## 📦 Project & Packaging

- **Repository**: https://github.com/lalantham/finder-linux
- **License**: GPL-3.0-or-later (see `LICENSE`).
- **AppStream metadata**: `data/org.finderlinux.Finder.metainfo.xml`
- **Man page**: `data/finder-linux.1`
- **Shell completions**: `data/completions/` for Bash, Zsh, and Fish.
- **CI**: `.github/workflows/ci.yml` builds and tests on Arch Linux.
- **Releases**: `.github/workflows/release.yml` builds Debian packages, AppImages, and tarballs on `v*` tags.
- **Formatting**: `.clang-format` and `.clang-tidy` are provided for contributors.
- **CMake presets**: `CMakePresets.json` provides `release` and `debug` presets.

### Packaged Releases

Tagged releases publish:

- `finder-linux_<version>_amd64.deb` — Debian 12 / Ubuntu 23.04+ package
- `finder-linux-<version>-1.x86_64.rpm` — Fedora 41+ / RHEL-family package
- `finder-linux-<version>-x86_64.AppImage` — portable single-file build
- `finder-linux-<version>-x86_64.tar.gz` — relocatable install tree
- `SHA256SUMS` — checksums for all assets

#### Installing per distribution

**Debian / Ubuntu**
```bash
sudo apt install ./finder-linux_<version>_amd64.deb
```
`apt` is used rather than `dpkg -i` so the Qt and libarchive dependencies are
resolved. The package *Recommends* the optional helper tools (`udisks2` for
mounting and ejecting media, `libglib2.0-bin` for Trash and remote mounts,
`xdg-utils`, `acl`, `poppler-utils`) rather than requiring them, so it still
installs on a minimal system but pulls in full functionality where available.

**Fedora / RHEL**
```bash
sudo dnf install ./finder-linux-<version>-1.x86_64.rpm
```

**Arch and anything else** — use the AppImage or build from source:
```bash
chmod +x finder-linux-<version>-x86_64.AppImage
./finder-linux-<version>-x86_64.AppImage
```

The AppImage bundles Qt, but it reads the *host* icon theme, so it looks native.
`Help → About File Manager` reports the exact build, which is the quickest way to
confirm you are running a current version.

#### Supported platforms

CI builds and runs the test suite on Debian 12, Ubuntu 24.04, Fedora 41 and
Arch Linux, so a distribution-specific regression is caught before release
rather than reported afterwards.

| Distribution | Minimum | Notes |
| :--- | :--- | :--- |
| Debian | 12 (bookworm) | Qt 6.4, glibc 2.36 |
| Ubuntu | 22.04 | built against Debian 12's glibc 2.34 floor |
| Fedora | 41 | native RPM |
| Arch | rolling | build from source |

The released binaries target glibc 2.34, so Debian 12, Ubuntu 22.04+ and
Fedora 35+ are all supported.

Build the same packages locally:

```bash
cmake -B build-pkg -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DFINDER_VERSION=1.0.0
cmake --build build-pkg
cpack --config build-pkg/CPackConfig.cmake -G DEB -B dist   # needs dpkg-dev
cpack --config build-pkg/CPackConfig.cmake -G RPM -B dist   # needs rpm-build
BUILD_DIR=build-pkg bash packaging/make-appimage.sh 1.0.0
BUILD_DIR=build-pkg bash packaging/make-tarball.sh 1.0.0
```

Each CPack generator needs its own toolchain: `dpkg-dev` for DEB and
`rpm-build` for RPM. The release workflow therefore builds the RPM on a Fedora
container — an RPM produced against Debian's Qt would carry sonames Fedora
cannot satisfy.

Releases are created automatically by pushing a version tag:

```bash
git tag v1.1.4
git push origin v1.1.4
```

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Space` | Quick Look |
| `Ctrl + 1` | Icon View |
| `Ctrl + 2` | List View |
| `Ctrl + 3` | Column View (Miller Columns) |
| `Ctrl + 4` | Gallery View |
| `Ctrl + T` | New Tab |
| `Ctrl + Shift + T` | Reopen Closed Tab |
| `Ctrl + N` | New Window |
| `Ctrl + W` | Close Tab |
| `Ctrl + Tab` / `Ctrl + Shift + Tab` | Next / Previous Tab |
| `Ctrl + Shift + N` | New Folder |
| `F2` | In-place Inline Rename (Icon, List & Column views) |
| `Ctrl + Shift + R` | Batch Rename selected items |
| `Ctrl + C` / `Ctrl + X` / `Ctrl + V` | Copy / Cut / Paste (system clipboard) |
| `Ctrl + D` | Duplicate |
| `Delete` | Move to Trash |
| `Shift + Delete` | Permanently delete |
| `Ctrl + F` | Focus Search |
| `Ctrl + I` | Get Info |
| `Ctrl + J` | Show View Options |
| `F11` | Toggle Full Screen |
| `Ctrl + Alt + S` | Show / Hide Sidebar |
| `Ctrl + H` | Toggle hidden files |
| `Ctrl + ,` | Preferences |
| `Ctrl + K` | Connect to Server |
| `F3` | Toggle Split View (Dual Panes) |
| `F4` | Toggle Integrated Terminal Panel |
| `F5` / `Ctrl + R` | Refresh |
| `Ctrl + +` / `Ctrl + -` / `Ctrl + 0` | Icon zoom in / out / actual size |
| `Ctrl + L` | Focus path bar (with Autocomplete) |
| `Ctrl + Z` / `Ctrl + Shift + Z` | Undo / Redo |
| `Alt + Left` / `Alt + Right` | Back / Forward |
| `Alt + Up` | Enclosing folder |

---

## 🐬 KDE Dolphin Parity & Linux Integration

File Manager for Linux provides a dependable, responsive daily alternative to KDE Dolphin while maintaining clean desktop portability:

* **Split-Pane Browsing (`F3`)**:
  - Independent dual browsing panes per tab.
  - Active pane is highlighted with an accent border and drives the toolbar, path bar, keyboard shortcuts, and status bar.
  - Each pane keeps its own view mode and sort order.
  - Optional **Synchronize Split Panes** keeps both panes on the same folder.
  - Copy/move selected items directly to the other pane from the context menu or Edit menu.
  - Direct drag & drop copy and move between left and right panes.
* **Smart Location Bar (`Ctrl+L`)**:
  - Real-time directory autocomplete via `QCompleter` and `QFileSystemModel`.
  - Tilde (`~` and `~/...`) expansion to home directory.
  - Native `file://` URL support and `Escape` key cancellation.
* **File Operation Reliability & Safety**:
  - **Containment Protection**: Prevents illegal attempts to move or copy directories into themselves or subdirectories.
  - **Cross-Filesystem Safety**: Preserves symlinks without dereferencing, performs verified chunked copies with size checks, and removes source files only after destination verification succeeds.
  - **Broken Symlinks**: Full support for renaming, moving, trashing, and restoring broken symlinks without spurious "File does not exist" errors.
  - **Metadata Preservation**: Copies and cross-filesystem moves preserve permissions, timestamps, extended attributes, ACLs, and ownership where permitted.
  - **Archive Security**: Hardened against Zip Slip / directory traversal (`../`) and unauthorized symlink target escapes outside the extraction root.
* **Performance & Responsiveness**:
  - Thumbnail cache probes, PDF rendering, and media probing run off the UI thread.
  - Thumbnail repaints are coalesced and scaled previews are cached.
  - Thumbnail, Office-conversion, and folder-size work use separate bounded thread pools.
  - Folder-size walks survive directory refreshes and are shared by Column View.
  - Search reads cached tags first and bounds cold xattr probes.
  - Settings writes are debounced and flushed on exit.
  - Preview pane updates are debounced; image/PDF/video thumbnails load asynchronously.
  - Get Info media probing and EXIF/ffprobe work run off the UI thread.
  - PDF pages render at a DPI fitted to the requested target size.
  - Copy uses 1 MB chunks and throttled progress updates.
  - Same-filesystem moves skip recursive size pre-walks.
  - Search cancellation no longer blocks the UI waiting for a scan thread.
  - Sidebar tag and volume refreshes are coalesced.
  - Desktop-app and MIME lookups are cached and invalidated when associations change.
  - Sidebar device discovery runs on a worker thread instead of blocking the UI.
  - Default-application updates avoid a second blocking `gio` process.
  - Toolbar state restores reliably via a named toolbar object.
* **Archive Management**:
  - Compression options dialog with format selection, compression levels 1-9, and optional ZIP AES password protection.
  - Extract password-protected ZIP archives via "Extract with Password...".
  - Extract AppImage bundles into `<name>-extracted` folders.
* **Desktop & KDE Standards**:
  - Terminal launching prioritizes modern standard `xdg-terminal-exec`, KDE `konsole --workdir`, and environment `$TERMINAL` before fallbacks.
  - Plasma cache updating via `kbuildsycoca6` when modifying MIME defaults.
  - Command-line parity: supports `finder-linux --select <path>` (reveals and highlights item in containing folder), `file://` URLs from browsers/launchers, and opening multiple folders into separate tabs.
  - Session persistence: open tabs and window geometry are saved and restored on startup.

* **Integrated Terminal Panel (`F4`)**:
  - Collapsible bottom terminal dock synchronized with the active browsed folder.
  - Interactive shell session with command history (`Up`/`Down`), filename/path autocomplete (`Tab`), ANSI color code support, and `Ctrl+C` interrupt handling.
  - Two-way directory synchronization: navigations in File Manager automatically update the terminal working directory, and running `cd <dir>` in the terminal navigates File Manager to the new folder.
  - Direct quick action to pop out to system external terminal (`xdg-terminal-exec`, `konsole`, `$TERMINAL`).
* **Multi-Tab Polish & Context Menus**:
  - Tab bar context menus: Duplicate Tab, Move Tab to New Window, Close Tab, Close Other Tabs, Close Tabs to the Right.
  - Drag a tab vertically out of the tab strip to tear it off into a new window.
  - Reopen Closed Tab (`Ctrl+Shift+T`) with session history stack.
* **File Operations Queue Manager**:
  - Non-blocking sequential FIFO operation queueing.
  - Multi-task queue progress visibility (`Task X of Y`) with granular "Skip Task" and "Stop All" actions.
* **In-place Inline Renaming**:
  - Directly edit item names in Icon, List, and Column views via `F2` or clicking the selected item label.
  - Preselects the filename stem while preserving the file extension.
  - Automatically records undo actions in `UndoManager`.
* **Everyday File Operations**:
  - Copy To... / Move To... destination picker.
  - New Folder with Selection.
  - Select by wildcard pattern and invert selection in all views.
  - Open in Terminal from a file's parent directory.
  - Calculate SHA-256 checksums for one or many selected files.
* **Desktop Notifications & Background Completion**:
  - Automatically notifies via `org.freedesktop.Notifications` when long-running operations (> 3 seconds) complete or when the window is inactive.
* **KDE / XDG Drag-and-Drop Parity**:
  - Right-click drag or `Alt` drag brings up the drop action menu (*Copy Here*, *Move Here*, *Link Here*, *Cancel*).
  - Cross-device and cross-partition drops automatically default safely to *Copy* to prevent data loss on source volumes.
* **Cross-Application File Clipboard**:
  - Copy/cut files in Dolphin, Nautilus, Thunar, or File Manager and paste them into the other application.
  - Uses the shared `text/uri-list`, `x-special/gnome-copied-files`, and `application/x-kde-cutselection` clipboard formats, including cut-vs-copy semantics.
  - Also reads Flatpak/sandboxed file clipboards via `application/vnd.portal.filetransfer` and the `org.freedesktop.portal.FileTransfer` portal.
* **Enhanced Application Associations & "Open With..."**:
  - Categorized into *Recommended Applications* (matching MIME type) and *Other Applications*.
  - Single-file and multi-file **Open With** support using `%f`, `%F`, `%u`, and `%U` token expansion.
  - Custom command line support with `%f`, `%F`, `%u`, `%U` token expansion.
  - Atomically updates `mimeapps.list` and triggers `kbuildsycoca6` when set as default.
  - AppImage files run directly as applications instead of being passed to a package manager; non-executable AppImages prompt to set the execute bit first. *Open With* still allows using Shelly, Ark, or another handler explicitly.
* **Disk Images & Removable Media**:
  - Mount `.iso`, `.img`, `.raw`, `.squashfs`, and `.udf` images through UDisks2 loop devices.
  - Open a disk image by double-clicking it or via **File → Mount Disk Image...**.
  - Mounted images appear in the sidebar and can be ejected normally; the loop device is cleaned up automatically.
  - `.dmg` images can be mounted when `dmg2img` is installed.
  - Create ISO images from a folder via **File → Create Disk Image from Current Folder...** or the folder context menu, using `xorriso`, `genisoimage`, or `mkisofs`.

* **Removable Media & Drive Management**:
  - Mount, unmount, and eject operations prefer the UDisks2 D-Bus API, with `udisksctl`, `gio`, and `umount` fallbacks.
  - Safely remove USB drives via UDisks2 drive eject/power-off.
  - Direct integration with KDE Partition Manager, GNOME Disks, or GParted for drive formatting and partition inspection.
* **Folder Size Calculations ("Calculate All Sizes")**:
  - Background recursive size computation across folder entries without freezing the UI.
  - Formats folder rows in List and Column views with total bytes and enclosed item counts.
  - Easily toggleable via the View menu or Preferences dialog.
* **Get Info & Metadata**:
  - Tabbed Get Info window: **General**, **Metadata**, **Permissions**, **Open With**, and **Checksums**.
  - Tracks **Date Added** and **Date Last Opened** across sessions.
  - Editable **Version** and **Comments** metadata.
  - Image dimensions and EXIF camera details, plus audio/video duration, codec, and bitrate.
  - Editable extended-attribute table with add, edit, and remove support.
  - Shows inode, device, hard-link count, and on-disk usage.
  - Lists extended attributes, POSIX ACL presence, and SELinux context.
  - On-demand SHA-256 checksum with a one-click copy button.
  - "Show Disk Usage" via Filelight and "Set as Wallpaper" for image files when the helper tools are installed.
* **Symlink Management & Relinking**:
  - Detects broken symlinks with distinct visual indicators in Get Info.
  - Direct interactive target relinking without command line intervention.
* **Advanced Permissions Management**:
  - Multi-item Get Info with combined size, kind, location, tags, and permission state.
  - Interactive octal notation (`0755`, `0644`) synchronized with visual read/write/execute checkboxes.
  - Edit owner and group names, with recursive ownership changes for folders.
  - Immutable "Locked" flag support via filesystem flags.
  - ACL editor powered by `getfacl` / `setfacl` when available.
  - "Apply to Enclosed Items..." recursively updates permissions across directory trees while intelligently preserving directory traversal bits.
  - Tag changes from a multi-item Get Info window apply to every selected item.
* **Thumbnail Generation & Quick Look Previews**:
  - Full Freedesktop XDG thumbnail cache integration (`~/.cache/thumbnails/`) for images, PDFs, and video files.
  - Video thumbnail generation powered by `ffmpegthumbnailer` across all video formats (MP4, MKV, AVI, MOV, WebM, FLV, TS).
  - Preview (`Spacebar`) provides instant preview overlay for video thumbnails, images, and text files.
* **Per-Folder View Preferences**:
  - Optional setting to remember the view mode (Icon, List, Column, Gallery) for each directory individually.
  - Seamlessly switches view mode and syncs toolbar controls when navigating across folders.

---

## 🤝 Contributing

Contributions are welcome. Before opening a pull request:

1. Build with `cmake --preset release`.
2. Run all tests with `ctest --preset release`.
3. Keep new UI strings in `QStringLiteral` and follow `.clang-format`.
4. Run `appstreamcli validate --no-net data/org.finderlinux.Finder.metainfo.xml` after changing desktop metadata.
5. Keep packaging and uninstall script file lists in sync with `CMakeLists.txt`.

Please open an issue first for larger feature changes.
