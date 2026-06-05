# Project plugins

This folder holds the plugins available to this project. Each plugin is a subfolder with a
`vultra.plugin.vmanifest` manifest (and its `init.lua` / native library). Plugins are **off by
default** — enable the ones you want from the editor's **Project Settings → Plugins** tab, which
records the enabled plugin ids in the `.vproject` file.

See `doc/plugins.md` for the manifest schema and how to author native/Lua plugins. The bundled
`hello` plugin is a pure-Lua example.
