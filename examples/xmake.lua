local selected = vultraExamples()
local features = vultraFeatures()
local assets = {}

-- Shared preparation is registered only for assets used by the selected modes.
local function asset_target(name, source)
    if assets[name] then
        return
    end
    assets[name] = true
    target(name)
        set_kind("phony")
        set_default(false)
        add_deps("vultra-import", {inherit = false})
        on_build(function (target)
            import("private.action.run.runenvs")
            local importer = target:dep("vultra-import")
            local addenvs, setenvs = runenvs.make(importer)
            os.execv(path.absolute(importer:targetfile()),
                {source, "--cache-dir", path.join(os.projectdir(), ".vultra", "assets")},
                {curdir = os.projectdir(), addenvs = addenvs, setenvs = setenvs})
        end)
    target_end()
end

local function has_group(name)
    for id in pairs(selected) do
        if id == name or id:startswith(name .. ".") then
            return true
        end
    end
    return false
end

-- The source and mode table use the same explicit selection, rather than unused link objects.
local function add_mode(group, name, source)
    if selected[group .. "." .. name] then
        add_files(group .. "/" .. source .. ".cpp")
        add_defines("VULTRA_EXAMPLE_" .. name:upper():gsub("-", "_"))
    end
end

if selected["basics.vri"] or selected["basics.graph"] or selected.offline or has_group("ui") or
   has_group("scene") or selected.scripting then
    target("example-common")
        set_kind("static")
        set_default(false)
        add_deps("vultra", {public = true})
        add_files("common/colored_mesh.cpp")
    target_end()
end

if has_group("basics") then
    target("example-basics")
        set_kind("binary")
        add_deps("vultra")
        if selected["basics.vri"] or selected["basics.graph"] then
            add_deps("example-common")
        end
        add_files("basics/main.cpp")
        add_mode("basics", "window", "window")
        add_mode("basics", "vri", "vri_triangle")
        add_mode("basics", "graph", "render_graph")
        add_mode("basics", "mesh-shading", "mesh_shader")
        set_rundir("$(projectdir)")
    target_end()
end

if selected.offline then
    target("example-offline")
        set_kind("binary")
        add_deps("example-common")
        add_files("offline/main.cpp", "research/color_gain.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

if has_group("ui") then
    target("example-ui")
        set_kind("binary")
        add_deps("example-common", "vultra-vgui")
        if features.native_plugin and features.api_ui then
            add_defines("VULTRA_EXAMPLE_NATIVE_PLUGIN")
            add_deps("example-native-plugin", {inherit = false})
        end
        add_files("ui/main.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

if (has_group("ui") and features.native_plugin and features.api_ui) or selected.scripting then
    target("example-native-plugin")
        set_kind("shared")
        set_default(false)
        set_languages("c11")
        add_deps("vultra-native-sdk")
        add_files("ui/native_plugin.c")
    target_end()
end

if has_group("scene") then
    if selected["scene.helmet"] or selected["scene.debug"] then
        asset_target("asset-damaged-helmet", "resources/models/DamagedHelmet/DamagedHelmet.glb")
    end
    if selected["scene.sponza"] or selected["scene.sponza-mesh-shading"] then
        asset_target("asset-sponza", "resources/models/Sponza/Sponza.gltf")
    end
    target("example-scene")
        set_kind("binary")
        add_deps("example-common")
        if selected["scene.helmet"] or selected["scene.debug"] then
            add_deps("asset-damaged-helmet", {inherit = false})
        end
        if selected["scene.sponza"] or selected["scene.sponza-mesh-shading"] then
            add_deps("asset-sponza", {inherit = false})
        end
        add_files("scene/main.cpp")
        add_mode("scene", "helmet", "helmet")
        add_mode("scene", "debug", "debug_draw")
        add_mode("scene", "sponza", "sponza")
        add_mode("scene", "sponza-mesh-shading", "meshlets")
        set_rundir("$(projectdir)")
    target_end()
end

if has_group("ray") then
    target("example-ray-common")
        set_kind("static")
        set_default(false)
        add_deps("vultra", {public = true})
        add_files("common/ray_scene.cpp")
        if selected["ray.triangle"] or selected["ray.cornell"] then
            add_files("common/ray_tracing_app.cpp")
        end
    target_end()
    if selected["ray.cornell"] then
        asset_target("asset-cornell-box", "resources/models/CornellBox/CornellBox-Original.obj")
    end
    if selected["ray.query"] then
        asset_target("asset-rayquery", "resources/models/raytracing_shadow/raytracing_shadow.gltf")
    end
    target("example-ray")
        set_kind("binary")
        add_deps("example-ray-common")
        if selected["ray.cornell"] then
            add_deps("asset-cornell-box", {inherit = false})
        end
        if selected["ray.query"] then
            add_deps("asset-rayquery", {inherit = false})
        end
        add_files("ray/main.cpp")
        add_mode("ray", "triangle", "triangle")
        add_mode("ray", "cornell", "cornell_box")
        add_mode("ray", "query", "query")
        set_rundir("$(projectdir)")
    target_end()
end

if has_group("xr") then
    target("example-xr-common")
        set_kind("static")
        set_default(false)
        add_deps("vultra", {public = true})
        add_files("common/xr_sample.cpp")
    target_end()
    if selected["xr.sponza"] then
        asset_target("asset-sponza", "resources/models/Sponza/Sponza.gltf")
    end
    target("example-xr")
        set_kind("binary")
        add_deps("example-xr-common")
        if selected["xr.sponza"] then
            add_deps("asset-sponza", {inherit = false})
        end
        add_files("xr/main.cpp")
        add_mode("xr", "triangle", "triangle")
        add_mode("xr", "sponza", "sponza")
        set_rundir("$(projectdir)")
    target_end()
end

if selected.shader then
    target("example-shaders")
        set_kind("phony")
        set_default(false)
        add_deps("vultra-shader", {inherit = false})
        on_build(function (target)
            local cooker = target:dep("vultra-shader")
            os.execv(path.absolute(cooker:targetfile()),
                {"examples/shader/painted_metal.vshader", "--output", "build/shaders/examples/painted_metal.vshaderc",
                 "--include", "builtin/shaders", "--include", "external"}, {curdir = os.projectdir()})
        end)
    target_end()
    target("example-shader")
        set_kind("binary")
        add_deps("vultra")
        add_deps("example-shaders", {inherit = false})
        add_files("shader/main.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

if selected.research then
    asset_target("asset-damaged-helmet", "resources/models/DamagedHelmet/DamagedHelmet.glb")
    target("example-research")
        set_kind("binary")
        add_deps("vultra")
        add_deps("asset-damaged-helmet", {inherit = false})
        add_files("research/main.cpp", "research/color_gain.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

if selected.scripting then
    target("example-native-cpp-plugin")
        set_kind("shared")
        set_default(false)
        add_deps("vultra-native-sdk")
        add_files("scripting/native_cpp.cpp")
    target_end()
    target("example-scripting")
        set_kind("binary")
        set_default(false)
        add_deps("example-common", "vultra-scripting")
        add_deps("vultra-managed-host", {inherit = false})
        add_deps("example-native-cpp-plugin", "example-native-plugin", {inherit = false})
        add_files("scripting/main.cpp")
        set_rundir("$(projectdir)")
        before_build(function ()
            local output = path.join(os.projectdir(), "build", ".tmp", "scripting-managed")
            os.mkdir(output)
            os.execv("dotnet", {"build", path.join(os.projectdir(), "examples", "scripting", "csharp", "VultraScript.csproj"),
                "-c", "Release", "-o", output, "--no-dependencies"})
        end)
    target_end()
end

if not table.empty(assets) then
    target("example-assets")
        set_kind("phony")
        set_default(false)
        for name in pairs(assets) do
            add_deps(name, {inherit = false})
        end
    target_end()
end
