# Render Upscaler Bridge V1

Implemented the first Streamline-aware bridge layer without vendoring NVIDIA SDK files into engine
core.

Current scope:

- plugin manifests support `loadPhase: "pre_render_device"`;
- early native plugin bootstrap runs before `RenderBackendSystem`;
- normal Lua plugin entry still runs later through `PluginSystem`;
- `IRenderBackendExtensionService` allows one Vulkan hook owner and exposes lifecycle callbacks;
- `IRenderUpscalerService` manages provider registration, active provider, settings, status, resize,
  begin-frame, and evaluate;
- `ExternalUpscaler` render-graph node snapshots native resources and calls the provider with fallback
  blit;
- Lua exposes only `Upscaler` high-level controls;
- `examples/plugins/noop_upscaler` validates early native provider registration.

Deferred follow-up:

- route Vulkan backend function-pointer creation through `VulkanHookTable`;
- provide real previous camera/instance transform caches for motion-vector quality;
- wire default render graphs to include `ExternalUpscaler` where the project wants upscaling;
- implement a separate `vultra-plugin-dlss` that owns all Streamline/DLSS-specific code and binaries.
