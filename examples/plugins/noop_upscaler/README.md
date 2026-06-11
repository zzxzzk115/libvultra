# Noop Upscaler Plugin

This plugin validates the V1 render upscaler bridge without Streamline or DLSS binaries.

- Native load phase: `pre_render_device`
- Registers one backend extension with an empty Vulkan hook table
- Registers one upscaler provider named `noop`
- Lua entry runs later through the normal plugin phase and reads `Upscaler.status()`

The provider returns `false` from `evaluate()`, so the engine bridge performs its fallback blit.
