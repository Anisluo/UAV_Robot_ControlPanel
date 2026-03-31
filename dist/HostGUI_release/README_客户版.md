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
