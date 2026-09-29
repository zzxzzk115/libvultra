-- Shared build dependencies: prepare each source once even when several examples use it.
local function asset_target(name, source)
    target(name)
        set_kind("phony")
        set_default(false)
        add_deps("vultra-import", {inherit = false})
        on_build(function (target)
            import("private.action.run.runenvs")
            local importer = target:dep("vultra-import")
            local addenvs, setenvs = runenvs.make(importer)
            print("Preparing asset: %s", source)
            os.execv(path.absolute(importer:targetfile()),
                {source, "--cache-dir", path.join(os.projectdir(), ".vultra", "assets")},
                {curdir = os.projectdir(), addenvs = addenvs, setenvs = setenvs})
        end)
    target_end()
end

asset_target("asset-damaged-helmet", "resources/models/DamagedHelmet/DamagedHelmet.glb")
asset_target("asset-sponza", "resources/models/Sponza/Sponza.gltf")
asset_target("asset-rayquery", "resources/models/raytracing_shadow/raytracing_shadow.gltf")
asset_target("asset-cornell-box", "resources/models/CornellBox/CornellBox-Original.obj")

target("example-assets")
    set_kind("phony")
    set_default(false)
    add_deps("asset-damaged-helmet", "asset-sponza", "asset-rayquery", "asset-cornell-box", {inherit = false})
target_end()

target("example-research")
    set_kind("binary")
    set_default(true)
    add_deps("vultra")
    add_files("research/main.cpp")
    set_rundir("$(projectdir)")
target_end()

target("example-common")
    set_kind("static")
    set_default(false)
    add_deps("vultra", {public = true})
    add_files("common/colored_mesh.cpp")
target_end()

local samples = {
    {"window", "window/main.cpp"},
    {"rhi-triangle", "rhi/triangle/main.cpp"},
    {"imgui", "imgui/main.cpp"},
    {"rendergraph-triangle", "render_graph/triangle/main.cpp"},
    {"debugdraw", "debug_draw/main.cpp"},
    {"meshshading-triangle", "mesh_shading/triangle/main.cpp"}
}
for _, sample in ipairs(samples) do
    target("example-" .. sample[1])
        set_kind("binary")
        set_default(true)
        add_deps("example-common")
        add_files(sample[2])
        if sample[1] == "debugdraw" then
            add_deps("vultra-renderer")
            add_deps("asset-damaged-helmet", {inherit = false})
        end
        set_rundir("$(projectdir)")
    target_end()
end

target("example-gltf-viewer")
    set_kind("binary")
    set_default(true)
    add_deps("vultra-renderer", "example-common")
    add_deps("asset-damaged-helmet", {inherit = false})
    add_files("gltf_viewer/main.cpp")
    set_rundir("$(projectdir)")
target_end()

if has_config("libvultra_with_openxr") then
    target("example-xr-common")
        set_kind("static")
        set_default(false)
        add_deps("vultra", {public = true})
        add_files("common/xr_sample.cpp")
    target_end()
    target("example-openxr-sponza")
        set_kind("binary")
        set_default(true)
        add_deps("example-xr-common", "vultra-renderer")
        add_deps("asset-sponza", {inherit = false})
        add_files("xr/sponza/main.cpp")
        set_rundir("$(projectdir)")
    target_end()
    target("example-openxr-triangle")
        set_kind("binary")
        set_default(true)
        add_deps("example-xr-common")
        add_files("xr/main.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

target("example-sponza")
    set_kind("binary")
    set_default(true)
    add_deps("vultra-renderer", "example-common")
    add_deps("asset-sponza", {inherit = false})
    add_files("sponza/main.cpp")
    set_rundir("$(projectdir)")
target_end()

target("example-meshshading-sponza")
    set_kind("binary")
    set_default(true)
    add_deps("vultra-renderer", "example-common")
    add_deps("asset-sponza", {inherit = false})
    add_files("mesh_shading/sponza/main.cpp")
    set_rundir("$(projectdir)")
target_end()

target("example-ray-common")
    set_kind("static")
    set_default(false)
    add_deps("vultra-renderer", {public = true})
    add_files("common/ray_scene.cpp", "common/ray_tracing_app.cpp")
target_end()

target("example-rayquery")
    set_kind("binary")
    set_default(true)
    add_deps("example-ray-common")
    add_deps("asset-rayquery", {inherit = false})
    add_files("ray_query/main.cpp")
    set_rundir("$(projectdir)")
target_end()

for _, sample in ipairs({{"triangle", "triangle"}, {"cornell-box", "cornell_box"}}) do
    target("example-raytracing-" .. sample[1])
        set_kind("binary")
        set_default(true)
        add_deps("example-ray-common")
        if sample[1] == "cornell-box" then
            add_deps("asset-cornell-box", {inherit = false})
        end
        add_files("ray_tracing/" .. sample[2] .. "/main.cpp")
        set_rundir("$(projectdir)")
    target_end()
end
