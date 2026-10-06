local is_root = (os.projectdir() == path.directory(os.scriptdir()))

includes("vri")

add_requires("vri-vultra v0.1.17", {
    alias = "vri",
    configs = {vulkan = true, gl = false, wgpu = false, d3d12 = false, metal = false}
})
local window_backend = get_config("libvultra_window_backend") or "glfw"
if window_backend == "sdl3" then
    add_requires("libsdl3 3.4.0")
else
    add_requires("glfw 3.4", {configs = {wayland = is_plat("linux")}})
    add_requireconfs("imgui.glfw", {configs = {wayland = is_plat("linux")}})
end
add_requires("argparse v3.2")
add_requires("spdlog v1.15.3", {configs = {header_only = true, std_format = true}})
add_requires("imgui v1.92.5-docking", {configs = {glfw = window_backend == "glfw", sdl3 = window_backend == "sdl3"}})
if is_root then
    -- Editor-only canvas; the player and public vultra library do not link it.
    add_requires("imgui-node-editor 021aa0ea4da13fed864bafb2a92d4c5205076866")
    add_requireconfs("imgui-node-editor.imgui", {
        version = "v1.92.5-docking", override = true,
        configs = {glfw = window_backend == "glfw", sdl3 = window_backend == "sdl3"}
    })
end
add_requires("slang-static 2026.11")
add_requires("antlr4-runtime 4.13.2", {configs = {shared = false}, system = false})
if is_root then
    add_requires("rmlui 6.2", {configs = {shared = false, lua = false, svg = false, lottie = false}})
    add_requireconfs("rmlui.zlib", {system = false, configs = {shared = false}, override = true})
    add_requireconfs("rmlui.freetype.zlib", {system = false, configs = {shared = false}, override = true})
end
add_requires("stb 2025.03.14")
add_requires("tinygltf v2.9.7", "glm 1.0.1")
-- Reuse TinyGLTF's existing JSON dependency for research workspace persistence.
add_requires("nlohmann_json v3.12.0")
add_requires("tinyobjloader v2.0.0rc13")
add_requireconfs("openfbx.libdeflate", {system = false, configs = {shared = false}, override = true})
add_requires("openfbx v0.9")
add_requires("directxtex 2025.07", {configs = {dx11 = false, dx12 = false}})
add_requires("xxhash v0.8.3")
add_requires("vtask v0.1.0")
add_requires("meshoptimizer v0.24")
add_requires("ispc 1.28.2", {host = true})
if is_plat("linux") then
    add_requires("nativefiledialog-extended v1.3.0", {configs = {portal = true}})
end
if has_config("libvultra_with_openxr") then
    if is_plat("linux") then
        add_requires("openxr-static 1.1.49")
    else
        add_requires("openxr 1.1.49", {configs = {shared = false}})
    end
    add_requires("vulkan-headers 1.4.335")
end

-- Vendored libraries own their public include paths and build details here.
target("filewatch")
    set_kind("headeronly")
    set_default(false)
    add_headerfiles("FileWatch/FileWatch.hpp")
    add_includedirs("FileWatch", {public = true})
target_end()

target("renderdoc-api")
    set_kind("headeronly")
    set_default(false)
    add_headerfiles("renderdoc/renderdoc_app.h")
    add_includedirs("renderdoc", {public = true})
target_end()

target("openpbr")
    set_kind("headeronly")
    set_default(false)
    add_headerfiles("openpbr/**.h")
    add_sysincludedirs("openpbr", {public = true})
target_end()

includes("bc7enc")

-- Only the opt-in scripting target links Lua; keep the core library independent.
if is_root then
    add_requires("lua v5.4.8", {configs = {shared = false}, system = false})
end
