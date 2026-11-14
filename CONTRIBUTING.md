# Contributing to Finder Linux

Thanks for your interest in improving Finder Linux.

## Development setup

```bash
sudo pacman -S cmake ninja qt6-base qt6-svg qt6-multimedia \
    libarchive poppler-qt6 kwindowsystem
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

On Debian/Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-svg-dev qt6-multimedia-dev \
    libarchive-dev libpoppler-qt6-dev
```

## Before opening a pull request

- Build without warnings where possible.
- Run the full test suite: `ctest --preset release`.
- Validate the desktop entry:
  ```bash
  desktop-file-validate data/org.finderlinux.Finder.desktop
  ```
- Validate AppStream metadata:
  ```bash
  appstreamcli validate --no-net data/org.finderlinux.Finder.metainfo.xml
  ```
- Keep the installer, uninstaller, and `CMakeLists.txt` install lists in sync.
- Follow the existing code style; `.clang-format` is provided.

## Commit messages

Use a short imperative subject line, optionally with a conventional prefix:

```text
feat: add multi-file Open With support
fix: preserve metadata during cross-filesystem moves
docs: document AppStream metadata
```

## Larger changes

Open an issue before starting a large feature or architectural change so the
approach can be discussed first.
