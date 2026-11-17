#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-${FINDER_VERSION:-1.1.0}}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
STAGE_ROOT="${TARBALL_STAGE:-$ROOT/build/tarball}"
DIST="${DIST:-$ROOT/dist}"
STAGE="$STAGE_ROOT/finder-linux-$VERSION"

rm -rf "$STAGE_ROOT"
mkdir -p "$STAGE" "$DIST"

DESTDIR="$STAGE" cmake --install "$BUILD_DIR" --prefix /usr >/dev/null

tar -C "$STAGE_ROOT" -czf "$DIST/finder-linux-${VERSION}-x86_64.tar.gz" \
    "finder-linux-$VERSION"

echo "Created $DIST/finder-linux-${VERSION}-x86_64.tar.gz"
