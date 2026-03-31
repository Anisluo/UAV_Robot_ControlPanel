#!/usr/bin/env bash
set -euo pipefail

RUNTIME_PACKAGES=(
  python3
  qt5-qtbase
  qt5-qtbase-gui
  qt5-qtwayland
  xcb-util-image
  xcb-util-keysyms
  xcb-util-renderutil
  xcb-util-wm
  libxkbcommon-x11
  mesa-libGL
  xdg-utils
)

log() {
  printf '[HostGUI runtime] %s\n' "$*"
}

warn() {
  printf '[HostGUI runtime] Warning: %s\n' "$*" >&2
}

die() {
  printf '[HostGUI runtime] Error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  ./install_centos_deps.sh

This script installs the runtime dependencies required by HostGUI on CentOS/RHEL compatible systems.
It is primarily validated for 8.x systems, where kernel 4.18 is common.
EOF
}

require_centos_like() {
  [[ -f /etc/os-release ]] || die "Cannot detect operating system."
  # shellcheck disable=SC1091
  source /etc/os-release

  case "${ID:-}" in
    centos|rhel|rocky|almalinux)
      ;;
    *)
      if [[ "${ID_LIKE:-}" != *rhel* ]] && [[ "${ID_LIKE:-}" != *centos* ]]; then
        die "This package currently supports CentOS/RHEL compatible systems only."
      fi
      ;;
  esac

  log "Detected ${PRETTY_NAME:-${ID:-CentOS-like Linux}}"
  if [[ -n "${VERSION_ID:-}" ]] && [[ "${VERSION_ID}" != 8* ]] && [[ "${VERSION_ID}" != 9* ]]; then
    warn "The package is primarily validated for 8.x and 9.x systems. Continuing anyway."
  fi
}

detect_pkg_manager() {
  if command -v dnf >/dev/null 2>&1; then
    PKG_MGR="dnf"
  elif command -v yum >/dev/null 2>&1; then
    PKG_MGR="yum"
  else
    die "Neither dnf nor yum was found."
  fi
}

sudo_prefix() {
  if [[ "$(id -u)" -eq 0 ]]; then
    echo ""
  else
    command -v sudo >/dev/null 2>&1 || die "sudo is required when not running as root."
    echo "sudo"
  fi
}

is_installed() {
  rpm -q "$1" >/dev/null 2>&1
}

install_packages() {
  local missing=()
  local pkg
  for pkg in "$@"; do
    if is_installed "${pkg}"; then
      log "Already installed: ${pkg}"
    else
      missing+=("${pkg}")
    fi
  done

  if ((${#missing[@]} == 0)); then
    log "No additional packages needed."
    return
  fi

  log "Installing packages: ${missing[*]}"
  ${SUDO} "${PKG_MGR}" install -y "${missing[@]}"
}

verify_runtime() {
  command -v python3 >/dev/null 2>&1 || die "python3 is missing after installation."
  log "Runtime environment is ready."
}

main() {
  if (($# > 0)); then
    case "$1" in
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "Unknown argument: $1"
        ;;
    esac
  fi

  require_centos_like
  detect_pkg_manager
  SUDO="$(sudo_prefix)"

  install_packages "${RUNTIME_PACKAGES[@]}"
  verify_runtime
  log "You can now run: ./HostGUI"
}

main "$@"
