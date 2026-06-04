-- Native example plugin: builds native_math.{dll,so,dylib} next to plugin.lua so the manifest's
-- `native = "native_math"` resolves to it. Desktop only (native plugins use dynamic loading).
if not is_plat("wasm") and not is_plat("android") then
    target("plugin-native-math")
        set_kind("shared")
        set_basename("native_math")
        set_prefixname("") -- no "lib" prefix, so the file is native_math.so / native_math.dll
        add_files("native_math.cpp")
        add_deps("vultra")
        add_packages("sol2")
        -- Emit the shared library directly beside the manifest.
        set_targetdir(os.scriptdir())
end
