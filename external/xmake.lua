local features = vultraFeatures()
local desktop = not is_plat("android", "wasm")
local shader_compiler = features.shader and has_config("libvultra_shader_compiler")
local asset_import = features.assets and has_config("libvultra_asset_import")

if features.core then
    add_requires("argparse v3.2")
    add_requires("spdlog v1.15.3", {configs = {header_only = true, std_format = true}})
    add_requires("glm 1.0.1")
    add_requires("nlohmann_json v3.12.0")
    add_requires("stb 2025.03.14")
end

if features.platform then
    if is_plat("android") then
        add_requires("game-activity 4.4.2")
    elseif is_plat("linux") then
        add_requires("nativefiledialog-extended v1.3.0", {configs = {portal = true}})
    end
    if desktop then
        add_requires("libsdl3 3.4.0")
    end
end

if features.platform then
    includes("vri")

    local api = get_config("libvultra_graphics_api") or (is_plat("wasm") and "webgpu" or "vulkan")
    local vri_dir = get_config("libvultra_vri_dir")
    add_requires((vri_dir and vri_dir ~= "") and "vri-vultra-local" or "vri v0.2.0", {
        alias = "vri",
        configs = {vulkan = api == "vulkan", gl = api == "webgl2", wgpu = api == "webgpu",
                   d3d12 = false, metal = false}
    })
end

if features.shader or features.assets then
    add_requires("xxhash v0.8.3")
end
if shader_compiler then
    add_requires("slang-static 2026.11")
    -- The static Slang build has no SPIRV-Opt module; the shader compiler runs the optimizer itself.
    add_requires("spirv-tools 1.4.309+0")
end

if asset_import then
    add_requires("tinygltf v2.9.7")
    add_requires("tinyobjloader v2.0.0rc13")
    add_requireconfs("openfbx.libdeflate", {system = false, configs = {shared = false}, override = true})
    add_requires("openfbx v0.9")
    add_requires("directxtex 2025.07", {configs = {dx11 = false, dx12 = false}})
    add_requires("ispc 1.28.2", {host = true})
    add_requires("vtask v0.1.0")
    add_requires("meshoptimizer v0.24")
end

if features.ui then
    add_requires("imgui v1.92.5-docking", {
        configs = {sdl3 = desktop}
    })
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
if features.core then
    target("pcg32")
        set_kind("static")
        set_default(false)
        set_languages("c11")
        add_files("pcg_basic/pcg_basic.c")
        add_sysincludedirs("pcg_basic", {public = true})
    target_end()
end

if features.gpu and not is_plat("wasm") then
    target("renderdoc-api")
        set_kind("headeronly")
        set_default(false)
        add_headerfiles("renderdoc/renderdoc_app.h")
        add_includedirs("renderdoc", {public = true})
    target_end()
end

if desktop and features.platform then
    target("filewatch")
        set_kind("headeronly")
        set_default(false)
        add_headerfiles("FileWatch/FileWatch.hpp")
        add_includedirs("FileWatch", {public = true})
    target_end()
end

if asset_import then
    includes("bc7enc")
end

if features.render then
    target("openpbr")
        set_kind("headeronly")
        set_default(false)
        add_headerfiles("openpbr/**.h")
        add_sysincludedirs("openpbr", {public = true})
    target_end()
end
