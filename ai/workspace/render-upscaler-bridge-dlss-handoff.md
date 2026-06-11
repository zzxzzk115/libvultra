# Render Upscaler Bridge DLSS Handoff

## 2026-06-11

- DLSS plugin now loads from `resources/plugins/DLSS` using local `.env` values for Streamline SDK/bin paths.
- `vultra_plugin_dlss.dll` is built locally and remains ignored together with Streamline/NVIDIA payload DLLs.
- Vulkan dispatch hooks are passed through the backend extension hook table and consumed by `RenderDevice` for
  instance/device dispatch initialization.
- Streamline 2.11 reports unsupported warnings for `CmdBindPipeline`, `CmdBindDescriptorSets`, and
  `BeginCommandBuffer` because its plugin-manager hook map omits those IDs. The bridge compensates by carrying
  generic command-state callbacks in `VulkanHookTable`; the DLSS plugin fills them from `sl.common.dll`, and
  `VulkanCommandBuffer` calls them after the native Vulkan commands.
- The plugin calls `slSetVulkanInfo()` after native Vulkan device creation even when Streamline hooks are active.
  OpenXR-created devices may not travel through Streamline's `vkCreateDevice` hook. If Streamline already captured
  the device through the interposer, `slSetVulkanInfo()` can return `eErrorInvalidIntegration`; the plugin treats
  that case as an already-bound device only when the hook table contains `vkCreateDevice`.
- `slShutdown()` is no longer optional. During native plugin uninstall the DLSS plugin calls `slShutdown()` before
  unregistering its backend extension/provider.
- Native plugin teardown is split into two phases: `PluginSystem::onShutdown()` calls plugin `uninstall()` while the
  render backend is still alive, then `Engine::shutdownCore()` frees native DLL handles after all subsystems are down.
  This keeps Streamline shutdown before Vulkan device/instance destruction without unloading the plugin DLL too early.
- OpenXR-created Vulkan devices do not necessarily pass through Streamline's `vkCreateDevice` hook. The DLSS plugin
  now always calls `slSetVulkanInfo()` after native device creation and only treats `eErrorInvalidIntegration` as
  success when Streamline already captured the device through the interposer path.
- The project-local plugin is now presented as "Streamline Upscaler" in the manifest/Lua log while preserving the
  existing `com.vultra.dlss` id for compatibility with current `.vproject` enabled plugin settings.
- Streamline diagnostics are intentionally human-oriented: status messages include result codes plus next checks
  for NGX, Application ID/Project ID, SDK DLL mismatch, missing resource tags, and Vulkan integration state.
- `IRenderBackendExtension::collectVulkanDeviceRequirements` lets early native plugins request Vulkan device
  extensions before logical device creation. The Streamline plugin fills this from
  `slGetFeatureRequirements(sl::kFeatureDLSS, ...)`.
- Scripted render passes now have a generic upscaler bridge surface:
  `ctx:createUpscalerOutput { color=, depth=, motion=, exposure= }` during setup and
  `rc:evaluateUpscaler()` during execute. Lua sees only FrameGraph handles; native texture tagging, frame tokens,
  command-buffer handles, and provider calls stay inside the engine bridge.
- The Streamline plugin ships `render/passes/dlss_upscale.lua`, a `DLSSUpscale` render-graph node that projects can
  wire into their own `.vrg.json` graphs after installing the plugin.

## Future Plugin Installment Plan

- After Streamline/DLSS starts cleanly, move first-party sample plugins out of `libvultra/resources/plugins`.
- Create sibling repositories:
  - `../vultra-plugin-hello`
  - `../vultra-plugin-streamline`
  - `../vultra-plugins`
- Each plugin repository owns its own `.git`, tags/releases, manifest, built native DLLs, and versioning.
- `vultra-plugins` is a registry repository containing a total manifest with plugin SHA, version, URL, release ZIP
  link, and compatibility metadata.
- Editor Plugin Manager should support importing from a git URL, a local release ZIP, a local folder, or the
  `vultra-plugins` registry manifest.
- The installer needs version tracking, file ownership records, and a lock file so plugin updates/removals do not
  silently overwrite local edits or mix files from different plugin versions.
- Once that system is live, `libvultra` can remove bundled plugin folders from `resources/plugins` and rely on
  online/local plugin installation sources instead.

## Verification

- `xmake build -y vultra-app`
- `xmake build -y plugin-dlss-upscaler`
- short `xmake run vultra-app` smoke: pre-render-device DLSS native plugin loads, backend extension/provider register,
  Vulkan instance/device dispatch use extension hooks.
- 2026-06-11 diagnostic smoke with `xmake run vultra-app --editor --mcp --project example.vproject`:
  - Streamline loads from `C:\Users\Administrator\Downloads\streamline-sdk-v2.11.1\bin\x64`.
  - Integration mode is local Project ID.
  - `slGetFeatureRequirements(DLSS)` reports driver 580.88 detected, 527.64 required, Vulkan supported, 4 required tags,
    3 Vulkan instance extensions, and 5 Vulkan device extensions.
  - Streamline finds a CMS id for the local Project ID and verifies NVIDIA DLL signatures.
  - `sl.dlss` startup still reports `NGX indicates DLSSContext is not available`, and
    `slIsFeatureSupported(kFeatureDLSS, vkPhysicalDevice)` returns `eErrorFeatureNotSupported`.
  - Shutdown remains clean: `slShutdown end: eOk (0)` and engine shutdown completes.
- 2026-06-11 follow-up diagnostic smoke after device-extension injection:
  - Streamline requested 5 Vulkan device extensions:
    `VK_KHR_push_descriptor`, `VK_NVX_binary_import`, `VK_NVX_image_view_handle`,
    `VK_KHR_buffer_device_address`, and `VK_EXT_buffer_device_address`.
  - RenderDevice enabled all 5 requested extensions before OpenXR/Vulkan device creation.
  - The filtered startup log no longer showed `NGX indicates DLSSContext is not available`.
  - Lua status reported `available=true`, `enabled=false`, `mode=off`, and
    `Streamline Vulkan device ready`.
- 2026-06-11 dev-next sync:
  - Local branch was reset to `origin/dev-next` at `09b9d69` after saving the Streamline bridge work as
    `stash@{0}: codex-before-dev-next-pull-streamline-bridge`.
  - The stash was applied on top of the latest dev-next. The only structural conflict was
    `source/vultra/src/function/rendering/srp/declarative_renderer.cpp` because upstream split builtin render-graph
    pass adapters out of that translation unit.
  - `ExternalUpscaler` was migrated to
    `source/vultra/src/function/rendering/srp/builtin/builtin_passes_post_process.cpp`.
  - Scripted upscaler Lua bridge APIs remain in `declarative_renderer.cpp`:
    `ctx:createUpscalerOutput { ... }` and `rc:evaluateUpscaler()`.
  - Verified after sync:
    `xmake build -y vultra-app` and
    `VULTRA_DLSS_STREAMLINE_SDK_ROOT=...; VULTRA_DLSS_STREAMLINE_BIN=...; xmake build -y plugin-dlss-upscaler`.
- 2026-06-11 builtin motion-vector pass:
  - Added `MotionVectors`, a builtin render-graph pass that reads depth and outputs an `RG16F` pixel-space `motion`
    texture.
  - The first implementation is camera-only reprojection: current clip space to previous frame clip space using
    current/previous camera matrices. It does not yet encode per-object velocity.
  - Default render graphs now include a `MotionVectors` node after `DirectGBuffer`.
  - Upscaler constants mark `cameraMotionIncluded=true` when a motion resource is provided to `ExternalUpscaler` or the
    scripted `rc:evaluateUpscaler()` path.
  - Verified with `xmake build -y vultra-app`; vshaderc compiled `builtin/general/motion_vector.frag`.
