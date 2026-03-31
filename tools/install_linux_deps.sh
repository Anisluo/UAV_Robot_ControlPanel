#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

die() {
  printf '[HostGUI setup] Error: %s\n' "$*" >&2
  exit 1
}

[[ -f /etc/os-release ]] || die "Cannot detect operating system."
# shellcheck disable=SC1091
source /etc/os-release

case "${ID:-}" in
  ubuntu)
    exec "${SCRIPT_DIR}/install_ubuntu_deps.sh" "$@"
    ;;
  centos|rhel|rocky|almalinux)
    exec "${SCRIPT_DIR}/install_centos_deps.sh" "$@"
    ;;
  *)
    if [[ "${ID_LIKE:-}" == *rhel* ]] || [[ "${ID_LIKE:-}" == *centos* ]]; then
      exec "${SCRIPT_DIR}/install_centos_deps.sh" "$@"
    fi
    die "Unsupported distribution: ${PRETTY_NAME:-${ID:-unknown}}"
    ;;
esac
