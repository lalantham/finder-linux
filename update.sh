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

# Parse optional arguments
OVERRIDE_PREFIX=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --user)
      OVERRIDE_PREFIX="${HOME}/.local"
      shift
      ;;
    --system)
      OVERRIDE_PREFIX="/usr"
      shift
      ;;
    --prefix)
      OVERRIDE_PREFIX="$2"
      shift 2
      ;;
    -h|--help)
      echo "Usage: ./update.sh [options]"
      echo "Options:"
      echo "  --user       Update user installation (~/.local)"
      echo "  --system     Update system installation (/usr, requires root/sudo)"
      echo "  --prefix DIR Update installation in custom directory"
      exit 0
      ;;
    *)
      shift
      ;;
  esac
done

# Check if inside git repo with a remote configured
if [ -d "${ROOT_DIR}/.git" ] && command -v git >/dev/null 2>&1; then
  REMOTES=$(git -C "${ROOT_DIR}" remote 2>/dev/null || true)
  if [ -n "${REMOTES}" ]; then
    echo -e "${BLUE}==>${NC} Checking for remote git updates..."
    git -C "${ROOT_DIR}" pull --rebase || echo -e "${YELLOW}Warning: Git pull failed, continuing with local build.${NC}"
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

if [ -n "${OVERRIDE_PREFIX}" ]; then
  PREFIX="${OVERRIDE_PREFIX}"
fi

echo -e "${BLUE}==>${NC} Target install prefix: ${BOLD}${PREFIX}${NC}"

SUDO=""
if [[ "${PREFIX}" != "${HOME}"* && $EUID -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
    echo -e "${YELLOW}Root privileges required to update files in ${PREFIX}.${NC}\n"
  else
    echo -e "${RED}Error: Root privileges required for ${PREFIX}. Please run with sudo.${NC}"
    exit 1
  fi
fi

# Reconfigure cmake if prefix changed
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  CURR_CACHED=$(grep "^CMAKE_INSTALL_PREFIX:PATH=" "${BUILD_DIR}/CMakeCache.txt" | cut -d'=' -f2)
  if [ "${CURR_CACHED}" != "${PREFIX}" ]; then
    echo -e "${BLUE}==>${NC} Reconfiguring prefix to ${PREFIX}..."
    cmake -B "${BUILD_DIR}" -DCMAKE_INSTALL_PREFIX="${PREFIX}" "${ROOT_DIR}"
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

# Ensure user desktop file specifies absolute binary path
DESKTOP_FILE="${PREFIX}/share/applications/finder-linux.desktop"
if [ -f "${DESKTOP_FILE}" ]; then
  if [[ -n "${SUDO}" ]]; then
    ${SUDO} sed -i "s|^Exec=finder-linux|Exec=${PREFIX}/bin/finder-linux|" "${DESKTOP_FILE}"
  else
    sed -i "s|^Exec=finder-linux|Exec=${PREFIX}/bin/finder-linux|" "${DESKTOP_FILE}"
  fi
fi

# Also update user desktop file if installing as system or vice versa
USER_DESKTOP="${HOME}/.local/share/applications/finder-linux.desktop"
if [ -f "${USER_DESKTOP}" ]; then
  sed -i "s|^Exec=.*finder-linux|Exec=${PREFIX}/bin/finder-linux|" "${USER_DESKTOP}" 2>/dev/null || true
fi

# Check if an older /usr/bin/finder-linux binary exists that might shadow this installation
if [ -f "/usr/bin/finder-linux" ] && [[ "${PREFIX}" != "/usr" ]]; then
  if [[ $EUID -eq 0 ]]; then
    cp "${BUILD_DIR}/finder-linux" "/usr/bin/finder-linux"
    echo -e "${GREEN}✓${NC} Also updated system-wide binary at /usr/bin/finder-linux"
  elif command -v sudo >/dev/null 2>&1; then
    echo -e "\n${YELLOW}Notice:${NC} A system-wide binary exists at /usr/bin/finder-linux."
    echo -e "To update it as well, run: ${BOLD}sudo cp ${BUILD_DIR}/finder-linux /usr/bin/finder-linux${NC}"
  fi
fi

# Refresh desktop & icon caches
echo -e "\n${BLUE}==>${NC} Refreshing system caches..."
if command -v update-desktop-database >/dev/null 2>&1; then
  if [[ -n "${SUDO}" ]]; then
    ${SUDO} update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
  else
    update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
  fi
  update-desktop-database "${HOME}/.local/share/applications" 2>/dev/null || true
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

# Check if Finder is currently running
if pgrep -x finder-linux >/dev/null 2>&1; then
  echo -e "\n${YELLOW}${BOLD}Notice:${NC} Finder is currently running. Please close and reopen it for updates to take effect."
fi

echo -e "\n${GREEN}${BOLD}✓ Update Complete!${NC}"
echo -e "Finder is up to date and ready to use."
