set_project("vultra-module-probe")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "$(builddir)"})
includes("../..")

local features = vultraFeatures()
if features.builtin_pack then
    target("module-builtin-pack")
        set_kind("binary")
        add_deps("vultra")
        add_files("../../tools/pack_builtins.cpp")
    target_end()
end

if features.random then
    target("module-random")
        set_kind("binary")
        add_deps("vultra")
        add_files("../random.cpp")
    target_end()
end
if features.meshlet_build then
    target("module-meshlet-build")
        set_kind("binary")
        add_deps("vultra")
        add_files("../meshlet_data.cpp")
    target_end()
end
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
        add_files("../../examples/offline/color_gain.cpp")
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
        io.writefile(path.join(library:targetdir(), "modules.json"), json.encode({
            features = library:values("vultra.features"),
            packages = packages,
            sources = library:sourcefiles(),
            targets = targets,
            target_kinds = target_kinds
        }))
    end)
target_end()

if features.editor_gui and features.image_io then
    target("module-gui")
        set_kind("binary")
        add_deps("vultra")
        add_files("../gui_offscreen.cpp")
    target_end()
    target("module-gui-lifetime")
        set_kind("binary")
        add_deps("vultra")
        add_files("../gui_lifetime.cpp")
    target_end()
end

if features.properties then
    target("module-property-schema")
        set_kind("binary")
        add_deps("vultra")
        add_packages("nlohmann_json")
        add_files("../property_schema.cpp")
    target_end()
end

if features.rendering and features.scene_prepare and features.shader then
    target("module-rendering")
        set_kind("binary")
        add_deps("vultra", "openpbr")
        add_files("../rendering.cpp")
    target_end()
end

if features.rendering and not features.shader and not features.scene_prepare then
    target("module-rendering-runtime")
        set_kind("binary")
        add_deps("vultra")
        add_files("../rendering_runtime.cpp")
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

if features.game_shader then
    target("module-game-shader")
        set_kind("binary")
        add_deps("vultra")
        add_files("../shader_asset.cpp")
    target_end()
end

if features.asset_source then
    target("module-vpk-memory")
        set_kind("binary")
        add_deps("vultra")
        add_files("../vpk_memory.cpp")
    target_end()
    target("module-assets")
        set_kind("binary")
        add_deps("vultra")
        add_files("asset_source.cpp")
    target_end()
end

if features.vgui then
    target("module-vgui")
        set_kind("binary")
        add_deps("vultra")
        add_files("vgui.cpp")
    target_end()
end
