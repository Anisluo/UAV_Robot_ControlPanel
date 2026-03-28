#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODE="runtime"
DO_BUILD=0

RUNTIME_PACKAGES=(
  python3
  libqt5core5a
  libqt5gui5
  libqt5widgets5
  libqt5network5
  qt5-gtk-platformtheme
  qtwayland5
)

BUILD_PACKAGES=(
  build-essential
  pkg-config
  qtbase5-dev
)

DESKTOP_PACKAGES=(
  xdg-utils
  desktop-file-utils
)

log() {
  printf '[HostGUI setup] %s\n' "$*"
}

warn() {
  printf '[HostGUI setup] Warning: %s\n' "$*" >&2
}

die() {
  printf '[HostGUI setup] Error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  ./tools/install_ubuntu_deps.sh [options]

Options:
  --runtime-only      Install runtime dependencies only (default)
  --with-build-deps   Install runtime + build dependencies
  --build             Build HostGUI after dependency installation
  -h, --help          Show this help

Examples:
  ./tools/install_ubuntu_deps.sh
  ./tools/install_ubuntu_deps.sh --with-build-deps
  ./tools/install_ubuntu_deps.sh --with-build-deps --build
EOF
}

parse_args() {
  while (($# > 0)); do
    case "$1" in
      --runtime-only)
        MODE="runtime"
        ;;
      --with-build-deps)
        MODE="build"
        ;;
      --build)
        DO_BUILD=1
        MODE="build"
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "Unknown argument: $1"
        ;;
    esac
    shift
  done
}

require_ubuntu() {
  [[ -f /etc/os-release ]] || die "Cannot detect operating system."
  # shellcheck disable=SC1091
  source /etc/os-release

  [[ "${ID:-}" == "ubuntu" ]] || die "This script currently supports Ubuntu only."

  case "${VERSION_ID:-}" in
    20.04|22.04)
      log "Detected Ubuntu ${VERSION_ID}"
      ;;
    *)
      warn "Detected Ubuntu ${VERSION_ID:-unknown}. The script is validated for 20.04 and 22.04, continuing anyway."
      ;;
  esac
}

require_apt() {
  command -v apt-get >/dev/null 2>&1 || die "apt-get not found."
  command -v dpkg-query >/dev/null 2>&1 || die "dpkg-query not found."
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
  local missing_cmds=()

  command -v python3 >/dev/null 2>&1 || missing_cmds+=("python3")

  if ((${#missing_cmds[@]} > 0)); then
    die "Runtime command verification failed: ${missing_cmds[*]}"
  fi

  log "Runtime command verification passed."
}

verify_build() {
  local missing_cmds=()

  command -v g++ >/dev/null 2>&1 || missing_cmds+=("g++")
  command -v make >/dev/null 2>&1 || missing_cmds+=("make")
  command -v pkg-config >/dev/null 2>&1 || missing_cmds+=("pkg-config")
  command -v moc >/dev/null 2>&1 || missing_cmds+=("moc")

  pkg-config --exists Qt5Widgets Qt5Network || missing_cmds+=("Qt5 pkg-config metadata")

  if ((${#missing_cmds[@]} > 0)); then
    die "Build dependency verification failed: ${missing_cmds[*]}"
  fi

  log "Build dependency verification passed."
}

maybe_build() {
  if [[ "${DO_BUILD}" -ne 1 ]]; then
    return
  fi

  log "Building HostGUI ..."
  make -C "${PROJECT_ROOT}" -j"$(nproc)"
  log "Build completed: ${PROJECT_ROOT}/build/bin/HostGUI"
}

main() {
  parse_args "$@"
  require_ubuntu
  require_apt
  SUDO="$(sudo_prefix)"

  log "Installing runtime dependencies ..."
  install_packages "${RUNTIME_PACKAGES[@]}" "${DESKTOP_PACKAGES[@]}"
  verify_runtime

  if [[ "${MODE}" == "build" ]]; then
    log "Installing build dependencies ..."
    install_packages "${BUILD_PACKAGES[@]}"
    verify_build
  fi

  maybe_build

  log "Done."
  if [[ "${MODE}" == "runtime" ]]; then
    log "Runtime environment is ready."
  else
    log "Runtime and build environments are ready."
  fi
}

main "$@"
