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
# When the installer is started with sudo, target the invoking user's home for
# per-user desktop/D-Bus integration instead of /root.
USER_HOME="${HOME}"
if [[ $EUID -eq 0 && -n "${SUDO_USER:-}" ]]; then
  RESOLVED_HOME=$(getent passwd "${SUDO_USER}" | cut -d: -f6)
  [[ -n "${RESOLVED_HOME}" ]] && USER_HOME="${RESOLVED_HOME}"
fi
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

DESKTOP_FILE="${PREFIX}/share/applications/org.finderlinux.Finder.desktop"
if [[ -f "${DESKTOP_FILE}" ]]; then
  if [[ -n "${SUDO}" ]]; then
    ${SUDO} sed -i "s|^Exec=finder-linux|Exec=${PREFIX}/bin/finder-linux|" "${DESKTOP_FILE}"
  else
    sed -i "s|^Exec=finder-linux|Exec=${PREFIX}/bin/finder-linux|" "${DESKTOP_FILE}"
  fi
fi

# Migrate from the old finder-linux.desktop ID. DBusActivatable requires a
# reverse-DNS desktop ID, so the launcher is now org.finderlinux.Finder.
OLD_DESKTOP_ID="finder-linux"
NEW_DESKTOP_ID="org.finderlinux.Finder"
USER_APPS_DIR="${USER_HOME}/.local/share/applications"
mkdir -p "${USER_APPS_DIR}"
cat > "${USER_APPS_DIR}/${OLD_DESKTOP_ID}.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Finder
Exec=finder-linux %U
Hidden=true
EOF
if [[ $EUID -eq 0 && -n "${SUDO_USER:-}" ]]; then
  chown "${SUDO_USER}:" "${USER_APPS_DIR}/${OLD_DESKTOP_ID}.desktop" 2>/dev/null || true
fi

MIMEAPPS_FILE="${USER_HOME}/.config/mimeapps.list"
if [[ -f "${MIMEAPPS_FILE}" ]]; then
  if [[ $EUID -eq 0 && -n "${SUDO_USER:-}" ]]; then
    sudo -u "${SUDO_USER}" sed -i "s/${OLD_DESKTOP_ID}\\.desktop/${NEW_DESKTOP_ID}.desktop/g" "${MIMEAPPS_FILE}" 2>/dev/null || true
  else
    sed -i "s/${OLD_DESKTOP_ID}\\.desktop/${NEW_DESKTOP_ID}.desktop/g" "${MIMEAPPS_FILE}" 2>/dev/null || true
  fi
fi

rm -f "${USER_HOME}/.local/share/dbus-1/services/${OLD_DESKTOP_ID}.service" 2>/dev/null || true

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

# Register Finder for the freedesktop.org FileManager1 D-Bus interface and
# the standard org.freedesktop.Application activation interface. Browsers use
# FileManager1 for "Show in folder"; launchers use Application for Activate/Open.
# A user-scoped activation file takes precedence over system-wide providers
# such as Dolphin or elementary Files.
FINDER_BIN="${PREFIX}/bin/finder-linux"
if [[ -x "${FINDER_BIN}" ]]; then
  DBUS_SERVICE_DIR="${USER_HOME}/.local/share/dbus-1/services"
  mkdir -p "${DBUS_SERVICE_DIR}"

  install_dbus_activation() {
    local service_name="$1"
    cat > "${DBUS_SERVICE_DIR}/${service_name}.service" <<EOF
[D-BUS Service]
Name=${service_name}
Exec="${FINDER_BIN}"
EOF
    if [[ $EUID -eq 0 && -n "${SUDO_USER:-}" ]]; then
      chown "${SUDO_USER}:" "${DBUS_SERVICE_DIR}/${service_name}.service" 2>/dev/null || true
    fi
  }

  install_dbus_activation org.freedesktop.FileManager1
  install_dbus_activation org.finderlinux.Finder
  rm -f "${DBUS_SERVICE_DIR}/finder-linux.service" 2>/dev/null || true
  echo -e "${GREEN}✓${NC} Registered Finder for the FileManager1 and Application D-Bus interfaces."

  # Make the session bus pick up the new activation file without a re-login.
  dbus-send --session --type=method_call --dest=org.freedesktop.DBus \
    /org/freedesktop/DBus org.freedesktop.DBus.ReloadConfig >/dev/null 2>&1 || true

  # If another file-manager daemon already owns the name, it must exit once
  # before Finder can take over. Only daemons are listed here; no file manager
  # windows are affected.
  if command -v busctl >/dev/null 2>&1; then
    OWNER_PID=$(busctl --user list --no-legend 2>/dev/null \
      | awk '$1=="org.freedesktop.FileManager1" {print $2; exit}')
    if [[ -n "${OWNER_PID}" && "${OWNER_PID}" =~ ^[0-9]+$ ]]; then
      OWNER_CMD=$(tr '\0' ' ' < "/proc/${OWNER_PID}/cmdline" 2>/dev/null || true)
      if [[ -n "${OWNER_CMD}" && "${OWNER_CMD}" != *"finder-linux"* ]]; then
        echo -e "${YELLOW}Notice:${NC} '${OWNER_CMD}' is currently the FileManager1 provider."
        echo -e "Finder will take over after you log out and back in (or after"
        echo -e "that daemon exits). To switch immediately, run:"
        echo -e "  ${BOLD}pkill -f io.elementary.files-daemon${NC}   # or: pkill -f 'dolphin --daemon'"
      fi
    fi
  fi
fi

# Set Finder as the default file manager, or migrate an existing old ID.
NEW_DESKTOP_ID="org.finderlinux.Finder"
OLD_DESKTOP_ID="finder-linux"
CURRENT_DEFAULT=$(xdg-mime query default inode/directory 2>/dev/null || true)
if [[ "${SET_DEFAULT}" == "true" || "${CURRENT_DEFAULT}" == "${OLD_DESKTOP_ID}.desktop" ]]; then
  echo -e "\n${BLUE}==>${NC} Setting Finder as default file manager..."
  xdg-mime default "${NEW_DESKTOP_ID}.desktop" inode/directory
  if command -v gio >/dev/null 2>&1; then
    gio mime inode/directory "${NEW_DESKTOP_ID}.desktop" 2>/dev/null || true
  fi
  echo -e "${GREEN}✓${NC} Finder is now your default file manager."
fi

if pgrep -x finder-linux >/dev/null 2>&1; then
  echo -e "\n${YELLOW}${BOLD}Notice:${NC} Finder is currently running. Restart it once so the"
  echo -e "new FileManager1 and Application (\"Show in folder\"/launcher) integration takes effect."
fi

echo -e "\n${GREEN}${BOLD}✓ Installation Successful!${NC}"
echo -e "You can launch Finder by running: ${BOLD}finder-linux${NC}"
echo -e "Or search for ${BOLD}Finder${NC} in your desktop application launcher."
