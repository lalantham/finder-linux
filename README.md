# Finder Linux

A premium, native Linux file manager inspired by Apple's macOS Finder, built with modern **C++20** and **Qt 6 Widgets**.

Designed as a native desktop application with genuine filesystem operations, responsive non-blocking background workers, robust error handling, high visual fidelity to Finder's iconic interface, and freedesktop.org standards compliance.

---

## 🌟 Key Features

### 1. The Four Iconic Finder Browsing Views
* **Icon View (`Ctrl+1`)**:
  - macOS Finder-style grid layout with smooth wrapping and clean item spacing.
  - Multi-line text labels with middle-elision and selection highlight pills.
  - Configurable icon zoom levels and real-time layout adjustment.
* **List View (`Ctrl+2`)**:
  - Detailed hierarchical list showing Name, Date Modified, Size, and Kind.
  - Click-to-sort columns with sort direction indicators and directories-first grouping.
  - Interactive column resizing and reordering.
* **Column View (`Ctrl+3`)**:
  - Authentic implementation of **Miller Columns**.
  - Clicking a folder dynamically opens the next column to the right.
  - Selecting a file displays a live Finder preview pane with file attributes and thumbnail.
  - Smooth auto-scrolling to the active column and keyboard-driven traversal (Left/Right/Up/Down).
* **Gallery View (`Ctrl+4`)**:
  - Large central hero preview viewport.
  - Horizontal filmstrip carousel at the bottom with real-time selection synchronization.
  - Displays instant image previews, text documents, or rendered PDF pages.

### 2. Quick Look (`Space`)
* Dedicated floating HUD overlay.
* Instant inspection of images, source code, text files, and multi-page PDFs (via `Poppler-Qt6`).
* Previous (`◀`) and Next (`▶`) navigation arrows to flip through selected items.
* Dismissible instantly with `Space` or `Esc`.

### 3. File Operations & Data Integrity Engine
* **Asynchronous Execution**: Copy, move, trash, duplicate, and delete operations execute on dedicated worker threads without blocking the GUI.
* **Safe Chunked Streams**: Operations stream data in 64 KB buffers, allowing instant cancellation and real-time progress calculations (speed, transferred bytes, item counts).
* **Temporary File Strategy**: Data is written to hidden temporary files first and then atomically renamed into place upon completion to prevent corrupted partial files.
* **Cross-Filesystem Moves**: When moving across distinct mount points, atomic renames fall back to verified recursive copying and source cleanup. Sources are never deleted if destination transfers fail.
* **Conflict Resolution**: When a destination file collision occurs, users are prompted with an interactive conflict dialog:
  - *Replace*
  - *Keep Both* (auto-renames, e.g. `document copy.txt`)
  - *Skip*
  - *Cancel*
  - *Apply to all* option
* **Undo & Redo (`Ctrl+Z` / `Ctrl+Shift+Z`)**: Reversible operations (Folder creation, Renaming, Moving, and Trashing) are tracked on an bounded undo stack.

### 4. Freedesktop.org XDG Trash Specification
* Moves items to `$XDG_DATA_HOME/Trash/files` and records metadata in `$XDG_DATA_HOME/Trash/info/*.trashinfo`.
* Full restoration support back to the original filesystem path.
* Confirmation dialog before permanent deletion or emptying the trash.

### 5. macOS Finder UI Design & Theming
* Segmented pill buttons: `[ ‹ | › ]` navigation and `[ ⊞ | ☰ | ☷ | ▣ ]` view switcher.
* Interactive Breadcrumb path bar with direct segment navigation and `Ctrl+L` editable path mode.
* macOS sidebar divided into **Favorites** (Home, Desktop, Documents, Downloads, custom pinned folders) and **Locations** (Mounted drives, Root drive, Trash).
* Dynamic **Light** and **Dark** themes following macOS Big Sur / Sonoma design conventions.

### 6. Desktop Integration & Tabs
* Tabbed browsing (`Ctrl+T` new tab, `Ctrl+W` close tab) with independent navigation history.
* Native application launching with default Linux associations (`xdg-open` / `QDesktopServices`).
* Open in Terminal shortcut (`Ctrl+Alt+T`) supporting popular Linux terminal emulators (`ptyxis`, `gnome-terminal`, `konsole`, `kitty`, `alacritty`, `xfce4-terminal`).

---

## 🛠 Prerequisites & Dependencies

- **Compiler**: GCC 11+ or Clang 14+ supporting C++20.
- **Build System**: CMake 3.22+ and Ninja.
- **Qt Framework**: Qt 6.4+ (`QtCore`, `QtGui`, `QtWidgets`, `QtConcurrent`, `QtSvg`, `QtTest`).
- **Optional**: `poppler-qt6` (for PDF preview rendering).

### Installing Dependencies on Arch / Manjaro / CachyOS:
```bash
sudo pacman -S cmake ninja qt6-base qt6-svg poppler-qt6
```

### Installing Dependencies on Ubuntu / Debian:
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-svg-dev libpoppler-qt6-dev
```

### Installing Dependencies on Fedora:
```bash
sudo dnf install gcc-c++ cmake ninja-build \
    qt6-qtbase-devel qt6-qtsvg-devel poppler-qt6-devel
```

---

## 🚀 Building & Running

### 1. Build
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
ctest --test-dir build --output-on-failure
```

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Space` | Quick Look preview |
| `Ctrl + 1` | Icon View |
| `Ctrl + 2` | List View |
| `Ctrl + 3` | Column View (Miller Columns) |
| `Ctrl + 4` | Gallery View |
| `Ctrl + T` | New Tab |
| `Ctrl + W` | Close Tab |
| `Ctrl + Shift + N` | New Folder |
| `F2` | Rename selected item |
| `Ctrl + C` / `Ctrl + X` / `Ctrl + V` | Copy / Cut / Paste |
| `Ctrl + D` | Duplicate |
| `Delete` | Move to Trash |
| `Shift + Delete` | Permanently delete |
| `Ctrl + I` | Get Info (Properties) |
| `Ctrl + H` | Toggle hidden files |
| `Ctrl + L` | Focus path bar |
| `Ctrl + Z` / `Ctrl + Shift + Z` | Undo / Redo |
| `Alt + Left` / `Alt + Right` | Back / Forward |
| `Alt + Up` | Enclosing folder |
