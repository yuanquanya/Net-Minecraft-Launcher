<<<<<<< HEAD
# Net-Minecraft-Launcher
=======
# Net Minecraft Launcher
>>>>>>> 507621b1b2f96ac00e7dbd87dff6907d9f98c20b

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

**Net-Minecraft-Launcher** 是一个基于 C++ (Qt) 和 Web 技术构建的现代化、轻量级 Minecraft 启动器。它结合了原生应用的性能与 Web界面的灵活性，旨在提供快速、纯粹的游戏启动体验。


## ✨ 主要特性 (Features)

*   **极速核心**: 采用 C++ 编写底层逻辑，资源占用低，启动速度快。
*   **现代化 UI**: 基于 HTML/CSS/JS 构建的单页应用 (SPA) 界面，提供流畅的交互体验。
    *   **主页**: 快速选择版本并启动游戏。
    *   **设置**: 自定义离线用户名、游戏内存分配、Java 路径等。
    *   **用户**: 简单的用户配置文件管理。
*   **智能环境管理**:
    *   自动检测系统中的 Java 环境。
    *   **自动下载**: 内置 Java 8 和 Java 17 自动下载功能（使用 BMCLAPI 镜像加速），解决国内下载困难问题。
*   **离线模式**: 支持离线账户登录，方便内网或无网络环境使用。
*   **版本隔离**: 采用标准的 `.minecraft` 目录结构，支持版本隔离。

## 🛠️ 构建指南 (Build Instructions)

本项目依赖 **Qt 6**、**libcurl** 和 **nlohmann_json**。

### 1. 环境准备

#### 方式 A: 使用 Qt 官方安装程序 (推荐)
1.  前往 [Qt 官网](https://www.qt.io/download-qt-installer) 下载在线安装程序。
2.  安装 **Qt 6.x** (例如 Qt 6.6 或 6.7) 以及对应的编译器组件 (如 **MSVC 2019 64-bit** 或 **MinGW**)。
3.  记下 Qt 的安装路径，例如 `C:\Qt\6.6.0\msvc2019_64`。

#### 方式 B: 使用 vcpkg
如果您熟悉 vcpkg，可以使用以下命令安装依赖：
```powershell
vcpkg install qt6:x64-windows curl:x64-windows nlohmann-json:x64-windows
```

### 2. 配置与编译

打开终端 (Terminal) 或 PowerShell，进入项目根目录并运行以下命令：

```powershell
# 1. 配置项目 (请将 <path_to_qt> 替换为您的实际 Qt 安装路径)
# 示例: cmake -B build -S . -DCMAKE_PREFIX_PATH="C:\Qt\6.6.0\msvc2019_64"
cmake -B build -S . -DCMAKE_PREFIX_PATH="<您的Qt安装路径>"

# 2. 编译项目 (Release 模式)
cmake --build build --config Release
```

### 3. 运行

编译成功后，可执行文件将位于 `build/NetMinecraftLauncher.exe` (MinGW/Ninja) 或 `build/Release/NetMinecraftLauncher.exe` (MSVC)。

首次运行时，程序会在当前目录下创建 `.minecraft` 文件夹用于存放游戏数据。

```
运行 `NetMinecraftLauncher.exe`
```


## 📂 项目结构

*   `src/`: C++ 源代码 (核心逻辑、HTTP 服务、Qt 窗口)。
*   `src/WebContent.h`: 前端资源 (HTML/CSS/JS) 内嵌文件。
*   `index.html`: 项目介绍页 (独立文件)。

## 📝 许可 (License)

本项目采用 MIT 许可证开源。详情请参阅 [LICENSE](LICENSE) 文件。
