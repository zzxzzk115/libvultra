# Render Upscaler Plugins

The engine owns a generic upscaler bridge. Vendor SDK code, DLLs, headers, libraries, signatures, and
feature-specific calls live in an external native plugin.

The bridge is split into three layers:

- `IRenderBackendExtensionService`: one early native plugin can own Vulkan hook metadata and receives
  backend lifecycle callbacks around device, swapchain, acquire, present, and idle.
- `IRenderUpscalerService`: plugins register an `IUpscalerProvider`; game/editor code can select a
  provider and set enabled/mode.
- `ExternalUpscaler`: a render-graph node that snapshots native command/texture resources, calls the
  active provider, then clears frame-graph command state so later passes rebind their pipeline and
  descriptors.
- When an active provider is enabled in a real upscaling mode, the renderer asks the provider for the
  optimal render extent and builds scene passes at that lower resolution. The upscaler output returns
  to the current target/backbuffer extent, so post-upscale passes should be wired after the upscaler.

Use `"loadPhase": "pre_render_device"` in the plugin manifest for native plugins that need to register
before `RenderBackendSystem` creates the render device:

```json
{
  "id": "com.example.streamline",
  "loadPhase": "pre_render_device",
  "config": [
    {
      "key": "streamlineSdkRoot",
      "label": "Streamline SDK Root",
      "type": "path",
      "env": "VULTRA_DLSS_STREAMLINE_SDK_ROOT",
      "required": true,
      "description": "Local folder containing Streamline bin/include/lib files. Keep this outside version control."
    },
    {
      "key": "streamlineBin",
      "label": "Streamline Binary Folder",
      "type": "path",
      "env": "VULTRA_DLSS_STREAMLINE_BIN"
    }
  ],
  "native": "vultra_plugin_streamline",
  "entry": "init.lua"
}
```

The early phase is native-only. Lua glue runs later through the normal plugin path after `ScriptSystem`.
The plugin can live under `<project>/resources/plugins/<plugin-name>/`; the SDK itself can live anywhere
on the machine and be referenced by the env-backed config fields above.

For local DLSS testing, put `resources/plugins/DLSS/` in the project, keep the native
`vultra_plugin_dlss.dll` and NVIDIA Streamline payload local, and place machine-specific SDK paths in
`<project>/.env`:

```ini
VULTRA_DLSS_STREAMLINE_SDK_ROOT=C:\Users\Administrator\Downloads\streamline-sdk-v2.11.1
VULTRA_DLSS_STREAMLINE_BIN=C:\Users\Administrator\Downloads\streamline-sdk-v2.11.1\bin\x64
VULTRA_DLSS_PROJECT_ID=7f3a9c21-84bd-46e2-91af-c5d7382b0e64
VULTRA_DLSS_ENABLED=true
VULTRA_DLSS_MODE=performance
```

Do not commit `.env`, the real DLSS DLL, or Streamline SDK folders.

`VULTRA_DLSS_APPLICATION_ID` is a NVIDIA-assigned numeric id for production DLSS/NGX use. Leave it
empty for local Project ID development mode, but do not treat a random value as a production id.
`VULTRA_DLSS_PROJECT_ID` can be a stable local GUID used while validating the bridge.

Lua control surface:

```lua
local providers = Upscaler.providers()
local active = Upscaler.active()
Upscaler.setActive("noop")
Upscaler.setEnabled(true)
Upscaler.setMode("quality")
local status = Upscaler.status()
```

The Streamline plugin can also activate itself at startup through env-backed
manifest settings: `VULTRA_DLSS_ENABLED=true` and `VULTRA_DLSS_MODE=performance`.
Accepted modes are `ultra_quality`, `quality`, `balanced`, `performance`,
`ultra_performance`, `dlaa`, and `off`.

Lua never receives Vulkan handles, textures, command buffers, or raw pointers.

The example `examples/plugins/noop_upscaler` validates the bridge without a vendor SDK. It registers a
provider named `noop`, receives `evaluate()` calls, and returns `false` so the engine performs the
fallback blit.

For a Streamline/DLSS SR plugin, keep these responsibilities in the plugin:

- load `sl.interposer.dll` or other Streamline binaries from the plugin distribution,
- perform Streamline signature/init/shutdown,
- fill the generic Vulkan hook table when using manual hooking,
- map `UpscalerFrameToken`, `UpscalerConstants`, `NativeTextureResource`, and `UpscalerResourceTag`
  into Streamline frame tokens, constants, and resource tags,
- expose `slGetFeatureRequirements(...).vkDeviceExtensions` through
  `IRenderBackendExtension::collectVulkanDeviceRequirements` before Vulkan device creation,
- call DLSS option/evaluate APIs.

Plugins can also ship render-graph pass assets. A Streamline plugin should provide a Lua pass such as
`DLSSUpscale` under its own `render/passes/` folder. The pass calls the generic scripted-pass API:

```lua
local out = ctx:createUpscalerOutput {
  color = ctx:getInput("color"),
  depth = ctx:getInput("depth"),
  motion = ctx:getInput("motion"),
}
ctx:setOutput("color", out)
```

and records `rc:evaluateUpscaler()` in `execute`. The pass remains portable because Lua only sees
FrameGraph handles; the engine assembles native texture tags and calls the active provider. Projects can
install the plugin and wire that pass into their own `.vrg.json` graph instead of relying on an engine
builtin node. `createUpscalerOutput` defaults to the current view target extent, so the input
color/depth/motion can be lower resolution while the output returns to the target/backbuffer resolution.
Exposure is not a required V1 graph input; DLSS should use provider auto exposure unless a later plugin
deliberately exposes manual exposure as a high-level setting or optional resource.

Motion vectors:

The builtin render graph provides a `MotionVectors` pass. It reads scene depth and outputs a `motion`
resource in `RG16F` pixel-space motion-vector format. The first implementation records camera motion by
reprojecting current clip space into the previous frame clip space. This is enough to provide a legal
motion-vector input for early DLSS SR testing on static scenes and camera movement.

Per-object velocity is not part of this first pass yet. Dynamic mesh/object motion will need a later
geometry/GBuffer extension that writes previous-vs-current object motion using
`RenderInstance::previousWorldMatrix`.

Troubleshooting:

- `failed to load native library` with Win32 error 126 usually means the plugin DLL exists but one of
  its dependent DLLs is missing. Check that the plugin folder or configured Streamline bin folder
  contains `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`, and `nvngx_dlss.dll`.
- `hook is activated without device being created` means Streamline saw Vulkan hook traffic before it
  knew the device. The plugin must call `slSetVulkanInfo()` after Vulkan device creation, including
  device paths created through OpenXR.
- `DLSSContext is not available` comes from NGX, NVIDIA's lower-level AI feature runtime. At that
  point the Streamline bridge is alive, but NGX refused to create the DLSS feature context. Check RTX
  hardware, current NVIDIA driver, matching SDK DLLs, Project ID for local testing, and NVIDIA
  Application ID for production testing. For Vulkan, also verify the device enabled Streamline's
  required extensions such as `VK_NVX_binary_import` and `VK_NVX_image_view_handle`.
- `slEvaluateFeature(DLSS)` failures after enabling the upscaler usually mean frame data is incomplete:
  verify `ExternalUpscaler` has color, output color, depth, and motion-vector resources with non-zero
  native Vulkan image/image-view handles.

Streamline 2.11's Vulkan common plugin may still print unsupported warnings for `CmdBindPipeline`,
`CmdBindDescriptorSets`, and `BeginCommandBuffer` because its plugin-manager hook map omits those IDs.
The DLSS plugin compensates by filling generic command-state callback slots in `VulkanHookTable` from
`sl.common.dll`, and `VulkanCommandBuffer` calls those callbacks after the native Vulkan commands. This
keeps command-buffer state restoration in the plugin while the engine only carries raw Vulkan handles
and function pointers.

DLSS plugins must call `slShutdown()` before the Vulkan instance/device and other backend components are
destroyed. The project-local DLSS plugin calls `slShutdown()` during native plugin uninstall, then unregisters
its backend extension/provider. Native plugin DLL handles are released later, after subsystem shutdown, so the
plugin can shut Streamline down while the render backend is still alive without unloading code too early.

Do not vendor Streamline `bin/`, `lib/`, `include/`, `source/`, `external/`, or `shaders/` into engine
core.
