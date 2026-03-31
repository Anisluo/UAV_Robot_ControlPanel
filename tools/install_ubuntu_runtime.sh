#!/usr/bin/env bash
set -euo pipefail

RUNTIME_PACKAGES=(
  python3
  libqt5core5a
  libqt5gui5
  libqt5widgets5
  libqt5network5
  qt5-gtk-platformtheme
  qtwayland5
)

DESKTOP_PACKAGES=(
  xdg-utils
  desktop-file-utils
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
  ./install_ubuntu_deps.sh

This script installs the runtime dependencies required by HostGUI on Ubuntu.
Validated on Ubuntu 20.04 and 22.04.
EOF
}

require_ubuntu() {
  [[ -f /etc/os-release ]] || die "Cannot detect operating system."
  # shellcheck disable=SC1091
  source /etc/os-release

  [[ "${ID:-}" == "ubuntu" ]] || die "This package currently supports Ubuntu only."

  case "${VERSION_ID:-}" in
    20.04|22.04)
      log "Detected Ubuntu ${VERSION_ID}"
      ;;
    *)
      warn "Detected Ubuntu ${VERSION_ID:-unknown}. The package is validated for 20.04 and 22.04, continuing anyway."
      ;;
  esac
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
  dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"
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
  ${SUDO} apt-get update
  ${SUDO} apt-get install -y "${missing[@]}"
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

  require_ubuntu
  SUDO="$(sudo_prefix)"
  install_packages "${RUNTIME_PACKAGES[@]}" "${DESKTOP_PACKAGES[@]}"
  verify_runtime
  log "You can now run: ./HostGUI"
}

main "$@"
