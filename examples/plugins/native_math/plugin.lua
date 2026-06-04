-- Manifest for the native_math example plugin: a native C++ library plus a Lua glue script.
-- The native library is loaded first (it registers native_math.* into Lua), then the entry runs.
return {
    name = "native_math",
    native = "native_math", -- platform extension (.dll/.so/.dylib) is appended automatically
    entry = "init.lua",
}
