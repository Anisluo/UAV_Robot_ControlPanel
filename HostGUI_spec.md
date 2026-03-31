# HostGUI 规格说明

## 1. 项目定位

`HostGUI` 是面向无人机/机器人系统的上位机控制终端，运行在 Linux 桌面环境中（Ubuntu / CentOS / RHEL），用于：

- 连接 `UAV_Robot` 侧 `proc_gateway`
- 进行 RPC 指令下发
- 接收并显示 `7002/TCP` 视频流
- 展示日志、设备状态和任务控制界面

---

## 2. 运行环境

当前推荐运行平台：

- Ubuntu 20.04
- Ubuntu 22.04
- CentOS / RHEL 8.x（常见内核版本为 4.18）

运行时依赖主要包括：

- `python3`
- `libqt5core5a`
- `libqt5gui5`
- `libqt5widgets5`
- `libqt5network5`
- `qt5-gtk-platformtheme`
- `qtwayland5`

---

## 3. 发布原则

`HostGUI` 面向客户交付时，**不提供源码仓库**，仅提供运行发布包。

发布目标：

- 客户无需获取 `src/`、`Makefile`、`.git/`
- 客户只需要安装运行依赖即可启动
- 客户使用步骤尽量压缩为两条命令

因此当前采用的交付方式为：

- 提供一个不含源码的发布目录
- 发布目录中只保留运行所需二进制、依赖安装脚本和使用说明

---

## 4. 当前推荐发布方式

### 4.1 发布目录生成

在开发机 `HostGUI` 项目根目录执行：

```bash
./tools/package_release.sh
```

该脚本会自动完成：

1. 清理旧构建产物
2. 重新编译 `HostGUI`
3. 生成客户发布目录

输出目录固定为：

```text
dist/HostGUI_release/
```

### 4.2 发布目录内容

当前发布目录默认包含：

```text
HostGUI_release/
├── HostGUI
├── install_ubuntu_deps.sh
├── install_centos_deps.sh
└── README_客户版.md
```

各文件职责如下：

- `HostGUI`
  - 已编译好的可执行文件
- `install_ubuntu_deps.sh`
  - 客户机运行依赖自动检测与安装脚本
- `install_centos_deps.sh`
  - CentOS / RHEL 客户机运行依赖自动检测与安装脚本
- `README_客户版.md`
  - 面向客户的最小化使用说明

---

## 5. 客户部署方式

客户拿到 `HostGUI_release/` 目录后，在该目录下执行：

```bash
chmod +x install_ubuntu_deps.sh install_centos_deps.sh HostGUI

# Ubuntu
./install_ubuntu_deps.sh

# CentOS / RHEL
./install_centos_deps.sh

./HostGUI
```

说明：

- 第一步确保脚本和二进制具备执行权限
- 第二步自动安装 Ubuntu 运行依赖
- 若为 CentOS / RHEL，则执行对应的 CentOS 依赖安装脚本
- 第三步直接启动 GUI

客户侧不需要：

- 编译源码
- 安装开发头文件
- 进入源码工程目录

---

## 6. 相关脚本说明

### 6.1 开发侧脚本

开发仓库中保留以下脚本：

- `tools/install_ubuntu_deps.sh`
  - 面向开发环境
  - 支持运行依赖安装、编译依赖安装和直接编译
- `tools/install_centos_deps.sh`
  - 面向 CentOS / RHEL 开发环境
  - 支持运行依赖安装、编译依赖安装和直接编译
- `tools/package_release.sh`
  - 用于生成客户发布目录

### 6.2 客户侧脚本

客户发布目录中使用：

- `install_ubuntu_deps.sh`
- `install_centos_deps.sh`

这些文件分别由开发侧脚本 `tools/install_ubuntu_runtime.sh` 与 `tools/install_centos_runtime.sh` 复制生成，职责仅限：

- 检测 Ubuntu 环境
- 或检测 CentOS / RHEL 环境
- 安装运行依赖
- 提示客户执行 `./HostGUI`

它不负责：

- 拉取源码
- 编译项目
- 安装开发工具链

---

## 7. 交付建议

实际发给客户时，建议直接打包：

```text
dist/HostGUI_release/
```

可选分发形式：

- `zip`
- `tar.gz`
- 企业内部文件分发平台目录包

推荐要求客户：

- 使用带 `sudo` 权限的 Ubuntu 账户执行依赖安装
- 或使用带 `sudo` 权限的 CentOS / RHEL 账户执行依赖安装
- 优先在 Ubuntu 20.04 / 22.04 或 CentOS / RHEL 8.x 上运行

---

## 8. 当前结论

`HostGUI` 当前正式推荐的客户交付方式为：

- 开发侧执行 `./tools/package_release.sh`
- 将 `dist/HostGUI_release/` 整体发给客户
- 客户执行：

```bash
# Ubuntu
./install_ubuntu_deps.sh

# CentOS / RHEL
./install_centos_deps.sh

./HostGUI
```

该方式满足以下目标：

- 不暴露源码
- 部署步骤简单
- 与当前 Ubuntu / CentOS 客户环境兼容
