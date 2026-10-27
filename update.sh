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

echo -e "${BLUE}${BOLD}==> Updating macOS Finder for Linux...${NC}\n"

# Check if inside git repo and offer to pull
if [ -d "${ROOT_DIR}/.git" ]; then
  if command -v git >/dev/null 2>&1; then
    echo -e "${BLUE}==>${NC} Checking for latest git updates..."
    git -C "${ROOT_DIR}" pull --rebase || echo -e "${YELLOW}Warning: Git pull failed or no remote configured, continuing with local updates.${NC}"
  fi
fi

# Detect existing prefix from CMakeCache if available
PREFIX="/usr/local"
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  CACHED_PREFIX=$(grep "^CMAKE_INSTALL_PREFIX:PATH=" "${BUILD_DIR}/CMakeCache.txt" | cut -d'=' -f2)
  if [ -n "${CACHED_PREFIX}" ]; then
    PREFIX="${CACHED_PREFIX}"
  fi
fi

echo -e "${BLUE}==>${NC} Detected install prefix: ${BOLD}${PREFIX}${NC}"

SUDO=""
if [[ "${PREFIX}" != "${HOME}"* && $EUID -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
    echo -e "${YELLOW}Root privileges required to update files in ${PREFIX}.${NC}\n"
  fi
fi

# Compile incrementally
echo -e "${BLUE}==>${NC} Compiling updates with Ninja..."
ninja -C "${BUILD_DIR}"

# Run tests
echo -e "\n${BLUE}==>${NC} Running automated test verification..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

# Install updated files
echo -e "\n${BLUE}==>${NC} Installing updated binaries and assets..."
if [[ -n "${SUDO}" ]]; then
  ${SUDO} cmake --install "${BUILD_DIR}"
else
  cmake --install "${BUILD_DIR}"
fi

# Refresh desktop & icon caches
echo -e "\n${BLUE}==>${NC} Refreshing system caches..."
if command -v update-desktop-database >/dev/null 2>&1; then
  if [[ -n "${SUDO}" ]]; then
    ${SUDO} update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
  else
    update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
  fi
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  if [[ -n "${SUDO}" ]]; then
    ${SUDO} gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
  else
    gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
  fi
fi

if command -v kbuildsycoca6 >/dev/null 2>&1; then
  kbuildsycoca6 --noincremental 2>/dev/null || true
fi

echo -e "\n${GREEN}${BOLD}✓ Update Complete!${NC}"
echo -e "Finder is up to date and ready to use."
