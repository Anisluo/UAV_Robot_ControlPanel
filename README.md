# UAV_Robot Control Panel

`HostGUI` 是一个基于 Qt5 的上位机控制终端，用于连接无人机/机器人平台，完成 RPC 控制、视频显示和多模块状态监控。

## 功能概览

- 主控面板，集中展示连接状态、视频画面和日志输出
- RPC 通信客户端，用于向机器人侧发送控制指令
- 视频客户端，用于接收实时视频流并显示帧率
- 子系统控制组件，包括机械臂、UGV、机场、夹爪、网格地图和无人机模块
- 接口配置页和帮助页，便于调试与联调

## 目录结构

```text
HostGUI/
├── Makefile
├── src/
│   ├── core/     # RPC / Video / Protocol
│   ├── gui/      # 主窗口与各功能组件
│   └── main.cpp  # 程序入口
└── build/        # 编译输出目录（默认不纳入版本管理）
```

## 环境依赖

- Ubuntu / Linux
- `g++`，支持 C++17
- `pkg-config`
- `Qt5Widgets`
- `Qt5Network`

可使用以下命令安装基础依赖：

```bash
make install-deps
```

## 编译

在 `HostGUI` 目录下执行：

```bash
make
```

编译完成后，程序输出到：

```bash
build/bin/HostGUI
```

## 运行

```bash
./build/bin/HostGUI
```

程序默认提供：

- 主机地址：`192.168.10.2`
- RPC 端口：来自 `Protocol::RPC_PORT`
- 视频端口：来自 `Protocol::VIDEO_PORT`

启动后可在主界面中设置目标主机与端口，连接机器人控制端与视频流服务。

## Ubuntu 自动安装脚本

如果未来需要把 `HostGUI` 部署到用户的 Ubuntu 主机上，可以直接使用脚本自动检测并安装依赖。

仅安装运行依赖：

```bash
./tools/install_ubuntu_deps.sh
```

安装运行依赖和编译依赖：

```bash
./tools/install_ubuntu_deps.sh --with-build-deps
```

安装依赖并直接编译：

```bash
./tools/install_ubuntu_deps.sh --with-build-deps --build
```

该脚本当前主要面向 Ubuntu `20.04` 和 `22.04`。

## 清理构建产物

```bash
make clean
```

## 说明

- 当前仓库仅包含上位机 GUI 部分
- `build/` 目录下的二进制和中间产物已通过 `.gitignore` 排除
