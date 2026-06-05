# Hello plugin

A minimal **pure-Lua** Vultra plugin. On install it registers a global `hello_from_plugin(who)`
helper into the shared Lua state, which any entity script can then call.

- Manifest: `vultra.plugin.vmanifest`
- Entry: `init.lua` (defines `on_install` / `on_uninstall`)

Enable it from the editor's **Project Settings → Plugins** tab (plugins are off by default).
