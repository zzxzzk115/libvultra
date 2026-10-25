local vultra_root = path.directory(os.scriptdir())
local features = vultraFeatures()

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

    for _, module in ipairs({"core", "platform", "main"}) do
        add_includedirs(module .. "/include", {public = true})
        add_headerfiles(module .. "/include/(vultra/**.hpp)")
    end
    -- Opaque ABI declarations carry no GUI implementation or graphics dependency.
    add_includedirs("api/include", {public = true})
    add_headerfiles("api/include/(vultra/api/vultra_abi.generated.h)",
                    "api/include/(vultra/api/vultra_ui.generated.h)")
    add_files("core/src/base/logger.cpp", "core/src/base/command_line.cpp", "core/src/base/stable_id.cpp")
    add_files("main/src/app/base_app.cpp")
    add_packages("argparse", "spdlog", {public = true})
    if is_plat("windows") then
        add_files("platform/src/windows/file.cpp", "platform/src/windows/process.cpp",
                  "platform/src/windows/memory.cpp")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", {public = true})
    elseif is_plat("linux") then
        add_files("platform/src/linux/file.cpp", "platform/src/linux/process.cpp", "platform/src/linux/memory.cpp")
    elseif is_plat("android") then
        add_files("platform/src/linux/file.cpp", "platform/src/linux/process.cpp", "platform/src/linux/memory.cpp")
        add_files("platform/src/android/asset.cpp")
        add_syslinks("android", "log")
    elseif is_plat("wasm") then
        add_files("platform/src/linux/file.cpp")
    end
    if features.math then
        add_files("core/src/math/rigid_pose.cpp")
        add_packages("glm", {public = true})
    end
    if features.random then
        add_deps("pcg32")
        add_files("core/src/math/random_stream.cpp")
    end
    if features.json then
        add_packages("nlohmann_json")
    end
    if features.properties then
        add_files("core/src/base/property.cpp", "core/src/base/property_schema.cpp")
    end
    if features.input then
        add_files("platform/src/input/input.cpp")
    end
    if features.input_actions then
        add_files("platform/src/input/input_map.cpp")
    end
    if features.image then
        add_files("core/src/image/image.cpp")
    end
    if features.image_io then
        add_files("core/src/image/image_io.cpp")
        add_packages("stb")
    end
    if features.image_metrics then
        add_files("core/src/image/image_metrics.cpp")
    end
    if features.asset_source or features.asset_values or features.material_values or features.texture_values then
        add_includedirs("assets/include", {public = true})
    end
    if features.asset_source then
        add_headerfiles("assets/include/(vultra/assets/asset_source.hpp)",
                        "assets/include/(vultra/assets/source_file.hpp)",
                        "assets/include/(vultra/assets/vpk_archive.hpp)")
        add_files("assets/src/asset_source.cpp", "assets/src/source_file.cpp", "assets/src/vpk_archive.cpp")
        add_packages("xxhash")
    end
    if features.texture_values then
        add_headerfiles("assets/include/(vultra/assets/texture_data.hpp)",
                        "assets/include/(vultra/assets/texture_asset.hpp)",
                        "assets/include/(vultra/assets/texture_format.hpp)")
    end
    if features.asset_jobs then
        add_includedirs("assets/src")
        add_files("assets/src/import_jobs.cpp")
        add_packages("vtask")
    end
    if features.meshlet_build then
        add_defines("VULTRA_WITH_MESHLET_BUILD", {public = true})
        add_files("assets/src/meshlet_build.cpp")
        add_packages("meshoptimizer")
    end
    if features.asset_values then
        add_headerfiles("assets/include/(vultra/assets/material_parameters.hpp)",
                        "assets/include/(vultra/assets/material_properties.generated.hpp)",
                        "assets/include/(vultra/assets/scene_data.hpp)",
                        "assets/include/(vultra/assets/scene_render_data.hpp)",
                        "assets/include/(vultra/assets/imported_asset.hpp)",
                        "assets/include/(vultra/assets/meshlet_data.hpp)",
                        "assets/include/(vultra/assets/fbx_import.hpp)",
                        "assets/include/(vultra/assets/source_file.hpp)")
        add_files("assets/src/material_parameters.cpp", "assets/src/material_properties.generated.cpp",
                  "assets/src/tangents.cpp")
    end
    if features.render_values then
        add_includedirs("servers/include", {public = true})
        add_headerfiles("servers/include/(vultra/servers/rendering/builtin/render_light.hpp)")
    end
    if features.material_values then
        add_headerfiles("assets/include/(vultra/assets/material_instance.hpp)",
                        "assets/include/(vultra/assets/asset_id.hpp)",
                        "assets/include/(vultra/assets/shader_property.hpp)",
                        "assets/include/(vultra/assets/asset_entry.hpp)",
                        "assets/include/(vultra/assets/fbx_import.hpp)")
        add_files("assets/src/material_instance.cpp", "assets/src/shader_property.cpp")
    end
    if features.texture_import then
        add_defines("VULTRA_WITH_TEXTURE_IMPORT", {public = true})
        add_headerfiles("assets/include/(vultra/assets/texture_import.hpp)")
        add_includedirs("assets/src")
        add_files("assets/src/dds_loader.cpp", "assets/src/image_decode.cpp",
                  "assets/src/texture_import.cpp")
        add_deps("bc7enc")
        add_packages("directxtex")
    end
    if features.asset_import then
        add_headerfiles("assets/include/(vultra/assets/asset_options.hpp)",
                        "assets/include/(vultra/assets/asset_pipeline.hpp)",
                        "assets/include/(vultra/assets/fbx_import.hpp)")
        add_files("assets/src/asset_options.cpp", "assets/src/asset_pipeline.cpp", "assets/src/fbx_loader.cpp",
                  "assets/src/gltf_loader.cpp", "assets/src/obj_loader.cpp")
        add_packages("tinygltf", "tinyobjloader", "openfbx")
    end
    if features.game_shader_runtime then
        add_headerfiles("assets/include/(vultra/assets/shader_asset.hpp)")
        add_includedirs("assets/src", "drivers/src")
        add_files("assets/src/shaders/shader_asset.cpp", "assets/src/shaders/shader_archive.cpp",
                  "assets/src/shaders/material_instance.cpp")
    end
    if features.game_shader then
        add_defines("VULTRA_WITH_GAME_SHADER_COMPILER", {public = true})
        add_files("assets/src/shaders/shader_compiler.cpp", "assets/src/shaders/shader_parser.cpp",
                  "assets/src/shaders/shader_source_map.cpp", "assets/src/shaders/generated/*.cpp")
        add_packages("antlr4-runtime")
    end
    if features.gpu then
        local api = get_config("libvultra_graphics_api") or (is_plat("wasm") and "webgpu" or "vulkan")
        local api_enum = {vulkan = "VriGraphicsAPI_Vulkan", webgpu = "VriGraphicsAPI_WebGPU",
                          webgl2 = "VriGraphicsAPI_OpenGLES"}
        add_defines("VULTRA_GRAPHICS_API=" .. api_enum[api])
        add_includedirs("drivers/include", {public = true})
        add_headerfiles("drivers/include/(vultra/**.hpp)")
        add_files("drivers/src/rhi/device.cpp", "drivers/src/rhi/resources.cpp")
        add_files("drivers/src/rhi/buffer_upload.cpp", "drivers/src/rhi/image_readback.cpp")
        add_files("drivers/src/profiling/profiler.cpp", "drivers/src/profiling/memory_report.cpp")
        add_packages("vri", {public = true})
    end
    if features.capture then
        add_defines("VULTRA_WITH_RENDERDOC", {public = true})
        add_files("drivers/src/profiling/renderdoc_capture.cpp")
        add_deps("renderdoc-api")
        if is_plat("linux") then
            add_syslinks("dl")
        end
    end
    local window_backend = get_config("libvultra_window_backend") or "glfw"
    if features.window then
        add_files("platform/src/os/window.cpp", "platform/src/" .. window_backend .. "/**.cpp")
        add_defines("VULTRA_WINDOW_" .. window_backend:upper(), {public = true})
        add_packages(window_backend == "sdl3" and "libsdl3" or "glfw", {public = true})
    end
    if features.swapchain then
        add_files("drivers/src/rhi/swapchain.cpp")
    end
    if features.swapchain and features.window then
        add_files("drivers/src/rhi/swapchain_window.cpp")
    end
    if features.render_app then
        add_files("main/src/app/render_app.cpp")
    end
    if features.android_app then
        add_files("main/src/app/android_app.cpp", "platform/src/android/input.cpp")
    end
    if features.android_entry then
        add_files("platform/src/android/arguments.cpp")
        add_packages("game-activity", {public = true})
        add_ldflags("-Wl,-u,GameActivity_onCreate", {public = true, force = true})
    end
    if features.web_app then
        add_files("main/src/app/web_app.cpp", "platform/src/web/input.cpp")
        add_ldflags("-sASYNCIFY", "-sALLOW_MEMORY_GROWTH=1", "-sGROWABLE_ARRAYBUFFERS=0", {public = true, force = true})
    end
    if features.desktop then
        add_files("main/src/runtime_context.cpp", "main/src/app/desktop_app.cpp")
    end
    if features.graph then
        add_includedirs("servers/include", {public = true})
        add_headerfiles("servers/include/(vultra/**.hpp)")
        add_files("servers/src/rendering/graph/render_graph.cpp", "servers/src/rendering/graph/pass_catalog.cpp")
    end
    if features.graph_json then
        add_files("servers/src/rendering/graph/graph_definition.cpp")
    end
    if features.blit then
        add_includedirs("servers/include", {public = true})
        add_headerfiles("servers/include/(vultra/servers/rendering/texture_blit.hpp)")
        add_files("servers/src/rendering/texture_blit.cpp")
    end
    if features.shader_runtime then
        add_files("drivers/src/rhi/shader_program.cpp", "drivers/src/rhi/shader_archive.cpp",
                  "drivers/src/rhi/shader_parameters.cpp", "drivers/src/rhi/shader_bindings.cpp",
                  "drivers/src/rhi/shader_pipeline.cpp")
        add_packages("xxhash")
    end
    if features.compute then
        add_includedirs("api/include", {public = true})
        add_headerfiles("api/include/(vultra/api/compute_bridge.hpp)",
                        "api/include/(vultra/api/vultra_compute.generated.h)",
                        "api/include/(vultra/api/vultra_abi.generated.h)")
        add_files("drivers/src/compute/compute_context.cpp", "api/src/compute_bridge.cpp",
                  "api/src/vultra_compute.generated.cpp")
    end
    if features.shader then
        add_defines("VULTRA_WITH_SHADER_COMPILER", {public = true})
        add_files("drivers/src/rhi/shader_compiler.cpp", "drivers/src/rhi/shader_cache.cpp",
                  "drivers/src/rhi/shader_program_compile.cpp", "drivers/src/rhi/shader_reflection.cpp")
        add_deps("filewatch")
        add_packages("slang-static", {public = true})
        add_packages("xxhash")
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
            local native = fingerprint({"source/drivers/src/rhi/shader*",
                                        "source/drivers/include/vultra/drivers/rhi/shader*.hpp"}, {})
            local contents = '#pragma once\ninline constexpr char kShaderToolchainKey[] = "' .. native .. '";\n'
            if features.game_shader then
                local language = fingerprint({"source/assets/src/shaders/*",
                                              "source/assets/src/shaders/generated/*.cpp",
                                              "source/assets/src/shaders/generated/*.h",
                                              "source/assets/include/vultra/assets/shader_asset.hpp",
                                              "source/assets/include/vultra/assets/material_instance.hpp",
                                              "source/assets/include/vultra/assets/shader_property.hpp",
                                              "source/assets/src/material_instance.cpp",
                                              "source/assets/src/shader_property.cpp"},
                                             {"antlr-4.13.2", "vultra-shader-language-1"})
                contents = contents .. 'inline constexpr char kShaderLanguageKey[] = "' .. language .. '";\n'
            end
            local output = path.join(os.projectdir(), get_config("builddir") or "build",
                                     "generated/shaders/shader_toolchain_key.hpp")
            -- Native Slang caches do not depend on the optional game-language parser. No build runs Java.
            if not os.isfile(output) or io.readfile(output) ~= contents then
                io.writefile(output, contents)
            end
        end)
    end
    if features.camera then
        add_files("main/src/camera/*.cpp")
    end
    if features.shader_material then
        add_includedirs("servers/include", {public = true})
        add_headerfiles("servers/include/(vultra/servers/rendering/shader_material.hpp)",
                        "servers/include/(vultra/servers/rendering/shader_runtime.hpp)")
        add_files("servers/src/rendering/shader_material.cpp", "servers/src/rendering/shader_pass_state.cpp",
                  "servers/src/rendering/shader_runtime.cpp")
        if features.game_shader then
            add_files("servers/src/rendering/shader_source_reload.cpp")
            add_packages("vtask")
        end
    end
    if features.texture_upload then
        add_includedirs("assets/src")
        add_includedirs("servers/include", {public = true})
        add_headerfiles("servers/include/(vultra/servers/rendering/texture_upload.hpp)")
        add_files("servers/src/rendering/texture_upload.cpp")
    end
    if features.gpu_scene then
        add_headerfiles("servers/include/(vultra/servers/rendering/scene.hpp)",
                        "servers/include/(vultra/servers/rendering/meshlets.hpp)",
                        "servers/include/(vultra/servers/rendering/rendering_server.hpp)")
        add_files("servers/src/rendering/meshlets.cpp", "servers/src/rendering/rendering_server.cpp",
                  "servers/src/rendering/scene.cpp")
    end
    if features.scene_prepare then
        add_defines("VULTRA_WITH_SCENE_PREPARATION", {public = true})
        add_files("servers/src/rendering/scene_prepare.cpp")
    end
    if features.rendering then
        add_includedirs("assets/src", "drivers/src")
        add_files("servers/src/rendering/builtin/*.cpp")
        add_deps("openpbr")
    end
    if features.resource_pack then
        add_files("main/src/packaged_resources.cpp", "main/src/working_directory.cpp")
    end
    if features.builtin_pack then
        add_headerfiles("assets/include/(vultra/assets/builtin_pack.hpp)")
        add_files("assets/src/builtin_pack.cpp")
    end
    if features.benchmark then
        add_files("drivers/src/profiling/benchmark.cpp")
    end
    if features.dialogs then
        if is_plat("windows") then
            add_files("platform/src/windows/file_dialog.cpp")
            add_syslinks("comdlg32", "ole32")
        elseif is_plat("linux") then
            add_files("platform/src/linux/file_dialog.cpp")
            add_packages("nativefiledialog-extended")
        end
    end
    if features.editor_gui then
        add_includedirs("ui/include", {public = true})
        add_includedirs("api/include", {public = true})
        add_headerfiles("ui/include/(vultra/ui/editor_gui*.hpp)")
        add_headerfiles("api/include/(vultra/api/vultra_abi.generated.h)",
                        "api/include/(vultra/api/vultra_ui.generated.h)", "api/include/(vultra/api/ui_bridge.hpp)")
        add_files("ui/src/editor_gui.cpp", "ui/src/editor_gui_frame.cpp", "ui/src/editor_gui_inspector.cpp",
                  "ui/src/editor_gui_theme.cpp", "api/src/vultra_ui.generated.cpp")
        add_packages("imgui", {public = true})
        if features.window then
            add_defines("VULTRA_EDITOR_GUI_WINDOW")
            add_files("ui/src/editor_gui_viewports.cpp", "ui/src/" .. window_backend .. "/**.cpp")
        end
        if features.desktop then
            add_files("main/src/app/imgui_app.cpp")
        end
    end
    if features.editor_gui and features.rendering then
        add_headerfiles("api/include/(vultra/api/render_settings.generated.hpp)")
        add_files("api/src/render_settings.generated.cpp")
    end
    if features.shader_inspector then
        add_files("ui/src/editor_gui_shader_material.cpp")
    end
    if features.vgui then
        add_includedirs("ui/include", {public = true})
        add_headerfiles("ui/include/vultra/ui/vgui.hpp")
        add_rules("utils.bin2c", {extensions = ".png"})
        add_files("ui/assets/vgui_skin.png", "ui/src/vgui.cpp")
        if features.window then
            add_defines("VULTRA_VGUI_WINDOW")
        end
        add_packages("rmlui")
    end
    if features.xr then
        add_defines("VULTRA_WITH_OPENXR", {public = true})
        add_files("drivers/src/openxr/**.cpp")
        add_packages(is_plat("linux") and "openxr-static" or "openxr", "vulkan-headers", {public = true})
    end
target_end()
