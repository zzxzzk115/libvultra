local vultra_root = path.directory(os.scriptdir())
local features = vultraFeatures()

target("vultra")
    set_kind("static")
    if is_plat("linux", "macosx") then
        add_cxflags("-fPIC")
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
    add_files("core/src/base/logger.cpp", "core/src/base/command_line.cpp", "core/src/base/stable_id.cpp")
    add_files("main/src/app/base_app.cpp")
    add_packages("argparse", "spdlog", {public = true})
    if is_plat("windows") then
        add_files("platform/src/windows/file.cpp", "platform/src/windows/process.cpp",
                  "platform/src/windows/memory.cpp")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", {public = true})
    elseif is_plat("linux") then
        add_files("platform/src/linux/file.cpp", "platform/src/linux/process.cpp", "platform/src/linux/memory.cpp")
    end
    if features.math then
        add_files("core/src/math/rigid_pose.cpp")
        add_packages("glm", {public = true})
    end
    if features.json then
        add_packages("nlohmann_json")
    end
    if features.properties then
        add_files("core/src/base/property.cpp")
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
    if features.asset_source or features.asset_values or features.material_values then
        add_includedirs("assets/include", {public = true})
    end
    if features.asset_source then
        add_headerfiles("assets/include/(vultra/assets/asset_source.hpp)",
                        "assets/include/(vultra/assets/source_file.hpp)",
                        "assets/include/(vultra/assets/vpk_archive.hpp)")
        add_files("assets/src/asset_source.cpp", "assets/src/source_file.cpp", "assets/src/vpk_archive.cpp")
        add_packages("xxhash")
    end
    if features.project then
        add_headerfiles("assets/include/(vultra/assets/project_manifest.hpp)")
        add_files("assets/src/project_manifest.cpp")
    end
    if features.asset_values then
        add_headerfiles("assets/include/(vultra/assets/material_parameters.hpp)",
                        "assets/include/(vultra/assets/material_properties.generated.hpp)",
                        "assets/include/(vultra/assets/scene_data.hpp)",
                        "assets/include/(vultra/assets/texture_asset.hpp)",
                        "assets/include/(vultra/assets/texture_format.hpp)",
                        "assets/include/(vultra/assets/texture_import.hpp)",
                        "assets/include/(vultra/assets/fbx_import.hpp)",
                        "assets/include/(vultra/assets/source_file.hpp)")
        add_files("assets/src/material_parameters.cpp", "assets/src/material_properties.generated.cpp",
                  "assets/src/tangents.cpp")
    end
    if features.material_values then
        add_headerfiles("assets/include/(vultra/assets/material_instance.hpp)",
                        "assets/include/(vultra/assets/shader_property.hpp)",
                        "assets/include/(vultra/assets/project_manifest.hpp)",
                        "assets/include/(vultra/assets/fbx_import.hpp)")
        add_files("assets/src/material_instance.cpp", "assets/src/shader_property.cpp")
    end
    if features.texture_import then
        add_headerfiles("assets/include/(vultra/assets/texture_import.hpp)")
        add_includedirs("assets/src")
        add_files("assets/src/dds_loader.cpp", "assets/src/image_decode.cpp", "assets/src/import_jobs.cpp",
                  "assets/src/texture_import.cpp")
        add_deps("bc7enc")
        add_packages("directxtex", "vtask")
    end
    if features.asset_import then
        add_headerfiles("assets/include/(vultra/assets/asset_options.hpp)",
                        "assets/include/(vultra/assets/asset_pipeline.hpp)",
                        "assets/include/(vultra/assets/fbx_import.hpp)")
        add_files("assets/src/asset_options.cpp", "assets/src/asset_pipeline.cpp", "assets/src/fbx_loader.cpp",
                  "assets/src/gltf_loader.cpp", "assets/src/obj_loader.cpp")
        add_packages("tinygltf", "tinyobjloader", "openfbx")
    end
    if features.game_shader then
        add_headerfiles("assets/include/(vultra/assets/shader_asset.hpp)")
        add_includedirs("assets/src", "drivers/src")
        add_files("assets/src/shaders/**.cpp")
        add_packages("antlr4-runtime")
    end
    if features.gpu then
        add_includedirs("drivers/include", {public = true})
        add_headerfiles("drivers/include/(vultra/**.hpp)")
        add_files("drivers/src/rhi/device.cpp", "drivers/src/rhi/resources.cpp")
        add_files("drivers/src/rhi/buffer_upload.cpp", "drivers/src/rhi/image_readback.cpp")
        add_files("drivers/src/profiling/profiler.cpp", "drivers/src/profiling/memory_report.cpp")
        add_packages("vri", {public = true})
    end
    if features.capture then
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
    if features.shader then
        add_files("drivers/src/rhi/shader*.cpp")
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
    if features.camera or features.world then
        add_includedirs("scene/include", {public = true})
        add_headerfiles("scene/include/(vultra/**.hpp)")
    end
    if features.camera then
        add_files("scene/src/camera/*.cpp")
    end
    if features.world then
        -- Camera/light descriptors are CPU values; this does not select GPU implementations.
        add_includedirs("servers/include", {public = true})
        add_headerfiles("assets/include/(vultra/assets/asset_pipeline.hpp)",
                        "servers/include/(vultra/servers/rendering/builtin/render_light.hpp)")
        add_files("scene/src/object.cpp", "scene/src/resource.cpp", "scene/src/material_resource.cpp",
                  "scene/src/node.cpp", "scene/src/node_3d.cpp", "scene/src/render_nodes.cpp",
                  "scene/src/render_properties.generated.cpp", "scene/src/scene_api.cpp", "scene/src/scene_changes.cpp",
                  "scene/src/resource_properties.generated.cpp",
                  "scene/src/scene_components.cpp", "scene/src/scene_execution.cpp", "scene/src/scene_properties.cpp",
                  "scene/src/scene_storage.cpp", "scene/src/scene_tree.cpp", "scene/src/scene_types.cpp",
                  "scene/src/scene_resources.cpp", "scene/src/scene_resource_types.cpp",
                  "scene/src/scene_render_state.cpp")
        add_packages("entt", {public = true})
    end
    if features.rendering then
        add_includedirs("assets/src", "drivers/src")
        add_files("servers/src/rendering/meshlets.cpp", "servers/src/rendering/rendering_server.cpp",
                  "servers/src/rendering/scene.cpp", "servers/src/rendering/shader_material.cpp",
                  "servers/src/rendering/shader_pass_state.cpp", "servers/src/rendering/shader_runtime.cpp",
                  "servers/src/rendering/texture_upload.cpp", "servers/src/rendering/builtin/*.cpp")
        add_packages("meshoptimizer", "vtask")
        add_deps("openpbr")
    end
    if features.scene_rendering then
        add_files("scene/src/scene_import.cpp", "scene/src/scene_shader_materials.cpp")
    end
    if features.asset_export then
        add_headerfiles("assets/include/(vultra/assets/vpk_export.hpp)")
        add_files("assets/src/vpk_export.cpp")
    end
    if features.resource_pack then
        add_files("main/src/packaged_resources.cpp", "main/src/working_directory.cpp")
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
        add_headerfiles("ui/include/(vultra/ui/editor_gui*.hpp)")
        add_files("ui/src/editor_gui.cpp", "ui/src/editor_gui_frame.cpp", "ui/src/editor_gui_inspector.cpp",
                  "ui/src/editor_gui_theme.cpp")
        add_packages("imgui", {public = true})
        if features.window then
            add_defines("VULTRA_EDITOR_GUI_WINDOW")
            add_files("ui/src/editor_gui_viewports.cpp", "ui/src/" .. window_backend .. "/**.cpp")
        end
        if features.desktop then
            add_files("main/src/app/imgui_app.cpp")
        end
    end
    if features.api_host then
        add_headerfiles("api/include/(vultra/api/native_plugin.h)",
                        "api/include/(vultra/api/vultra_ui.generated.h)",
                        "api/include/(vultra/api/plugin_session.hpp)")
        add_files("api/src/plugin_session.cpp")
        if features.api_ui then
            add_defines("VULTRA_API_UI")
        end
    end
    if features.native_plugin then
        add_headerfiles("api/include/(vultra/api/native_plugin.hpp)")
        add_files("api/src/native_plugin.cpp")
        if is_plat("linux") then
            add_syslinks("dl")
        end
    end
    if features.api_scene or features.api_ui or
       (features.editor_gui and (features.world or features.rendering)) then
        add_includedirs("api/include", {public = true})
        add_headerfiles("api/include/(vultra/api/vultra_abi.generated.h)")
    end
    if features.api_scene then
        add_headerfiles("api/include/(vultra/api/scene_bridge.hpp)",
                        "api/include/(vultra/api/vultra_scene.generated.h)")
        add_files("api/src/vultra_scene.generated.cpp")
    end
    if features.api_ui then
        add_headerfiles("api/include/(vultra/api/ui_bridge.hpp)",
                        "api/include/(vultra/api/vultra_ui.generated.h)")
        add_files("api/src/vultra_ui.generated.cpp")
    end
    if features.editor_gui and features.world then
        add_headerfiles("api/include/(vultra/api/scene_properties.generated.hpp)")
        add_files("api/src/scene_properties.generated.cpp")
    end
    if features.editor_gui and features.rendering then
        add_headerfiles("api/include/(vultra/api/render_settings.generated.hpp)")
        add_files("api/src/render_settings.generated.cpp")
    end
    if features.shader_inspector then
        add_files("ui/src/editor_gui_shader_material.cpp")
    end
    if features.xr then
        add_defines("VULTRA_WITH_OPENXR", {public = true})
        add_files("drivers/src/openxr/**.cpp")
        add_packages(is_plat("linux") and "openxr-static" or "openxr", "vulkan-headers", {public = true})
    end
target_end()

if features.native_sdk then
    target("vultra-native-sdk")
        set_kind("headeronly")
        set_default(false)
        add_includedirs("api/include", "scripting/include", {public = true})
        add_headerfiles("api/include/(vultra/api/native_plugin.h)",
                        "api/include/(vultra/api/vultra_*.generated.h)",
                        "scripting/include/(vultra/scripting/native_script.hpp)")
    target_end()
end

-- Compile the shared managed API once before either example or test script assemblies.
if features.script_csharp and os.projectdir() == vultra_root then
    target("vultra-managed-host")
        set_kind("phony")
        set_default(false)
        set_policy("build.fence", true)
        on_build(function ()
            local output = path.join(os.projectdir(), "build", ".tmp", "scripting-managed")
            local project = path.join(vultra_root, "source/scripting/managed/Vultra.ManagedHost.csproj")
            os.execv("dotnet", {"build", project,
                               "-c", "Release", "-o", output, "--nologo"})
        end)
    target_end()
end

-- Authored in-game UI is opt-in; applications that only use VRI keep one vultra dependency.
if features.vgui then
    target("vultra-vgui")
        set_kind("static")
        set_default(false)
        add_deps("vultra")
        add_includedirs("ui/include", {public = true})
        add_headerfiles("ui/include/vultra/ui/vgui.hpp")
        add_rules("utils.bin2c", {extensions = ".png"})
        add_files("ui/assets/vgui_skin.png", "ui/src/vgui.cpp")
        if features.window then
            add_defines("VULTRA_VGUI_WINDOW")
        end
        add_packages("rmlui", {public = true})
    target_end()
end

-- Project scripting is optional. The runtime links it statically, while C++ experiments may omit it.
if features.script_host then
    target("vultra-scripting")
        set_kind("static")
        set_default(false)
        if is_plat("linux", "macosx") then
            add_cxflags("-fPIC")
        end
        add_deps("vultra")
        add_includedirs("scripting/include", {public = true})
        add_headerfiles("scripting/include/(vultra/**.hpp)")
        add_files("scripting/src/script_host.cpp")
        if features.script_native then
            add_defines("VULTRA_SCRIPT_NATIVE")
            add_files("scripting/src/native_script.cpp")
        end
        if features.script_lua then
            add_defines("VULTRA_SCRIPT_LUA")
            add_files("scripting/src/lua_script.cpp")
            add_packages("lua")
        end
        if features.script_csharp then
            add_defines("VULTRA_SCRIPT_CSHARP")
            add_files("scripting/src/dotnet_script.cpp")
            if is_plat("linux") then
                add_syslinks("dl")
            end
        end
        if features.api_ui then
            add_defines("VULTRA_SCRIPT_GUI")
        end
        add_packages("nlohmann_json")
    target_end()
end
