# VultraEngine 入门指南

[English](../getting_started.md) | **简体中文**

欢迎！本指南将带你从全新的构建一路走到运行你的第一个场景。它涵盖了 `vultra`
可执行文件的作用、如何启动它，以及如何在编辑器中创建或打开项目。

> 想从源码构建引擎？请参阅 [BUILD.md](BUILD_CN.md)。

## 你得到的：一个可执行文件，多种角色

VultraEngine 以单一可执行文件的形式发布。构建目标是 `vultra-app`，所生成的二进制
文件名为 `vultra`（在 Windows 上为 `vultra.exe`）。这个程序同时是你的：

- **项目启动器** —— 创建、打开和管理项目。
- **编辑器** —— 编写场景、材质、渲染图和脚本。
- **运行时播放器** —— 运行一个项目或一个打包好的游戏。
- **工具宿主** —— 集成的资产导入器、打包器和着色器编译器（以子命令方式运行；见
  下文）。

它如何决定扮演哪个角色，取决于它在启动时所发现的内容：

- **从松散文件编辑。** 当你打开一个项目（一个包含 `.vproject` 文件的目录）时，
  编辑器会直接从项目的资产根目录（默认为 `resources/`）以磁盘上的普通文件形式读取
  你的资产。编辑一个场景、着色器或材质，改动就直接落在磁盘上 —— 非常适合迭代。
- **运行打包好的 `resources.vpk`。** 为了分发，项目会被导出为一个 `.vpk` 包（一个
  打包的、只读的虚拟文件系统，挂载在 `res://`）。启动时运行时会查找 VPK —— 首先是
  可执行文件旁边的 `vultra.vpk`，然后是工作目录或项目中的 `resources.vpk` ——
  如果找到了，它就直接启动进入运行时模式并播放打包好的游戏。没有编辑器，没有松散
  文件。

如果未找到 VPK 且未指定项目，`vultra` 会打开项目启动器。

## 启动：CLI 模式与标志

运行 `vultra help`（或 `vultra --help`）可打印用法。下面的标志按用途分组。

### 编辑器 / 运行时

- `--editor` —— 以编辑器模式启动。**需要 `--project`**；没有项目的编辑器会话是
  无效的（它会回退到启动器）。
- `--project <dir-or-.vproject>` —— 要打开的项目。接受项目目录或其中的 `.vproject`
  文件。
- `--scene <res://...>` —— 要加载的场景（例如 `res://scenes/main.vscn`）。省略时，
  使用项目的默认场景，并回退到 `res://scenes/main.vscn`。
- `--vpk <file>` —— 加载并运行一个特定的 `.vpk` 包。
- `--render-mode <visible|offscreen|none>` —— `visible`（默认）显示一个窗口；
  `offscreen` 隐藏窗口但保持渲染处于活动状态（对捕获很有用）；`none` 以无渲染的
  方式运行无头模拟。
- `--plugins-dir <dir>` —— 在运行时模式下，发现并启用此目录中的每一个插件。
  （在编辑器模式下，改为由项目的已启用插件列表来驱动加载。）

> `--backend` / `--render-backend` 和 `--render-profile` 在命令行上是被接受的，
> 但目前只是占位符 —— 它们尚未改变任何行为。

### XR

- `--xr` / `--no-xr` —— 请求或禁用 XR。XR 仅在 Vulkan 后端上受支持；在其他后端
  上请求它会记录一条警告。
- `--xr-mirror` / `--no-xr-mirror` —— 切换是否将头显视图镜像到桌面窗口。

### 调试

- `--validation` / `--no-validation` —— 启用或禁用图形验证层。
- `--debug-markers` / `--no-debug-markers` —— 切换 GPU 调试标记。
- `--renderdoc` / `--no-renderdoc` —— 切换 RenderDoc 集成。

### 自动化（RPC / MCP）

- `--mcp` —— 启用 agent 并自动启动 MCP 服务器，使外部工具能够驱动
  编辑器/运行时。
- `--mcp-host <host>` / `--mcp-port <port>` —— MCP 服务器的绑定地址和端口（默认
  端口 `8848`）。

与 `--render-mode none` 结合可运行无窗口的模拟服务（视觉捕获工具被禁用），或与
`--render-mode offscreen` 结合可保持渲染服务处于活动状态以供捕获。

### 导出

- `--export` —— 运行一次无头桌面导出（无编辑器窗口）并退出。
- `--export-output <dir>`（别名 `--out`）—— 导出的目标目录。
- `--export-platform <platform>` —— 目标平台；为空表示宿主桌面。
- `--export-run` —— 在打包后启动导出的构建。

完整工作流请参阅 [跨平台导出](cross_platform_export_CN.md)。

### 集成工具（子命令）

这些会完全绕过引擎 UI：

- `vultra asset import <asset-root> [--reimport]` —— 将源资产导入/烹饪到
  `<asset-root>/imported`。
- `vultra asset pack <asset-root> <out.vpk> [...]` —— 将导入的资产打包为一个
  `.vpk`。
- `vultra asset validate-vpk <resources.vpk> [...]` —— 对照其源代码树校验一个
  `.vpk`。
- `vultra shader <args>` —— 运行着色器编译器 CLI（`vultra shader --help`）。

### 示例调用

```sh
# Open the launcher (no args)
vultra

# Edit a project
vultra --editor --project ./MyGame

# Edit, with the MCP automation server on a custom port
vultra --editor --project ./MyGame --mcp --mcp-port 9000

# Play a project's scene directly (no editor)
vultra --project ./MyGame --scene res://scenes/main.vscn

# Run a packaged game
vultra --vpk ./MyGame.vpk

# Headless simulation for automation
vultra --mcp --render-mode none --project ./MyGame

# Headless desktop export
vultra --export --project ./MyGame --export-output ./build --export-run
```

## 创建或打开项目

不带任何参数启动 `vultra` 即可打开**项目启动器**。在那里你可以创建新项目、打开
现有项目，或派生一个示例。

项目是一个包含 `.vproject` 工作区文件的目录。创建新项目会搭建如下脚手架：

- `resources/` —— 资产根目录（可通过项目的 `assetRoot` 配置），其中包含一个入门
  级的 `scenes/main.vscn`。一个完整的入门模板还会额外铺设一个默认渲染图
  （`render/default.vrg.json`）、一个示例材质图
  （`materials/default.vmatgraph.json`）、一个项目着色器库，以及几个示例后处理
  通道（Pixelate、Invert）。
- `ai/` —— 一个受版本跟踪的 AI 协作工作区（`game.md` 简报，外加 `specs/`、
  `tasks/`、`workspace/`、`knowledge/`、`agents/` 和 `generated/` 文件夹），用于
  记录项目意图和约定。

`.vproject` 本身记录了项目名称、资产根目录、默认场景与构建场景、用于编辑的渲染图，
以及哪些插件被启用。

新项目打开时已经连好了一个最小场景 —— 一个方向光、一个相机和一个环境节点 ——
因此你可以立即渲染出一些东西。

要获取更多入门模板，[vultra-examples](https://github.com/zzxzzk115/vultra-examples)
仓库中有额外的示例项目，你可以从启动器克隆或派生它们。

## 编辑器速览

一旦一个项目以编辑器模式打开，你就会得到一个可停靠的、多窗口的工作区。主要窗口
包括：

- **场景视图（Scene View）** —— 用于导航和编辑场景的 3D 视口；另有一个独立的
  **游戏视图（Game View）**，通过活动相机展示场景。
- **场景层级（Scene Hierarchy）** —— 当前场景中实体的树。
- **检视器（Inspector）** —— 查看和编辑所选实体上的组件。
- **内容浏览器（Content Browser）** —— 浏览和管理项目的资产。
- **渲染图编辑器（Render Graph Editor）** —— 编写基于节点的渲染管线。
- **材质图编辑器（Material Graph Editor）** —— 以节点图的方式构建材质。
- **动画器图（Animator Graph）** —— 编写动画状态机。
- **帧调试器（Frame Debugger）** —— 逐帧检视渲染通道和资源。
- **性能分析器（Profiler）** —— 跟踪帧计时和性能。
- **控制台（Console）** —— 引擎和脚本的日志输出。
- **历史（History）** —— 编辑器操作的撤销/重做历史。
- **代码编辑器（Code Editor）** —— 在应用内编辑项目脚本。

编辑器还是可扩展的：插件可以注册它们自己的面板和菜单项（参阅
[插件](plugins_CN.md)）。

## 后续步骤

- [项目与资产](project_and_assets_CN.md) —— 项目布局、资产管线，以及 `res://`
  URI。
- [场景与组件](scene_and_components_CN.md) —— 实体/组件模型，以及场景是如何
  构建的。
- [Lua 脚本](lua_scripting_CN.md) —— 用 Lua API 编写玩法逻辑。
- [渲染图](render_graphs_CN.md) —— 编写和定制渲染管线。
- [插件](plugins_CN.md) —— 扩展引擎和编辑器。
- [跨平台导出](cross_platform_export_CN.md) —— 打包并发布你的游戏。
