# 项目布局、资产管线与虚拟文件系统

[English](../project_and_assets.md) | **简体中文**

本文档说明 Vultra 项目在磁盘上的布局方式、源资产如何变为烘焙后的运行时数据，以及一切如何在运行时通过虚拟文件系统（VFS）寻址。它与渲染及场景文档配套：
[架构](architecture_CN.md)、[跨平台导出](cross_platform_export_CN.md)、
[插件](plugins_CN.md) 和 [Lua 脚本](lua_scripting_CN.md)。

## 1. 项目文件

一个项目是一个目录，包含一个 `.vproject` 文件外加一个资产根目录（默认是
`resources/` 文件夹）。项目元数据文件是小型的类 INI/TOML 文本：
一个 `[section]` 头（解析器会忽略它）、`key = value` 行、`#` 注释，以及
可选的双引号包裹的值。

### `.vproject`

项目描述符，由
[`source/vultra_app/src/vproject.cpp`](../../source/vultra_app/src/vproject.cpp) 中的 `loadVProject` 解析。可识别的键：

| 键 | 含义 |
|-----|---------|
| `version` | 格式版本标记（当前为 `1`）；保存时写入，加载时则被忽略。 |
| `name` | 项目 / 可执行文件名称。默认为项目目录名。 |
| `asset_root` | 相对于项目目录的资产根文件夹。默认为 `resources`。 |
| `default_scene` | 默认打开的场景的 `res://` uri。若缺失则回退到包的 `entry_scene`，或 `scenes/` 下的第一个 `.vscn`。 |
| `editing_rendergraph` | 编辑时使用的渲染图的 `res://` uri。默认为 `res://render/default.vrg.json`。 |
| `build_scene.N` | 索引 `N` 处构建场景的 `res://` uri。 |
| `build_scene_alias.N` | 该场景的可选玩家自定义别名（规范名称始终为 uri 文件名的主干）。 |
| `build_scene_enabled.N` | 该场景是否包含在构建中（`true`/`false`/`1`/`0`/`yes`/`no`/`on`/`off`）。 |
| `enabled_plugins` | 为本项目启用的插件 id 的逗号分隔列表（插件默认关闭）。 |
| `plugin_config.<id>.<key>` | 由插件清单声明的逐插件配置值。 |

旧版的 `build_scene_name.N` 键在加载时会被迁移到 `build_scene_alias.N`。

### `.env`

机器本地环境变量，由 `loadProjectEnvFile` 加载。每行为
`KEY=value`（可选的 `export ` 前缀会被剥除，值可加引号）。每个条目
都会被设置到进程环境中，从而让密钥和机器特定路径不进入已提交的
`.vproject`。该文件应当被 gitignore。

### `.vimport`

一个逐源文件的伴生文件，记录某个源资产是如何被导入的。它是文本，含有
`[vimport]`、`[source]`、`[output]` 和 `[params]` 区段，例如：

```ini
[vimport]
version=1
importer="scene_manifest"
uid="ab252d9dc9df553c37dfc89a0cedc8bd"

[source]
file="scenes/test.vmanifest"

[output]
file="imported/scene_manifest/scenes/test.vmanifest"
```

导入器还维护一个导入数据库（见
[`vasset_import_database.hpp`](../../external/vasset/source/libvasset/include/vasset/vasset_import_database.hpp)），
以源路径为键，跟踪导入器、烘焙后的 `output`，以及用于判断何时需要重新导入的
`sourceHash` / `dependencyHash` / `paramsHash` 值。

### 其他项目文件

- `.vscn` — 场景。见 [场景与组件](scene_and_components_CN.md)。
- `.vrg.json` — 渲染图。见 [渲染图](render_graphs_CN.md)。
- `.vmanifest`（`vultra.package.vmanifest`）— 写在资产根旁边的包清单，
  携带 `name`、`entry_scene`、构建场景表，以及 `plugin_dirs`
  （打包进构建的 `res://` 插件目录）。
- `resources.vpk` — 烘焙后的运行时包（见下文）。

## 2. 资产管线（vasset）

资产处理位于内置的 [vasset](../../external/vasset) 库中。其流程为：

```
source asset  ->  import  ->  UUID-based registry + cooked output  ->  pack  ->  resources.vpk
```

**导入。** 源文件（`.gltf`/`.glb`/`.fbx`/`.obj`、`.png`/`.jpg`/`.hdr`/`.ktx2`/`.dds`、
`.ply`/`.spz`/`.splat`、音频等）会被扫描并转换为引擎原生的烘焙形式，
放在 `imported/` 文件夹下。每个资产都被分配一个稳定的 UUID，记录在
资产注册表（`registryFile`）中；一个 `.vimport` 伴生文件和导入数据库跟踪
源到输出的映射以及内容哈希，以便在下一遍处理时跳过未改动的资产。

**资产类型。** 注册表通过 `VAssetType`
（[`vasset_type.hpp`](../../external/vasset/source/libvasset/include/vasset/vasset_type.hpp)）对资产进行分类：
纹理（包括 KTX2/Basis 和 DDS）、材质、网格、骨架、动画、
高斯泼溅、场景、场景清单、Lua 脚本与可脚本化对象、渲染 /
材质 / 动画器图 JSON、着色器库、预制体、音频和字体。网格、
纹理、骨架和动画是从它们的 `imported/` 路径解析得到的烘焙二进制；
类文本资产（场景、图、材质、脚本）从其源路径解析。

**打包。** 烘焙过程将导入的输出（外加注册表和包清单）
聚集进 `resources.vpk`，一个 VPK 归档。当启用 `loadFromVPK` 时，VPK 内嵌的
注册表成为 UUID 到路径解析的权威来源。

### CLI 与脚本

`vultra asset` 命令（背后是位于
[`tool_cli.cpp`](../../external/vasset/source/libvasset/src/tool_cli.cpp) 的 vasset CLI）驱动管线：

```
vultra asset import <asset-root> [--reimport]
vultra asset pack   <asset-root> <out.vpk> [--zstd N] [--include ...] [--root ...]
vultra asset cook   <asset-root> <out.vpk> [--reimport] [--zstd N] ...
```

`import` 将源烘焙进 `imported/`；`pack` 将一棵已导入的树打包进
VPK；`cook` 在单遍中完成 import + pack（不二次扫描）。便捷封装位于
[`scripts/`](../../scripts)：`import.ps1`/`.sh`、`pack.ps1`/`.sh` 和 `cook.ps1`/`.sh`。
pack/cook 脚本默认使用 `--zstd 6`。这些脚本通过
`VULTRA` 环境变量或标准的 `build/.../vultra-app/` 输出来定位 `vultra` 可执行文件。

## 3. 虚拟文件系统（vfilesystem）

在运行时，资产通过挂载的 VFS
（[vfilesystem](../../external/vasset/external/vfilesystem)）按 URI 寻址，配置位于
[`asset_system.cpp`](../../source/vultra/src/function/asset/asset_system.cpp)。注册了三种
scheme：

| Scheme | 后端 | 用途 |
|--------|-----------|-----|
| `res://` | 项目资产根或挂载的 `resources.vpk` | 项目 / 包资产 |
| `builtin://` | 内嵌的 `builtin.vpk`（见下文） | 引擎资源 |
| `plugins://` | 受管插件存储 `<project>/.vultra/plugins/<id>/<version>/...` | 受管插件内容 |

`res://` 的关键特性是它在两种模式下都解析到**相同的路径**：

- **编辑时**，资产根通过一个位于物理文件系统之上的编辑器重映射文件系统挂载，
  因而松散的源文件及其 `imported/` 输出会被透明地可见。
  （没有导入器的纯运行时构建直接挂载物理文件系统，并期望预先烘焙的资产。）
- **在打包构建中**，`resources.vpk` 以只读方式挂载在 `res://` 下，
  其内嵌的注册表解析 UUID。

因此 `res://textures/foo.png` 的工作方式完全一致，无论这些字节来自编辑器中的
松散文件，还是来自已发布游戏中烘焙后的 VPK。

## 4. 运行时资产加载

`AssetSystem`（一个提供 `IAssetService` 的引擎子系统）按需加载资产，
在高层次上的行为如下：

- **按需。** 资产在首次被请求时加载（例如被某材质引用的纹理，
  或被某场景引用的网格），而非提前加载。
- **异步 CPU 加载 + GPU 上传。** 当启用异步加载时，读取并解析资产
  字节的工作通过任务调度器在工作线程上进行；解析后的 CPU 数据随后
  排队等待 GPU 上传。GPU 上传在渲染线程上每帧排空若干个，因此
  上传永远不会阻塞工作线程。
- **驻留与引用计数。** 每条资产记录跟踪一个加载状态、一个引用计数，以及
  它最后一次被使用的帧。GPU 上传后，CPU 副本通常会被释放（仅在
  明确请求时保留，例如仍需其骨架的蒙皮网格）。空闲、
  零引用的 CPU 缓存可以在可配置的若干空闲帧后释放。网格、
  纹理和高斯泼溅会驻留在 GPU 池中；纹理注册进一个
  全局无绑定表。

具体的阈值和池布局属于实现细节 —— 请将以上内容视为
预期行为而非契约。

## 5. builtin.vpk

随二进制一同发布的引擎资源 —— GLSL 着色器库、字体、内置
渲染图、纹理（LTC LUT、环境贴图、编辑器图标、光标）以及 i18n
目录 —— 被打包进单个 **zstd 压缩的** `builtin.vpk`。该包由
`builtinpack` 主机工具根据
[`xmake/builtin_pack_cook.lua`](../../xmake/builtin_pack_cook.lua) 中的清单构建（级别 19，最小 blob；
已压缩的格式以未压缩方式存储）。

随后该 VPK 被烘焙进每个自包含二进制，并在启动时挂载在 `builtin://`
scheme 下（见
[`builtin/embed/builtin_pack_mount.cpp`](../../builtin/embed/builtin_pack_mount.cpp)）。该
嵌入机制因平台而异：Windows 上的 `.rc` RCDATA 资源、Linux/macOS 上的 `.incbin` 符号、
wasm 上预加载到 MEMFS 的 `/builtin.vpk`，以及 Android 上的 APK 读取。因此引擎
代码读取的是如 `builtin://shaders/...`、`builtin://fonts/...` 或
`builtin://i18n/...`，磁盘上没有松散文件。关于目录如何被嵌入见 [i18n](i18n_CN.md)，
关于 `plugins://` 存储见 [插件](plugins_CN.md)。
