#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-${FINDER_VERSION:-1.0.0}}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT/build/appimage-tools}"
APPDIR="${APPDIR:-$ROOT/build/AppDir}"
DIST="${DIST:-$ROOT/dist}"

mkdir -p "$TOOLS_DIR" "$DIST"

LINUXDEPLOY="${LINUXDEPLOY:-$TOOLS_DIR/linuxdeploy-x86_64.AppImage}"
QT_PLUGIN="${LINUXDEPLOY_QT_PLUGIN:-$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage}"

download() {
    local url="$1"
    local target="$2"
    if [[ -x "$target" ]]; then
        return
    fi
    echo "Downloading $url"
    curl -fL --retry 3 --connect-timeout 20 -o "$target" "$url"
    chmod +x "$target"
}

download "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$LINUXDEPLOY"
download "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" "$QT_PLUGIN"

# linuxdeploy discovers the Qt plugin by filename on PATH; the downloaded
# plugin already uses the expected name.

rm -rf "$APPDIR"
mkdir -p "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr >/dev/null

# D-Bus activation files use absolute /usr paths and are not useful inside an
# AppImage; the in-app Linux integration handles activation separately.
rm -rf "$APPDIR/usr/share/dbus-1/services"

DESKTOP_FILE="$APPDIR/usr/share/applications/org.finderlinux.Finder.desktop"
ICON_FILE="$APPDIR/usr/share/icons/hicolor/512x512/apps/finder-linux.png"
if [[ -f "$DESKTOP_FILE" ]]; then
    sed -i 's|^Exec=.*|Exec=finder-linux %U|' "$DESKTOP_FILE"
fi

export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64
export PATH="$TOOLS_DIR:$PATH"
export QMAKE="${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake6)}"
export EXTRA_QT_MODULES="${EXTRA_QT_MODULES:-svg multimedia}"
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:-libqwayland-generic.so;libqwayland-egl.so;libqoffscreen.so}"

rm -f "$ROOT"/*.AppImage
pushd "$ROOT" >/dev/null

"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$DESKTOP_FILE" \
    --icon-file "$ICON_FILE" \
    --executable "$APPDIR/usr/bin/finder-linux" \
    --plugin qt \
    --output appimage

APPIMAGE_PATH="$(find "$ROOT" -maxdepth 1 -type f -name '*.AppImage' -printf '%T@ %p\n' 2>/dev/null \
    | sort -nr | head -1 | cut -d' ' -f2-)"
popd >/dev/null
if [[ -z "$APPIMAGE_PATH" ]]; then
    echo "linuxdeploy did not produce an AppImage" >&2
    exit 1
fi
mv "$APPIMAGE_PATH" "$DIST/finder-linux-${VERSION}-x86_64.AppImage"
echo "Created dist/finder-linux-${VERSION}-x86_64.AppImage"
