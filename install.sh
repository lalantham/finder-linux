#!/usr/bin/env bash
set -e

# Colors
BOLD='\033[1m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo -e "${BLUE}${BOLD}"
echo "  ______ _           _             _      _                  "
echo " |  ____(_)         | |           | |    (_)                 "
echo " | |__   _ _ __   __| | ___ _ __  | |     _ _ __  _   ___  __"
echo " |  __| | | '_ \ / _\` |/ _ \ '__| | |    | | '_ \| | | \ \/ /"
echo " | |    | | | | | (_| |  __/ |    | |____| | | | | |_| |>  < "
echo " |_|    |_|_| |_|\__,_|\___|_|    |______|_|_| |_|\__,_/_/\_\\"
echo -e "${NC}"
echo -e "${BOLD}macOS Finder for Linux - Installer & Updater${NC}\n"

# Default configuration
PREFIX="/usr/local"
SET_DEFAULT=false
BUILD_TYPE="Release"
INSTALL_MODE="system"

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --user)
      INSTALL_MODE="user"
      PREFIX="${HOME}/.local"
      shift
      ;;
    --system)
      INSTALL_MODE="system"
      PREFIX="/usr"
      shift
      ;;
    --prefix)
      PREFIX="$2"
      shift 2
      ;;
    --default)
      SET_DEFAULT=true
      shift
      ;;
    --debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    -h|--help)
      echo "Usage: ./install.sh [OPTIONS]"
      echo ""
      echo "Options:"
      echo "  --system         Install system-wide to /usr (requires sudo)"
      echo "  --user           Install to user directory (~/.local, no root needed)"
      echo "  --prefix <path>  Install to custom prefix (default: /usr/local)"
      echo "  --default        Set Finder as the default file manager"
      echo "  --debug          Build in Debug mode instead of Release"
      echo "  -h, --help       Show this help message"
      exit 0
      ;;
    *)
      echo -e "${RED}Unknown option: $1${NC}"
      exit 1
      ;;
  esac
done

# Check permissions for system-wide install
SUDO=""
if [[ "${INSTALL_MODE}" == "system" && "${PREFIX}" != "${HOME}"* ]]; then
  if [[ $EUID -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
      SUDO="sudo"
      echo -e "${YELLOW}System-wide installation requires root privileges.${NC}"
      echo -e "You may be prompted for your password via sudo.\n"
    else
      echo -e "${RED}Error: Root privileges required to install to ${PREFIX}.${NC}"
      echo "Please run as root or use './install.sh --user' for user-space installation."
      exit 1
    fi
  fi
fi

# Check required build tools
echo -e "${BLUE}==>${NC} Checking dependencies..."
for cmd in cmake ninja c++; do
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo -e "${RED}Error: Required command '${cmd}' not found.${NC}"
    exit 1
  fi
done
echo -e "${GREEN}✓${NC} Build tools found."

# Configure and compile
echo -e "\n${BLUE}==>${NC} Configuring build with CMake (Prefix: ${PREFIX}, Mode: ${BUILD_TYPE})..."
cmake -B "${BUILD_DIR}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
  "${ROOT_DIR}"

echo -e "\n${BLUE}==>${NC} Compiling Finder..."
ninja -C "${BUILD_DIR}"

echo -e "\n${BLUE}==>${NC} Running automated test suite..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

# Install
echo -e "\n${BLUE}==>${NC} Installing to ${PREFIX}..."
if [[ -n "${SUDO}" ]]; then
  ${SUDO} cmake --install "${BUILD_DIR}"
else
  cmake --install "${BUILD_DIR}"
fi

# Update desktop & icon caches
echo -e "\n${BLUE}==>${NC} Updating desktop and icon caches..."
if command -v update-desktop-database >/dev/null 2>&1; then
  if [[ "${INSTALL_MODE}" == "system" && -n "${SUDO}" ]]; then
    ${SUDO} update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
  else
    update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
  fi
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  if [[ "${INSTALL_MODE}" == "system" && -n "${SUDO}" ]]; then
    ${SUDO} gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
  else
    gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
  fi
fi

if command -v kbuildsycoca6 >/dev/null 2>&1; then
  kbuildsycoca6 --noincremental 2>/dev/null || true
fi

# Set default file manager if requested
if [[ "${SET_DEFAULT}" == "true" ]]; then
  echo -e "\n${BLUE}==>${NC} Setting Finder as default file manager..."
  xdg-mime default finder-linux.desktop inode/directory
  if command -v gio >/dev/null 2>&1; then
    gio mime inode/directory finder-linux.desktop 2>/dev/null || true
  fi
  echo -e "${GREEN}✓${NC} Finder is now your default file manager."
fi

echo -e "\n${GREEN}${BOLD}✓ Installation Successful!${NC}"
echo -e "You can launch Finder by running: ${BOLD}finder-linux${NC}"
echo -e "Or search for ${BOLD}Finder${NC} in your desktop application launcher."
