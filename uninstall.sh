#!/usr/bin/env bash
set -e

# Colors
BOLD='\033[1m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

PREFIX="/usr/local"
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  CACHED_PREFIX=$(grep "^CMAKE_INSTALL_PREFIX:PATH=" "${BUILD_DIR}/CMakeCache.txt" | cut -d'=' -f2)
  if [ -n "${CACHED_PREFIX}" ]; then
    PREFIX="${CACHED_PREFIX}"
  fi
fi

if [[ $# -gt 0 && "$1" == "--prefix" ]]; then
  PREFIX="$2"
fi

echo -e "${YELLOW}${BOLD}==> Uninstalling Finder from ${PREFIX}...${NC}\n"

SUDO=""
if [[ "${PREFIX}" != "${HOME}"* && $EUID -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  fi
fi

# Remove binary
echo -e "${BLUE}==>${NC} Removing executable..."
${SUDO} rm -f "${PREFIX}/bin/finder-linux"

# Remove desktop entry
echo -e "${BLUE}==>${NC} Removing desktop launcher..."
${SUDO} rm -f "${PREFIX}/share/applications/finder-linux.desktop"

# Remove icons
echo -e "${BLUE}==>${NC} Removing application icons..."
${SUDO} rm -f "${PREFIX}/share/icons/hicolor/"*/apps/finder-linux.*

# Update caches
echo -e "${BLUE}==>${NC} Updating desktop caches..."
if command -v update-desktop-database >/dev/null 2>&1; then
  ${SUDO} update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  ${SUDO} gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
  kbuildsycoca6 --noincremental 2>/dev/null || true
fi

echo -e "\n${GREEN}${BOLD}✓ Uninstallation complete.${NC}"
