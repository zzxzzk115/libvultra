local vultra_root = path.directory(os.scriptdir())
local features = vultraFeatures()
-- Android and Web hosts own their surface; only desktop platforms build a window backend.
local desktop = not is_plat("android", "wasm")
local shader_compiler = features.shader and has_config("libvultra_shader_compiler")
local asset_import = features.assets and has_config("libvultra_asset_import")

target("vultra")
    set_kind("static")
    if is_plat("linux", "macosx", "android") then
        add_cxflags("-fPIC", {force = true})
    end
    if is_plat("wasm") then
        -- Library throws and consumer catch blocks must use the same Emscripten exception ABI.
        add_cxxflags("-fexceptions", {public = true, force = true})
        add_ldflags("-fexceptions", {public = true, force = true})
    end
    local names = {}
    for name in pairs(features) do
        table.insert(names, name)
    end
    table.sort(names)
    add_values("vultra.features", table.unpack(names))
    add_rules("vultra.linux.delivery")
    after_load(function (library)
        import("core.project.project")
        local delivery = library:rule("vultra.linux.delivery")
        -- xmake does not inherit dependency rules; apply link policy only to actual library consumers.
        for _, consumer in pairs(project.targets()) do
            if consumer:kind() == "binary" or consumer:kind() == "shared" then
                for _, dependency in ipairs(consumer:orderdeps({inherit = true})) do
                    if dependency == library then
                        consumer:rule_add(delivery)
                        break
                    end
                end
            end
        end
    end)

    -- One block per module, in dependency order. scripts/build_features.lua owns the dependency table.
    for _, module in ipairs(names) do
        add_includedirs(module .. "/include", {public = true})
        add_headerfiles(module .. "/include/(vultra/**.hpp)")
    end

    if features.core then
        add_files("core/src/**.cpp")
        add_deps("pcg32")
        add_packages("argparse", "spdlog", "glm", {public = true})
        add_packages("nlohmann_json", "stb")
        if is_plat("windows") then
            add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", {public = true})
        end
    end

    if features.platform then
        add_files("platform/src/input/*.cpp")
        if is_plat("windows") then
            add_files("platform/src/windows/*.cpp")
            add_syslinks("comdlg32", "ole32")
        elseif is_plat("linux") then
            add_files("platform/src/linux/*.cpp")
            add_packages("nativefiledialog-extended")
        elseif is_plat("android") then
            add_files("platform/src/linux/file.cpp", "platform/src/linux/process.cpp", "platform/src/linux/memory.cpp")
            add_files("platform/src/android/*.cpp")
            add_syslinks("android", "log")
            add_packages("game-activity", {public = true})
            add_ldflags("-Wl,-u,GameActivity_onCreate", {public = true, force = true})
        elseif is_plat("wasm") then
            add_files("platform/src/linux/file.cpp", "platform/src/web/input.cpp")
        end
        if desktop then
            add_files("platform/src/os/window.cpp", "platform/src/os/file_watcher.cpp", "platform/src/sdl3/**.cpp")
            add_deps("filewatch")
            add_packages("libsdl3", {public = true})
            -- Window surfaces are described with VRI's native-window types.
            add_packages("vri", {public = true})
        end
    end

    if features.gpu then
        local api = get_config("libvultra_graphics_api") or (is_plat("wasm") and "webgpu" or "vulkan")
        local api_enum = {vulkan = "VriGraphicsAPI_Vulkan", webgpu = "VriGraphicsAPI_WebGPU",
                          webgl2 = "VriGraphicsAPI_OpenGLES"}
        add_defines("VULTRA_GRAPHICS_API=" .. api_enum[api])
        add_files("gpu/src/device.cpp", "gpu/src/resources.cpp", "gpu/src/buffer_upload.cpp",
                  "gpu/src/image_readback.cpp", "gpu/src/swapchain.cpp")
        add_files("gpu/src/profiling/profiler.cpp", "gpu/src/profiling/memory_report.cpp",
                  "gpu/src/profiling/benchmark.cpp")
        add_packages("vri", {public = true})
        if desktop then
            add_files("gpu/src/swapchain_window.cpp")
        end
        if not is_plat("wasm") then
            add_defines("VULTRA_WITH_RENDERDOC", {public = true})
            add_files("gpu/src/profiling/renderdoc_capture.cpp")
            add_deps("renderdoc-api")
            if is_plat("linux") then
                add_syslinks("dl")
            end
        end
    end

    if features.shader then
        -- Cooked runtime: load, reflect and bind shader programs without Slang.
        add_files("shader/src/shader_program.cpp", "shader/src/shader_archive.cpp",
                  "shader/src/shader_parameters.cpp", "shader/src/shader_bindings.cpp",
                  "shader/src/shader_pipeline.cpp")
        add_packages("xxhash")
    end
    if shader_compiler then
        add_defines("VULTRA_WITH_SHADER_COMPILER", {public = true})
        -- The builtin pack compiles the library shaders into a VPK.
        add_files("shader/src/builtin_pack.cpp")
        add_files("shader/src/shader_compiler.cpp", "shader/src/shader_cache.cpp",
                  "shader/src/shader_program_compile.cpp", "shader/src/shader_reflection.cpp")
        add_packages("slang-static", "spirv-tools", {public = true})
        add_includedirs("$(builddir)/generated/shaders")
        before_build(function (target)
            local function fingerprint(patterns, identifiers)
                local inputs = {}
                for _, pattern in ipairs(patterns) do
                    table.join2(inputs, os.files(path.join(vultra_root, pattern)))
                end
                table.sort(inputs)
                for _, file in ipairs(inputs) do
                    local name = path.relative(file, vultra_root):gsub("\\", "/")
                    table.insert(identifiers, name .. ":" .. hash.sha256(file))
                end
                return table.concat(identifiers, ";")
            end
            local native = fingerprint({"source/shader/src/shader*",
                                        "source/shader/include/vultra/shader/shader*.hpp"}, {})
            local contents = '#pragma once\ninline constexpr char kShaderToolchainKey[] = "' .. native .. '";\n'
            local output = path.join(os.projectdir(), get_config("builddir") or "build",
                                     "generated/shaders/shader_toolchain_key.hpp")
            if not os.isfile(output) or io.readfile(output) ~= contents then
                io.writefile(output, contents)
            end
        end)
    end

    if features.graph then
        add_files("graph/src/*.cpp")
    end

    if features.assets then
        -- Values, sources and VPK archives; importers are optional.
        add_files("assets/src/asset_source.cpp", "assets/src/source_file.cpp", "assets/src/vpk_archive.cpp",
                  "assets/src/material_parameters.cpp", "assets/src/tangents.cpp")
        add_packages("xxhash")
    end
    if asset_import then
        add_defines("VULTRA_WITH_TEXTURE_IMPORT", "VULTRA_WITH_MESHLET_BUILD", {public = true})
        add_files("assets/src/import_jobs.cpp", "assets/src/meshlet_build.cpp")
        add_files("assets/src/dds_loader.cpp", "assets/src/image_decode.cpp", "assets/src/texture_import.cpp")
        add_files("assets/src/asset_options.cpp", "assets/src/asset_pipeline.cpp", "assets/src/fbx_loader.cpp",
                  "assets/src/gltf_loader.cpp", "assets/src/obj_loader.cpp")
        add_deps("bc7enc")
        add_packages("vtask", "meshoptimizer", "directxtex", "tinygltf", "tinyobjloader", "openfbx")
    end

    if features.render then
        add_files("render/src/*.cpp|scene_prepare.cpp")
        -- texture_upload.cpp shares the private texture_layout.hpp with the asset importers.
        add_includedirs("assets/src")
        add_deps("openpbr")
        if asset_import then
            add_defines("VULTRA_WITH_SCENE_PREPARATION", {public = true})
            add_files("render/src/scene_prepare.cpp")
        end
    end

    if features.ui then
        add_rules("utils.bin2c", {extensions = {".ttf"}})
        add_files("../resources/ui/LatoLatin-Regular.ttf")
        add_files("ui/src/editor_gui.cpp", "ui/src/editor_gui_frame.cpp", "ui/src/editor_gui_inspector.cpp",
                  "ui/src/editor_gui_theme.cpp", "ui/src/editor_gui_textures.cpp")
        add_packages("imgui", {public = true})
        if desktop then
            add_defines("VULTRA_EDITOR_GUI_WINDOW")
            add_files("ui/src/editor_gui_viewports.cpp", "ui/src/sdl3/**.cpp")
        end
    end

    if features.app then
        add_files("app/src/base_app.cpp", "app/src/render_app.cpp", "app/src/camera/*.cpp")
        if is_plat("android") then
            add_files("app/src/android_app.cpp")
        elseif is_plat("wasm") then
            add_files("app/src/web_app.cpp")
            add_ldflags("-sASYNCIFY", "-sALLOW_MEMORY_GROWTH=1", "-sGROWABLE_ARRAYBUFFERS=0",
                        {public = true, force = true})
        else
            add_files("app/src/runtime_context.cpp", "app/src/desktop_app.cpp")
            if features.ui then
                add_files("app/src/imgui_app.cpp")
            end
        end
        if features.assets then
            add_files("app/src/packaged_resources.cpp", "app/src/working_directory.cpp")
        end
    end

    if features.xr then
        add_defines("VULTRA_WITH_OPENXR", {public = true})
        add_files("xr/src/*.cpp")
        add_packages(is_plat("linux") and "openxr-static" or "openxr", "vulkan-headers", {public = true})
    end
target_end()
