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
# Match the installer: when started with sudo, clean up the invoking user's
# per-user FileManager1 activation file rather than /root's.
USER_HOME="${HOME}"
if [[ $EUID -eq 0 && -n "${SUDO_USER:-}" ]]; then
  RESOLVED_HOME=$(getent passwd "${SUDO_USER}" | cut -d: -f6)
  [[ -n "${RESOLVED_HOME}" ]] && USER_HOME="${RESOLVED_HOME}"
fi
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

echo -e "${YELLOW}${BOLD}==> Uninstalling File Manager from ${PREFIX}...${NC}\n"

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
${SUDO} rm -f "${PREFIX}/share/applications/org.finderlinux.Finder.desktop"
${SUDO} rm -f "${PREFIX}/share/applications/finder-linux.desktop"
# Also remove the user-scoped copies / migration stubs.
rm -f "${USER_HOME}/.local/share/applications/org.finderlinux.Finder.desktop" 2>/dev/null || true
rm -f "${USER_HOME}/.local/share/applications/finder-linux.desktop" 2>/dev/null || true

# Remove the FileManager1 and org.freedesktop.Application activation files.
# org.freedesktop.FileManager1 is a shared name that other file managers also
# provide, so only delete the system-wide copy if it actually points at Finder;
# removing it unconditionally would break "Show in folder" for nautilus or
# dolphin. Package installs no longer ship that file at all.
echo -e "${BLUE}==>${NC} Removing D-Bus activation registrations..."
SYSTEM_FM1="${PREFIX}/share/dbus-1/services/org.freedesktop.FileManager1.service"
if [[ -f "${SYSTEM_FM1}" ]] && grep -q "finder-linux" "${SYSTEM_FM1}" 2>/dev/null; then
  ${SUDO} rm -f "${SYSTEM_FM1}"
fi
${SUDO} rm -f "${PREFIX}/share/dbus-1/services/org.finderlinux.Finder.service"
${SUDO} rm -f "${PREFIX}/share/dbus-1/services/finder-linux.service"
rm -f "${USER_HOME}/.local/share/dbus-1/services/org.freedesktop.FileManager1.service" 2>/dev/null || true
rm -f "${USER_HOME}/.local/share/dbus-1/services/org.finderlinux.Finder.service" 2>/dev/null || true
rm -f "${USER_HOME}/.local/share/dbus-1/services/finder-linux.service" 2>/dev/null || true
dbus-send --session --type=method_call --dest=org.freedesktop.DBus \
  /org/freedesktop/DBus org.freedesktop.DBus.ReloadConfig >/dev/null 2>&1 || true

# Remove documentation, completions, and metadata
echo -e "${BLUE}==>${NC} Removing documentation and shell completions..."
${SUDO} rm -f "${PREFIX}/share/man/man1/finder-linux.1"
${SUDO} rm -f "${PREFIX}/share/metainfo/org.finderlinux.Finder.metainfo.xml"
${SUDO} rm -f "${PREFIX}/share/bash-completion/completions/finder-linux"
${SUDO} rm -f "${PREFIX}/share/zsh/site-functions/_finder-linux"
${SUDO} rm -f "${PREFIX}/share/fish/vendor_completions.d/finder-linux.fish"
${SUDO} rm -rf "${PREFIX}/share/licenses/finder-linux"

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
