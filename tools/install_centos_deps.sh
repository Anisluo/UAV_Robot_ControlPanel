#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODE="runtime"
DO_BUILD=0

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

BUILD_PACKAGES=(
  gcc-c++
  make
  pkgconf-pkg-config
  qt5-qtbase-devel
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
  ./tools/install_centos_deps.sh [options]

Options:
  --runtime-only      Install runtime dependencies only (default)
  --with-build-deps   Install runtime + build dependencies
  --build             Build HostGUI after dependency installation
  -h, --help          Show this help

Examples:
  ./tools/install_centos_deps.sh
  ./tools/install_centos_deps.sh --with-build-deps
  ./tools/install_centos_deps.sh --with-build-deps --build
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

require_centos_like() {
  [[ -f /etc/os-release ]] || die "Cannot detect operating system."
  # shellcheck disable=SC1091
  source /etc/os-release

  case "${ID:-}" in
    centos|rhel|rocky|almalinux)
      ;;
    *)
      if [[ "${ID_LIKE:-}" != *rhel* ]] && [[ "${ID_LIKE:-}" != *centos* ]]; then
        die "This script currently supports CentOS/RHEL compatible systems only."
      fi
      ;;
  esac

  log "Detected ${PRETTY_NAME:-${ID:-CentOS-like Linux}}"
  if [[ -n "${VERSION_ID:-}" ]] && [[ "${VERSION_ID}" != 8* ]] && [[ "${VERSION_ID}" != 9* ]]; then
    warn "This script is primarily validated for CentOS/RHEL 8/9 style systems (kernel 4.18 commonly used on 8.x). Continuing anyway."
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
  local missing_items=()

  command -v python3 >/dev/null 2>&1 || missing_items+=("python3")

  if ! pkg-config --exists Qt5Widgets Qt5Network 2>/dev/null; then
    warn "Qt5 pkg-config metadata is not currently visible. Runtime may still work if Qt5 runtime packages are installed."
  fi

  if ((${#missing_items[@]} > 0)); then
    die "Runtime verification failed: ${missing_items[*]}"
  fi

  log "Runtime command verification passed."
}

verify_build() {
  local missing_items=()
  local moc_path=""

  command -v g++ >/dev/null 2>&1 || missing_items+=("g++")
  command -v make >/dev/null 2>&1 || missing_items+=("make")
  command -v pkg-config >/dev/null 2>&1 || missing_items+=("pkg-config")

  if command -v moc >/dev/null 2>&1; then
    moc_path="$(command -v moc)"
  elif command -v moc-qt5 >/dev/null 2>&1; then
    moc_path="$(command -v moc-qt5)"
  elif [[ -x /usr/lib64/qt5/bin/moc ]]; then
    moc_path="/usr/lib64/qt5/bin/moc"
  fi

  [[ -n "${moc_path}" ]] || missing_items+=("moc")
  pkg-config --exists Qt5Widgets Qt5Network || missing_items+=("Qt5 pkg-config metadata")

  if ((${#missing_items[@]} > 0)); then
    die "Build dependency verification failed: ${missing_items[*]}"
  fi

  log "Build dependency verification passed."
}

maybe_enable_powertools() {
  if [[ "${PKG_MGR}" != "dnf" ]]; then
    return
  fi

  if command -v dnf >/dev/null 2>&1; then
    ${SUDO} dnf config-manager --set-enabled powertools >/dev/null 2>&1 || true
    ${SUDO} dnf config-manager --set-enabled PowerTools >/dev/null 2>&1 || true
    ${SUDO} dnf config-manager --set-enabled crb >/dev/null 2>&1 || true
  fi
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
  require_centos_like
  detect_pkg_manager
  SUDO="$(sudo_prefix)"

  maybe_enable_powertools

  log "Installing runtime dependencies ..."
  install_packages "${RUNTIME_PACKAGES[@]}"
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
