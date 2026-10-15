local features = vultraFeatures()

if features.gpu then
    includes("vri")

    add_requires("vri-vultra v0.1.17", {
        alias = "vri",
        configs = {vulkan = true, gl = false, wgpu = false, d3d12 = false, metal = false}
    })
end

local window_backend = get_config("libvultra_window_backend") or "glfw"
if features.window then
    if window_backend == "sdl3" then
        add_requires("libsdl3 3.4.0")
    else
        add_requires("glfw 3.4", {configs = {wayland = is_plat("linux")}})
    end
end
add_requires("argparse v3.2")
add_requires("spdlog v1.15.3", {configs = {header_only = true, std_format = true}})
if features.math then
    add_requires("glm 1.0.1")
end
if features.json then
    add_requires("nlohmann_json v3.12.0")
end
if features.image_io then
    add_requires("stb 2025.03.14")
end
if features.shader then
    add_requires("slang-static 2026.11")
end
if features.shader or features.asset_source then
    add_requires("xxhash v0.8.3")
end
if features.editor_gui then
    add_requires("imgui v1.92.5-docking", {
        configs = {glfw = features.window == true and window_backend == "glfw",
                   sdl3 = features.window == true and window_backend == "sdl3"}
    })
    if features.window and window_backend == "glfw" then
        add_requireconfs("imgui.glfw", {configs = {wayland = is_plat("linux")}})
    end
end
if features.game_shader then
    add_requires("antlr4-runtime 4.13.2", {configs = {shared = false}, system = false})
end
if features.world then
    add_requires("entt v3.15.0", {system = false})
end
if features.asset_import then
    add_requires("tinygltf v2.9.7")
    add_requires("tinyobjloader v2.0.0rc13")
    add_requireconfs("openfbx.libdeflate", {system = false, configs = {shared = false}, override = true})
    add_requires("openfbx v0.9")
end
if features.texture_import then
    add_requires("directxtex 2025.07", {configs = {dx11 = false, dx12 = false}})
    add_requires("vtask v0.1.0")
    add_requires("ispc 1.28.2", {host = true})
end
if features.rendering then
    add_requires("meshoptimizer v0.24")
end
if features.dialogs and is_plat("linux") then
    add_requires("nativefiledialog-extended v1.3.0", {configs = {portal = true}})
end
if features.editor then
    add_requires("imgui-node-editor 021aa0ea4da13fed864bafb2a92d4c5205076866")
    add_requireconfs("imgui-node-editor.imgui", {
        version = "v1.92.5-docking", override = true,
        configs = {glfw = window_backend == "glfw", sdl3 = window_backend == "sdl3"}
    })
end
if features.vgui then
    add_requires("rmlui 6.2", {configs = {shared = false, lua = false, svg = false, lottie = false}})
    add_requireconfs("rmlui.zlib", {system = false, configs = {shared = false}, override = true})
    add_requireconfs("rmlui.freetype.zlib", {system = false, configs = {shared = false}, override = true})
end
if features.xr then
    if is_plat("linux") then
        add_requires("openxr-static 1.1.49")
    else
        add_requires("openxr 1.1.49", {configs = {shared = false}})
    end
    add_requires("vulkan-headers 1.4.335")
end

-- Vendored libraries own their public include paths and build details here.
if features.shader then
    target("filewatch")
        set_kind("headeronly")
        set_default(false)
        add_headerfiles("FileWatch/FileWatch.hpp")
        add_includedirs("FileWatch", {public = true})
    target_end()
end
if features.rendering then
    target("openpbr")
        set_kind("headeronly")
        set_default(false)
        add_headerfiles("openpbr/**.h")
        add_sysincludedirs("openpbr", {public = true})
    target_end()
end
if features.texture_import then
    includes("bc7enc")
end

if features.research_quality then
    target("flip")
        set_kind("headeronly")
        set_default(false)
        add_headerfiles("flip/**.h")
        add_sysincludedirs("flip", {public = true})
    target_end()
end

if features.capture then
    target("renderdoc-api")
        set_kind("headeronly")
        set_default(false)
        add_headerfiles("renderdoc/renderdoc_app.h")
        add_includedirs("renderdoc", {public = true})
    target_end()
end

if features.script_lua then
    add_requires("lua v5.4.8", {configs = {shared = false}, system = false})
end
