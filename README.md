# listtree — 美观多彩的终端目录树查看器

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**listtree** 是一个用 C++20 编写的命令行工具，能够在终端中以**树形结构**美观地显示目录内容。支持**实时监控**文件变动、**交互模式**（方向键导航、展开/折叠、创建/删除文件）、**tmux 窗格集成**、**多彩着色**、**文件类型图标**等特性。

---

## 📸 效果预览

```
  📂 目录树: /home/user/projects/myapp
  ─────────────────────────────────────────────

  📁 .
  ├── 📁 src · 2026-07-21 11:55
  │   ├── 🅲 main.cpp · 13.2 KB · 2026-07-21 11:56
  │   ├── 🅲 utils.cpp · 4.1 KB · 2026-07-21 10:30
  │   └── 🅲 utils.h · 1.2 KB · 2026-07-21 10:28
  ├── 📁 assets · 2026-07-20 09:15
  │   ├── 🖼️ logo.png · 256.0 KB · 2026-07-20 09:15
  │   └── 🎵 bgm.mp3 · 1.2 MB · 2026-07-19 18:00
  ├── 📋 package.json · 342 B · 2026-07-21 09:00
  ├── 📝 README.md · 5.6 KB · 2026-07-21 08:45
  ├── 🔒 package-lock.json · 89.2 KB · 2026-07-21 09:00
  └── ▶️ build.sh · 245 B · 2026-07-20 14:30

  ── 统计 ──
  📁 目录: 2
  📄 文件: 7
  💾 总计: 1.6 MB
```

---

## ✨ 功能特性

### 🎨 多彩输出
每种文件类型都有专属颜色，一目了然：

| 类型 | 颜色 | 示例 |
|------|------|------|
| 📁 目录 | **亮蓝色加粗** | `src` |
| 📄 普通文件 | 浅灰色 | `notes.txt` |
| ▶️ 可执行文件 | **亮绿色加粗** | `build.sh` |
| 🔗 符号链接 | *青色斜体* | `link → target` |
| 🙈 隐藏文件 | *暗灰色斜体* | `.gitignore` |
| 📊 文件大小 | 金色 | `13.2 KB` |
| 🕐 修改时间 | 小麦色 | `2026-07-21 11:56` |
| 📊 统计信息 | **黄色加粗** | `📁 目录: 2` |

### 🏷️ 智能文件图标
自动识别 **50+ 种**文件类型并显示对应图标：

| 扩展名 | 图标 | 说明 |
|--------|------|------|
| `.cpp/.c/.h/.hpp` | 🅲 | C/C++ |
| `.py` | 🐍 | Python |
| `.java` | ☕ | Java |
| `.rs` | 🦀 | Rust |
| `.go` | 🔵 | Go |
| `.js/.ts/.mjs` | 🟨 | JavaScript/TypeScript |
| `.html/.htm` | 🌐 | HTML |
| `.css/.scss/.less` | 🎨 | 样式表 |
| `.json` | 📋 | JSON |
| `.xml` | 📄 | XML |
| `.yaml/.yml` | ⚙️ | YAML |
| `.md/.markdown` | 📝 | Markdown |
| `.txt` | 📃 | 文本文件 |
| `.png/.jpg/.gif/.bmp/.webp` | 🖼️ | 图片 |
| `.svg` | ✏️ | SVG 矢量图 |
| `.mp3/.wav/.flac/.ogg` | 🎵 | 音频 |
| `.mp4/.avi/.mkv/.mov` | 🎬 | 视频 |
| `.zip/.tar/.gz/.bz2/.7z/.rar` | 📦 | 压缩包 |
| `.pdf` | 📕 | PDF |
| `.sh/.bash/.zsh` | ⚡ | Shell 脚本 |
| `.so/.dll/.dylib` | 🔧 | 动态库 |
| `.o/.obj` | 🧱 | 目标文件 |
| `.exe/.out` | ▶️ | 可执行文件 |
| `.toml` | 🔩 | TOML |
| `.lock` | 🔒 | 锁文件 |
| `.gitignore/.gitattributes/.gitmodules` | 🙈 | Git 忽略文件 |
| `.env` | 🔐 | 环境变量 |
| `.config/.conf/.ini` | ⚙️ | 配置文件 |
| `.log` | 📋 | 日志文件 |
| `.csv` | 📊 | 表格数据 |
| `.sql/.db/.sqlite` | 🗄️ | 数据库 |
| `.vim/.vimrc` | ✏️ | Vim 配置 |
| `.dockerfile` | 🐳 | Docker |
| ... 更多 | ... | ... |

### 🖥️ 交互模式（Interactive Mode）
使用方向键在文件树中导航，支持丰富的键盘操作。**注意：交互模式打开文件时仅支持 `micro` 编辑器**，不支持 vim/nano/emacs 等其他编辑器。

| 按键 | 功能 |
|------|------|
| `↑` / `↓` | 移动光标 |
| `Space` | **目录**: 展开/折叠（若指定 `-c` 则同时在 tmux 窗格中 `cd` 到该目录） |
| `Space` | **文件**: 在 tmux 窗格中用 `micro` 编辑器打开（文档）、显示内容（压缩包）、执行（可执行文件） |
| `m` | 在光标所在目录下创建新目录 |
| `n` | 在光标所在目录下用 `micro` 编辑器创建新文件 |
| `d` | 递归删除光标指向的目录或文件（需确认 `y/n`） |
| `u` | 在 cd 窗格中 `cd` 到根目录 |
| `q` / `ESC` | 退出交互模式 |

交互模式自动通过 `inotify` 监听文件变动，有变化时自动刷新显示。

### 🔗 tmux 窗格集成
支持将操作发送到指定的 tmux 窗格：

- **`-p` / `--pane`**: 指定用于打开文件/执行命令的 tmux 窗格（支持逗号分隔的多个窗格）
- **`-c` / `--cd-pane`**: 指定用于 `cd` 到目录的 tmux 窗格（可与 `-p` 不同）

智能中断检测：打开文件前自动检测窗格是否正在运行程序（如 `micro`、`vim`），并发送合适的退出命令（`Ctrl+Q` / `Ctrl+C`）。

### 👁️ 实时监控模式（Watch Mode）
使用 Linux `inotify` 机制监听文件系统事件，**自动刷新**显示：

- 文件 **创建** → 自动显示 `+ filename`
- 文件 **删除** → 自动显示 `- filename`
- 文件 **移动/重命名** → 自动更新
- 新目录创建 → 自动添加子目录监控
- **防抖机制**（200ms 间隔），避免频繁刷新
- 按 `Ctrl+C` 优雅退出

### 📊 统计信息
显示目录树底部的汇总统计：
- 📁 目录数量
- 📄 文件数量
- 🔗 符号链接数量
- 🙈 隐藏文件数量
- 💾 总文件大小

### 📐 智能排序
- **目录在前，文件在后**
- 各自按名称**字母顺序**排序
- 根目录始终显示为 `.`

---

## 📦 安装

### 依赖

- **C++20 编译器**（GCC 11+ / Clang 14+）
- **fmt 库**（>= 10.0）
- **CMake**（>= 3.16，可选）
- Linux 内核（需要 `inotify` 支持，用于监控模式和交互模式）

#### 在 Termux (Android) 上安装依赖

```bash
pkg update
pkg install gcc cmake fmt
```

#### 在 Ubuntu/Debian 上安装依赖

```bash
sudo apt update
sudo apt install g++ cmake libfmt-dev
```

#### 在 Arch Linux 上安装依赖

```bash
sudo pacman -S gcc cmake fmt
```

#### 在 macOS 上安装依赖（使用 Homebrew）

```bash
brew install gcc cmake fmt
```

> **注意**：macOS 不支持 `inotify`，监控模式（`-w`）和交互模式（`-i`）在 macOS 上不可用。

### 编译安装

#### 方法一：直接编译（推荐）

```bash
# 克隆或下载源码
git clone https://github.com/yourname/listtree.git
cd listtree

# 编译
g++ -std=c++20 src/main.cpp -o listtree -lfmt -O2

# 安装到系统 PATH
sudo cp listtree /usr/local/bin/
```

#### 方法二：使用 CMake

```bash
mkdir build && cd build
cmake ..
make
sudo make install
```

---

## 🚀 用法

### 基本用法

```bash
# 显示当前目录（仅一级）
listtree

# 显示指定目录
listtree /path/to/directory

# 递归显示子目录
listtree -r

# 显示隐藏文件（以 . 开头的文件/目录）
listtree -a

# 组合使用
listtree -r -a /home/user/projects
```

### 实时监控模式

```bash
# 实时监控当前目录
listtree -w

# 实时监控并递归子目录
listtree -w -r

# 实时监控（含隐藏文件）
listtree -w -r -a

# 监控指定目录
listtree -w -r /path/to/watch
```

监控模式下，终端会**持续显示**目录树，并在文件变动时**自动刷新**。按 `Ctrl+C` 退出。

### 交互模式

```bash
# 基本交互模式
listtree -i

# 交互模式 + 递归显示
listtree -i -r

# 交互模式 + tmux 窗格集成（窗格 0 用于打开文件）
listtree -i -p 0

# 交互模式 + 多个 tmux 窗格
listtree -i -p 0,1

# 交互模式 + 指定窗格名称
listtree -i -p mypane:1

# 交互模式 + 分离的 cd 窗格（窗格 0 打开文件，窗格 1 用于 cd）
listtree -i -p 0 -c 1

# 交互模式 + 多个文件窗格 + 独立 cd 窗格
listtree -i -p 0,1 -c 2
```

### 帮助信息

```bash
listtree -h
```

输出：

```
listtree v2.0.0 - 实时目录树查看器
在终端中美观地显示目录树结构，支持实时监控文件变动

用法:
  listtree [选项...] [目录路径]

选项:
  -r, --recursive    递归显示子目录
  -a, --all          显示隐藏文件
  -w, --watch        实时监控模式，文件变动自动刷新
  -i, --interactive  交互模式，支持方向键导航和空格操作
  -p, --pane ID      指定 tmux 窗格 ID（多个用逗号分隔，用于打开文件/执行）
  -c, --cd-pane ID   指定 tmux 窗格 ID（多个用逗号分隔，用于 cd 到目录）
  -h, --help         显示此帮助信息

交互模式操作:
  ↑/↓        移动光标
  Space      目录:展开/折叠（若指定 -c 则同时在 cd 窗格中 cd 到该目录）
             文件:在 tmux 窗格中打开（若窗格正在运行程序则先中断）
  m [空格]   在光标所在目录下创建新目录（输入目录名后按 Enter）
  n [空格]   在光标所在目录下用 micro 编辑器创建新文件（输入文件名后按 Enter）
  d          递归删除光标指向的目录或文件（需确认 y/n）
  u          在 cd 窗格中 cd 到最上层目录（根目录）
  q/ESC      退出交互模式
  💡 交互模式自动监听文件变动，有变化时自动刷新

示例:
  listtree
  listtree -r ~/code
  listtree -r -a .
  listtree -w -r .          # 实时监控当前目录
  listtree -w -r -a /path   # 实时监控（含隐藏文件）
  listtree -i               # 交互模式
  listtree -i -p 0          # 交互模式，tmux 窗格 0
  listtree -i -p 0,1        # 交互模式，tmux 窗格 0 和 1
  listtree -i -p mypane:1   # 交互模式，指定 tmux 窗格
  listtree -i -p 0 -c 1     # 交互模式，窗格0打开文件，窗格1 cd 到目录
  listtree -i -p 0,1 -c 2   # 交互模式，窗格0,1打开文件，窗格2 cd

💡 提示: 按 Ctrl+C 退出监控模式
```

---

## 🔧 命令行选项汇总

| 选项 | 长选项 | 说明 |
|------|--------|------|
| `-r` | `--recursive` | 递归显示所有子目录的内容 |
| `-a` | `--all` | 显示隐藏文件（以 `.` 开头的文件/目录） |
| `-w` | `--watch` | 实时监控模式，持续显示并自动刷新 |
| `-i` | `--interactive` | 交互模式，支持方向键导航和空格操作 |
| `-p` | `--pane` | 指定 tmux 窗格 ID（多个用逗号分隔） |
| `-c` | `--cd-pane` | 指定 tmux 窗格 ID 用于 cd 到目录 |
| `-h` | `--help` | 显示帮助信息 |

---

## 🏗️ 项目结构

```
listtree/
├── CMakeLists.txt      # CMake 构建配置
├── src/
│   └── main.cpp        # 主程序源码（所有功能）
├── listtree            # 编译后的可执行文件
└── README.md           # 本文件
```

---

## ⚙️ 技术细节

- **语言标准**: C++20
- **文件系统**: `std::filesystem`（C++17 标准库）
- **格式化/着色**: `fmt` 库（`fmt::color` 支持 24-bit 真彩色）
- **文件监控**: Linux `inotify` 系统调用
- **终端控制**: ANSI 转义序列（清屏、光标控制、原始模式）
- **排序**: 目录优先，按名称字母排序
- **交互输入**: 终端原始模式 + `poll` 多路复用（同时监听键盘和文件事件）

### 跨平台兼容性

| 平台 | 一次性模式 | 监控模式 | 交互模式 |
|------|-----------|---------|---------|
| Linux | ✅ | ✅（需要 inotify）| ✅ |
| Android (Termux) | ✅ | ✅ | ✅ |
| macOS | ✅ | ❌ | ❌ |
| Windows (WSL) | ✅ | ✅ | ✅ |

---

## 📄 许可证

本项目采用 MIT 许可证。详见 [LICENSE](LICENSE) 文件。

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 创建 Pull Request