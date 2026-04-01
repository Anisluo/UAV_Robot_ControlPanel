# UAV_Robot Control Panel

`HostGUI` 是一个基于 Qt 的上位机控制界面，用于连接无人机/机器人平台，完成 RPC 控制、视频显示、任务配置和状态监控。

## 当前状态

项目最初主要按 Linux 环境组织，仓库内保留了原有 `Makefile` 和 Linux 安装脚本。

现在仓库已经补充了 Windows 适配所需的关键改动：

- 新增 `CMakeLists.txt`，可用于 Windows 上通过 CMake 构建
- `MeshPinger` 已适配 Windows 和 Linux 不同的 `ping` 参数
- Python 中继进程已兼容 `python3`、`python`、`py` 三种常见启动方式
- 保留 Linux 构建方式，不影响原有使用流程

## 目录结构

```text
UAV_Robot_ControlPanel/
|- CMakeLists.txt
|- Makefile
|- src/
|  |- core/
|  |- gui/
|  `- main.cpp
|- tools/
`- dist/
```

## 依赖

### Linux

- `g++`，支持 C++17
- `pkg-config`
- Qt5 Widgets / Network 开发包

### Windows

- CMake 3.16+
- Qt 5 或 Qt 6
- 一套可用的 C++ 编译器
  - 推荐 Visual Studio 2022
  - 或 MinGW-w64

## Linux 构建

在项目根目录执行：

```bash
make
```

输出文件：

```text
build/bin/HostGUI
```

## Windows 构建

推荐使用 “x64 Native Tools Command Prompt for VS 2022” 或已配置好 Qt/CMake 的 PowerShell。

### 1. 配置工程

Qt 6 示例：

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.6.3\msvc2019_64"
```

Qt 5 示例：

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64"
```

如果你使用 MinGW，可以改成对应的生成器和 Qt MinGW 套件路径。

### 2. 编译

```powershell
cmake --build build-windows --config Release
```

可执行文件通常位于：

```text
build-windows\Release\HostGUI.exe
```

### 3. 部署 Qt 运行库

如果系统中可找到 `windeployqt`，仓库里的 CMake 目标会自动提供 `deploy_windows`：

```powershell
cmake --build build-windows --config Release --target deploy_windows
```

如果没有这个目标，也可以手动执行：

```powershell
windeployqt build-windows\Release\HostGUI.exe
```

## 运行

程序默认使用：

- 主机地址：`192.168.1.101`
- RPC 端口：`7001`
- 视频端口：`7002`

启动后可以在主界面中修改地址和端口。

## 已知说明

- `MeshPinger` 在 Windows 上依赖系统自带的 `ping.exe`
- 中继逻辑依赖本机存在可调用的 Python 解释器
- 当前机器如果没有安装 Qt / CMake / 编译器，将无法直接本地编译
- 仓库中部分界面文案仍存在历史编码问题，不影响这次 Windows 构建链适配，但后续建议统一转成 UTF-8

## Linux 发布脚本

如果需要打 Linux 客户发布包，仍可使用：

```bash
./tools/package_release.sh
```
