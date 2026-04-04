# Git GUI 客户端

一个轻量级的 Git 图形化客户端，使用 C 语言和 Win32 API 开发，无需任何外部依赖。

## 功能特性

- **仓库管理** - 选择/切换 Git 仓库
- **文件状态** - 查看已修改、已暂存、未跟踪的文件
- **暂存操作** - 暂存选中文件或全部暂存
- **提交更改** - 输入提交信息并提交
- **推送/拉取** - Push 和 Pull 远程仓库
- **日志查看** - 查看提交历史和文件变更
- **差异对比** - 集成 Beyond Compare 查看文件差异
- **分支操作** - 创建分支、回退版本
- **右键菜单** - 文件回退、取消暂存、删除等操作

## 系统要求

- Windows 7 或更高版本
- Git 已安装并添加到 PATH

## 编译

需要 MinGW-w64 GCC 编译器。

```bash
# 编译
make -f Makefile

# 清理
make -f Makefile clean

# 重新编译
make -f Makefile rebuild
```

## 使用

1. 运行 `git-gui.exe`
2. 点击「选择仓库」打开 Git 仓库目录
3. 在「状态」标签页查看文件变更
4. 暂存文件后输入提交信息，点击「提交更改」
5. 使用「推送」「拉取」同步远程仓库

## 配置

程序会在同目录下生成 `git-gui.ini` 配置文件，可配置：

- `GitPath` - git.exe 路径（默认使用 PATH 中的 git）
- `BComparePath` - Beyond Compare 路径（用于文件对比）
- `LastRepo` - 上次打开的仓库路径

也可以通过程序内的「设置」按钮进行配置。

## 快捷操作

### 文件列表右键菜单

- 查看改动 - 使用 Beyond Compare 对比差异
- 回退文件改动 - 撤销工作区修改
- 暂存/取消暂存 - 添加或移出暂存区
- 删除文件 - 删除工作区文件

### 日志列表右键菜单

- 回退到此版本 - 硬重置到选定提交
- 从此版本创建分支 - 基于选定提交创建新分支
- 查看操作记录 - 显示 reflog
- 复制哈希值 - 复制 commit hash 到剪贴板

## 项目结构

```
git-gui/
├── main.c              # 主程序入口、窗口创建
├── git_operations.c    # Git 命令封装
├── git_operations.h    # Git 操作接口定义
├── ui_callbacks.c      # UI 回调函数、事件处理
├── ui_callbacks.h      # UI 接口定义
├── resource.h          # 资源 ID 定义
├── Makefile            # 编译配置
└── README.md           # 说明文档
```

## 技术特点

- 纯 Win32 API，无第三方 UI 框架依赖
- 支持 Unicode 和 UTF-8 编码
- 高 DPI 显示支持
- 异步 Git 命令执行，UI 不阻塞

## 许可证

MIT License
