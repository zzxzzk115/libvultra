# 跨平台导出模板

[English](../cross_platform_export.md) | **简体中文**

> **状态 (2026-06)：**
> - **桌面 (Vulkan)：** 可用。
> - **Web (WASM / WebGPU)：** 整个流水线已就位，运行时**可在浏览器中构建、链接
>   并运行** —— 但目前**尚不能正确渲染**。引擎能够进入实时渲染循环而不崩溃，
>   然后产生一个**黑帧**：在 WebGPU 上，universal 渲染器会回退到一个实验性的
>   *兼容（compatibility）* 渲染图
>   （`universal_compat.vrg.json`），其输出目前为空。这是 WebGPU 后端的渲染
>   正确性引导工作，与导出流水线分开追踪。其架构（无编辑器的 `vultra-runtime`
>   播放器 + 运行时获取 VPK + 仅引擎的模板）是合理的；剩下的是逐 pass 的
>   WebGPU 调试（RenderDoc / 插桩）。同样的 WebGPU 黑帧问题在**桌面
>   `--backend webgpu`** 上也会重现，确认这是后端问题，而非 wasm 特有问题。
> - **Android：** 仅设计（未实现）—— 见 Android 章节。

编辑器的 **Export & Run** 会为目标平台打包一个项目。每个目标都复用
相同的两个理念：

1. **将项目打包进一个 `.vpk`**（只读、zstd 压缩的资产包），通过
   `vasset` CLI 完成。此步骤与平台无关 —— 参见
   [editor_app_build.cpp](../../source/vultra_app/src/editor_app/editor_app_build.cpp) 中的 `packProjectVpk`。
2. **将 `.vpk` 与一个预构建的"导出模板"组合**（一个知道如何打开
   `.vpk` 的运行时）以生成可分发产物。

用作*所有*平台模板来源的运行时播放器是无编辑器的
`vultra-runtime` 目标（[source/xmake.lua](../../source/xmake.lua)，
[runtime_main.cpp](../../source/vultra_app/src/runtime/runtime_main.cpp)）。它为桌面、
wasm 和 android 构建，且不包含任何编辑器 / 启动器 / MCP / 资产导入代码。它是
*源*二进制；它也会作为已发布的 release 资产到达用户手中 —— 一个编辑器可以
下载而非本地构建的预构建运行时（见 [导出模板 / 可下载
运行时](#export-templates--downloadable-runtime)）。

编辑器的导出调度器（`runBuildAndLaunch`）根据 `BuildSettings.targetPlatform` 进行分发：
`Windows`/`macOS`/`Linux` → `exportDesktop`；`WebGPU` → `exportWeb`；`Android` → 计划中。

## 桌面

`exportDesktop` 打包 `<Project>.vpk`，将运行时可执行文件复制到它旁边，并启动
`vultra --vpk <Project>.vpk --scene res://...`。

它按优先级顺序解析要复制的运行时
（[editor_app_build.cpp](../../source/vultra_app/src/editor_app/editor_app_build.cpp)）：

1. 来自导出设置的显式 `exportTemplatePath`（如果已设置）；
2. 否则是为目标平台+架构+引擎版本从远程目录**下载的官方模板**，通过
   `export_templates::cachedExportTemplate(...)` 离线解析（见下文）；
3. 否则是编辑器自身的可执行文件，但仅在为宿主平台导出时使用。

因此，编辑器的 exe 现在只是最后的后备来源；推荐路径是一个预构建的
无编辑器运行时（显式指定的，或下载的官方模板）。

## 导出模板 / 可下载运行时

导出设置可以为目标平台+架构+引擎版本下载一个**预构建的、无编辑器的
`vultra-runtime`**，而非要求本地构建。该子系统位于
[export_templates_repository.hpp](../../source/vultra_app/include/editor_app/export_templates_repository.hpp)
/ [.cpp](../../source/vultra_app/src/editor_app/export_templates_repository.cpp)。

- **远程目录。** 一个 JSON 目录枚举 `ExportTemplateEntry`（platform + arch）→ 最新优先的
  `ExportTemplateVersion` 列表（`version`、`minEngineVersion`、release-asset `url`、可选的
  `sha256` / `size`）。目录指向
  `zzxzzk115/vultra-export-templates` 上的 GitHub Release 资产；二进制文件**不**在目录仓库中，而是
  按需下载。`bestVersion` 选取一个精确的引擎版本匹配，否则选取
  满足其 `minEngineVersion` 的最高版本。
- **缓存布局**（以编辑器全局缓存根为根）：
  ```
  .vultra/export-templates/
    catalogs/<owner>-<repo>.json                    downloaded catalog (offline fallback)
    <platform>-<arch>/<version>/vultra-runtime[.exe]  materialized runtime binaries
  ```
  `fetchExportTemplatesCatalog` 缓存目录，使其在离线时仍能渲染；目录缺失时会使用
  先前缓存的副本。`materializeExportTemplate` 只下载一次二进制（如果已存在且
  预期大小匹配则跳过）。
- **离线构建解析器。** 在导出期间，`cachedExportTemplate(cacheRoot, platform, arch,
  engineVersion)` 返回已缓存的运行时路径，**不进行网络访问**（导出设置中的
  "Download official template" 按钮才是早先获取它的途径）。这是上面
  `exportDesktop` 解析顺序中的第 2 步。
- **Release CI。** [.github/workflows/release_export_templates.yaml](../../.github/workflows/release_export_templates.yaml)
  在 `v*` 标签（或手动 dispatch）上为每个平台/架构构建无编辑器的 `vultra-runtime`，并
  将其作为 release 资产发布到 `zzxzzk115/vultra-export-templates`
  （`vultra-export-template-<platform>-<arch>-<version>[.exe]`，release 标签 `v<version>` 来自
  `xmake.lua` 的 `set_version`）。它需要一个 `EXPORT_TEMPLATES_TOKEN` PAT 才能推送到独立的
  模板仓库。该矩阵目前提供 windows/x64，其余行随着跨平台
  运行器上线而保留。

## Web (WASM / WebGPU) —— 流水线已完成，渲染进行中

Web 模板**不**烘焙任何资产。在页面加载时，运行时通过 HTTP **获取**项目的
`.vpk` 并将其挂载到 MEMFS 中，因此一台 Windows 机器可以在**未安装
Emscripten 工具链**的情况下导出到 web。

各部分：

- **仅引擎的模板构建。** `xmake f -p wasm -m release; xmake build vultra-runtime`
  产生带有运行时外壳但无项目
  `--preload-file` 的 `vultra-runtime.{html,js,wasm}`。一个 `after_build` 钩子将它们复制到
  `build/web-template/` 中，命名为
  `index.html` + `vultra-runtime.js/.wasm` —— 桌面端
  `exportTemplatePath` 的 web 对应物。这是一个**构建产物**（在 `build/` 下被 git 忽略），
  并非源；源是位于 `web/emscripten_vultra_runtime.html` 的外壳。
- **运行时获取外壳。** [web/emscripten_vultra_runtime.html](../../web/emscripten_vultra_runtime.html)
  从 URL 读取 `?vpk=`（默认 `game.vpk`）和 `?scene=`，设置
  `Module.arguments = ['--vpk','resources.vpk','--scene', …]`，并在 `Module.preRun` 中将
  VPK 获取到 MEMFS 的 `/resources.vpk`，作为一个 emscripten *run-dependency* —— 因此引擎会等待
  资产就绪后再运行 `main()`。无需 C++ 改动：`AssetSystem` 在 `__EMSCRIPTEN__` 下已经将相对的
  `vpkFile` 映射到 `/resources.vpk`。
- **编辑器导出。** `exportWeb` 打包 `outputDir/game.vpk`，将模板复制到 `outputDir`，
  并（对于 Export & Run）启动一个本地 `python -m http.server` 并在
  `index.html?scene=…` 处打开浏览器。需要服务器是因为 `fetch()` 在 `file://` 上被阻止。

### WASM 兼容性修复

要让引擎真正为 wasm 链接，需要削减/替换仅限 Vulkan 和桌面的代码：

- `requestXRSession` 被守护在 `VULTRA_ENABLE_XR` 之后（唯一未被守护的 `m_XRBackend` 使用）。
- `animation_system.cpp` 使用 ozz 的可移植 `StorePtrU` 而非原始的 `_mm_storeu_ps` SSE
  内联函数。
- graphviz 在 `_GNU_SOURCE` 下编译（emscripten 的 libc 仅在那里声明 `strdup`/`strndup`）。
- 光线追踪 RHI 入口点（BLAS/TLAS/SBT/RT-pipeline）仅存在于 Vulkan 后端；一个
  受守护的 [raytracing_stub_no_vulkan.cpp](../../source/vultra/src/core/rhi/raytracing_stub_no_vulkan.cpp)
  为非 Vulkan 构建提供惰性定义（它们从不被调用：RT 功能标志在非 Vulkan 设备上
  从不启用）。
- `joltphysics` 包在 wasm 上以 `rtti` 配置构建 —— `physics_system.cpp`
  继承 `JPH::JobSystemWithBarrier`，而在 clang/Itanium 下派生类的 typeinfo
  引用基类 typeinfo，Jolt 仅在以 C++ RTTI 构建时才发出它。（桌面/MSVC
  无需它即可链接。）

### WebGPU 后端修复（同时适用于桌面 `--backend webgpu` 和 wasm）

在此工作之前，WebGPU RHI 后端从未运行过真正的一帧；让它通过验证
需要：

- **HDR/浮点纹理格式。** `toWgpuTextureFormat`
  （[conversions.hpp](../../source/vultra/include/vultra/core/rhi/backends/webgpu/conversions.hpp)）
  仅映射 8 位 + 深度格式；增加了 `RG16F/RGBA16F/R32F/RG32F/RGBA32F`（任何 HDR 纹理，
  例如天空盒，此前都是 `Undefined`）。
- **`Float32Filterable` 特性。** 在适配器支持时于设备创建期请求，以便
  RGBA32F 环境贴图可用过滤采样器采样。
- **深度格式能力。** `WebGPURenderDevice::getFormatFeatureFlagsOptimal` 未通告任何
  深度格式，因此深度纹理被拒绝，深度附件变为 `None`（→
  pipeline mismatch panic）。增加了带有 depth-stencil-attachment
  能力位的深度/模板格式。
- **Load-op 兼容性。** WebGPU 没有 `DontCare` load op；`AttachmentLoadOp::eDontCare` 此前被
  映射为 `WGPULoadOp_Undefined`，wgpu-native 会因已使用的附件而拒绝它。现在为
  color/depth/stencil 映射为 `Load`。

**仍然损坏：** 在上述修复之后，WebGPU 渲染循环运行而不崩溃，但渲染一个
黑帧（见状态横幅）。在 WebGPU 上，universal 渲染器使用
`universal_compat.vrg.json`（兼容前向路径，
[universal_renderer.cpp](../../source/vultra/src/function/rendering/srp/builtin/universal_renderer.cpp)）；
该路径执行其各 pass，但不产生可见输出。下一步是逐 pass 调试
（RenderDoc 捕获或对 `final_composition_pass` / `compatibility_basecolor_pass` 插桩）。

## Android —— 计划中（尚未实现）

镜像 [examples/android_app/](../../examples/android_app/)：

- **模板构建。** 将 `vultra-runtime` 构建为 `set_kind("shared")` →
  `libvultra_runtime.so`（arm64-v8a）；入口 `vultra_android_run`；从 gradle prefab 缓存链接
  `libgame-activity.a`（`vultra-runtime` 目标中的 `on_load` 钩子已经
  这样做了）。Android 使用 **Vulkan** RHI，而非 WebGPU。
- **模板包。** 一个从 `examples/android_app/` 派生的压缩 gradle 骨架
  （`*.gradle`、`gradle/wrapper/*`、`AndroidManifest.xml`、GameActivity 胶水代码），带有用于
  app 名称 / package id 的占位符，以及 `app/src/main/assets/game.vpk` 和
  `app/src/main/jniLibs/arm64-v8a/libvultra_runtime.so` 的槽位。
- **资产分发。** 与 web 不同，将 `game.vpk` 捆绑在 APK 的 `assets/` 中；在启动时将其提取到 app
  files 目录并将 `vpkFile` 指向那里（直到存在一个 `VpkFileSystem` AAsset 后端）。
- **`exportAndroid`（未来）。** `packProjectVpk` → 解压 gradle 模板 → 放入 `.so` +
  `game.vpk` → 以来自新的 `BuildSettings.androidSdkPath` / `androidNdkPath` 的
  `ANDROID_SDK_ROOT` / `ANDROID_NDK` 调用 `gradlew assembleDebug/Release`
  → 产出 APK。将
  `androidSdkPath`、`androidNdkPath`、`androidPackageId` 和 keystore 字段添加到 `BuildSettings`
  和导出设置 UI 中。

未决风险：每个 ABI 发布预构建的 `.so` 还是在导出时进行 NDK 编译；`VpkFileSystem` 尚无
AAssetManager 后端；`libgame-activity.a` 版本必须匹配 gradle 解析的
`games-activity` prefab；导出机器上 Gradle/JDK/SDK/NDK 的存在性；release 签名。
