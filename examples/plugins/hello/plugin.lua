-- Plugin manifest. The returned table tells the engine PluginSystem what to load.
--
-- Fields (all optional except that at least one of native/entry should be present):
--   name   : display name (defaults to the folder name)
--   native : a native C++ shared library exporting vultraCreatePlugin (relative to this file)
--   entry  : a Lua entry script run after the native library (relative to this file)
return {
    name = "hello",
    -- native = "libhello_plugin.dll", -- optional native glue library
    entry = "init.lua",
}
