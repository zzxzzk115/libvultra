# Native Math plugin

A **native C++ + Lua** Vultra plugin demonstrating the glue-layer pattern. The native library
registers `native_math.length3(x, y, z)` into the shared Lua state; the Lua `init.lua` then builds
on it. The same shape wraps any third-party C/C++ SDK and exposes a Lua API.

- Manifest: `vultra.plugin.vmanifest` (`native` + `entry`)
- Native: built by the `plugin-native-math` xmake target into this folder (`native_math.dll`/`.so`)
- Entry: `init.lua`

Enable it from the editor's **Project Settings → Plugins** tab (plugins are off by default).
Native plugins are desktop-only.
