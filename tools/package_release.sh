#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${PROJECT_ROOT}/dist/HostGUI_release"

log() {
  printf '[HostGUI release] %s\n' "$*"
}

die() {
  printf '[HostGUI release] Error: %s\n' "$*" >&2
  exit 1
}

require_file() {
  [[ -f "$1" ]] || die "Missing required file: $1"
}

write_customer_readme() {
  cat >"${DIST_DIR}/README_客户版.md" <<'EOF'
# HostGUI 客户使用说明

## 支持系统

- Ubuntu 20.04
- Ubuntu 22.04
- CentOS / RHEL 8.x（常见内核版本为 4.18）

## 使用步骤

在当前目录打开终端后，依次执行：

```bash
chmod +x HostGUI install_ubuntu_deps.sh install_centos_deps.sh

# Ubuntu 20.04 / 22.04
./install_ubuntu_deps.sh

# CentOS / RHEL 8.x
./install_centos_deps.sh

./HostGUI
```

## 说明

- `install_ubuntu_deps.sh` 会自动检测 Ubuntu 环境并安装运行依赖
- `install_centos_deps.sh` 会自动检测 CentOS/RHEL 环境并安装运行依赖
- `HostGUI` 为可直接运行的上位机程序
- 如系统提示权限不足，请使用有 sudo 权限的用户执行依赖安装
EOF
}

main() {
  require_file "${PROJECT_ROOT}/Makefile"
  require_file "${PROJECT_ROOT}/tools/install_ubuntu_runtime.sh"
  require_file "${PROJECT_ROOT}/tools/install_centos_runtime.sh"

  log "Building release binary ..."
  make -C "${PROJECT_ROOT}" clean
  make -C "${PROJECT_ROOT}" -j"$(nproc)"

  require_file "${PROJECT_ROOT}/build/bin/HostGUI"

  log "Preparing release directory ..."
  rm -rf "${DIST_DIR}"
  mkdir -p "${DIST_DIR}"

  install -m 0755 "${PROJECT_ROOT}/build/bin/HostGUI" "${DIST_DIR}/HostGUI"
  install -m 0755 "${PROJECT_ROOT}/tools/install_ubuntu_runtime.sh" "${DIST_DIR}/install_ubuntu_deps.sh"
  install -m 0755 "${PROJECT_ROOT}/tools/install_centos_runtime.sh" "${DIST_DIR}/install_centos_deps.sh"
  install -m 0755 "${PROJECT_ROOT}/tools/install_linux_deps.sh" "${DIST_DIR}/install_linux_deps.sh"

  write_customer_readme

  log "Release package ready: ${DIST_DIR}"
  log "Customer usage:"
  log "  cd HostGUI_release"
  log "  ./install_centos_deps.sh   # CentOS / RHEL"
  log "  ./install_ubuntu_deps.sh   # Ubuntu"
  log "  ./HostGUI"
}

main "$@"
