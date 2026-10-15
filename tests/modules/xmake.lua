set_project("vultra-module-probe")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "$(builddir)"})
includes("../..")

local features = vultraFeatures()
target("module-probe")
    set_kind("binary")
    add_deps("vultra")
    add_files("probe.cpp")
    for _, name in ipairs({"gpu", "graph", "graph_json", "shader", "desktop", "image_io"}) do
        if features[name] then
            add_defines("TEST_" .. name:upper())
        end
    end
    if features.graph and features.shader then
        add_defines("TEST_GRAPH_SHADER")
        add_files("../../examples/research/color_gain.cpp")
    end
    after_build(function (target)
        import("core.project.project")
        import("core.base.json")
        local library = target:dep("vultra")
        local packages = {}
        for name in pairs(project.required_packages()) do
            table.insert(packages, name)
        end
        table.sort(packages)
        local targets = {}
        local target_kinds = {}
        for name, item in pairs(project.targets()) do
            table.insert(targets, name)
            target_kinds[name] = item:kind()
        end
        table.sort(targets)
        local script_sources = {}
        local scripting = project.targets()["vultra-scripting"]
        if scripting then
            script_sources = scripting:sourcefiles()
        end
        json.mark_as_array(script_sources)
        local research_sources = {}
        local research = project.targets()["vultra-research-extension"]
        if research then
            research_sources = research:sourcefiles()
        end
        json.mark_as_array(research_sources)
        local host_sources = {}
        local host = project.targets()["vultra-research-scripting"]
        if host then
            host_sources = host:sourcefiles()
        end
        json.mark_as_array(host_sources)
        io.writefile(path.join(library:targetdir(), "modules.json"), json.encode({
            features = library:values("vultra.features"),
            packages = packages,
            sources = library:sourcefiles(),
            targets = targets,
            scripting_sources = script_sources,
            research_sources = research_sources,
            research_host_sources = host_sources,
            target_kinds = target_kinds
        }))
    end)
target_end()

if features.research_quality or features.research_camera or features.research_project or features.research_graph then
    target("module-research")
        set_kind("binary")
        add_deps("vultra-research-extension")
        add_files("research.cpp")
        for _, name in ipairs({"quality", "camera", "project", "graph"}) do
            if features["research_" .. name] then
                add_defines("TEST_RESEARCH_" .. name:upper())
            end
        end
        if features.json then
            add_packages("nlohmann_json")
        end
    target_end()
end

if features.research_reference and features.asset_import and features.image_metrics then
    target("module-reference")
        set_kind("binary")
        add_deps("vultra-research-extension", "openpbr")
        add_files("../reference_path_tracer.cpp")
    target_end()
end

if features.research_experiment then
    target("module-experiment")
        set_kind("binary")
        add_deps("vultra-research-extension")
        add_files("../experiment_session.cpp", "../../examples/research/color_gain.cpp")
    target_end()
end

if features.research_api and features.research_project and features.api_host and features.image_metrics then
    target("module-research-extension")
        set_kind("binary")
        add_deps("vultra-research-extension")
        add_files("../research_extension.cpp")
    target_end()
end

if features.editor_gui and features.image_io then
    target("module-gui")
        set_kind("binary")
        add_deps("vultra")
        add_files("../gui_offscreen.cpp")
    target_end()
end

if features.world then
    target("module-world-ecs")
        set_kind("binary")
        add_deps("vultra")
        add_files("../ecs.cpp")
    target_end()
    target("module-world-execution")
        set_kind("binary")
        add_deps("vultra")
        add_files("../scene_execution.cpp")
        add_packages("nlohmann_json")
    target_end()
end

if features.rendering then
    target("module-rendering")
        set_kind("binary")
        add_deps("vultra", "openpbr")
        add_files("../rendering.cpp")
    target_end()
end

if features.asset_import then
    target("module-import")
        set_kind("binary")
        add_deps("vultra")
        add_files("import.cpp")
    target_end()
end

if features.material_values then
    target("module-material")
        set_kind("binary")
        add_deps("vultra")
        add_files("material.cpp")
    target_end()
end

if features.api_scene or features.api_ui then
    target("module-abi")
        set_kind("binary")
        add_deps("vultra")
        add_files("abi.cpp")
        if features.api_scene then
            add_defines("TEST_API_SCENE")
        end
        if features.api_ui then
            add_defines("TEST_API_UI")
        end
    target_end()
end

if features.api_host then
    target("module-plugin-session")
        set_kind("binary")
        add_deps("vultra")
        add_files("plugin_session.cpp")
        if features.api_ui then
            add_defines("TEST_API_UI")
        end
    target_end()
end

if features.script_host then
    if features.script_native then
        for _, name in ipairs({"module-native-code", "module-native-replacement"}) do
            target(name)
                set_kind("shared")
                set_default(false)
                add_deps("vultra-native-sdk")
                add_files("native.cpp")
                if name == "module-native-replacement" then
                    add_defines("TEST_REPLACEMENT")
                end
            target_end()
        end
    end
    if features.script_csharp then
        target("module-managed-assets")
            set_kind("phony")
            set_default(false)
            set_policy("build.fence", true)
            on_build(function (target)
                local root = path.absolute("../..", os.projectdir())
                local output = path.join(target:targetdir(), "managed")
                local artifacts = path.join(os.projectdir(), get_config("builddir"), "managed-artifacts")
                local host = path.join(root, "source/scripting/managed/Vultra.ManagedHost.csproj")
                local scripts = path.join(os.projectdir(), "managed/Vultra.ModuleScripts.csproj")
                os.execv("dotnet", {"build", host, "-c", "Release", "-o", output,
                                   "--artifacts-path", artifacts, "--nologo"})
                os.execv("dotnet", {"build", scripts, "-c", "Release", "-o", output,
                                   "--artifacts-path", artifacts, "--nologo"})
                os.execv("dotnet", {"build", scripts, "-c", "Release", "-o", path.join(output, "replacement"),
                                   "--artifacts-path", path.join(artifacts, "replacement"),
                                   "-p:ModuleReload=true", "--nologo"})
            end)
        target_end()
    end
    target("module-scripting")
        set_kind("binary")
        add_deps("vultra-scripting")
        add_files("scripting.cpp")
        for _, name in ipairs({"native", "lua", "csharp"}) do
            if features["script_" .. name] then
                add_defines("TEST_SCRIPT_" .. name:upper())
            end
        end
        if features.script_native then
            add_deps("module-native-code", "module-native-replacement", {inherit = false})
        end
        if features.script_csharp then
            add_deps("module-managed-assets", {inherit = false})
        end
        if features.api_ui then
            add_defines("TEST_API_UI")
        end
    target_end()
end

if features.game_shader then
    target("module-game-shader")
        set_kind("binary")
        add_deps("vultra")
        add_files("../shader_asset.cpp")
    target_end()
end

if features.asset_source then
    target("module-assets")
        set_kind("binary")
        add_deps("vultra")
        add_files("asset_source.cpp")
        if features.project then
            add_defines("TEST_PROJECT")
        end
    target_end()
end

if features.vgui then
    target("module-vgui")
        set_kind("binary")
        add_deps("vultra-vgui")
        add_files("vgui.cpp")
    target_end()
end
