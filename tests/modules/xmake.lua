set_project("vultra-module-probe")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "$(builddir)"})
includes("../..")

local features = vultraFeatures()
local shader_compiler = features.shader and has_config("libvultra_shader_compiler")
local asset_import = features.assets and has_config("libvultra_asset_import")

target("module-probe")
    set_kind("binary")
    add_deps("vultra")
    add_files("probe.cpp")
    for _, name in ipairs({"platform", "gpu", "graph"}) do
        if features[name] then
            add_defines("TEST_" .. name:upper())
        end
    end
    if features.graph then
        add_defines("TEST_GRAPH_JSON")
    end
    if shader_compiler then
        add_defines("TEST_SHADER")
    end
    if features.app and not is_plat("android", "wasm") then
        add_defines("TEST_DESKTOP")
    end
    if features.graph and shader_compiler then
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

if features.ui then
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

if features.render and asset_import and shader_compiler then
    target("module-rendering")
        set_kind("binary")
        add_deps("vultra", "openpbr")
        add_files("../render_path.cpp")
    target_end()
end

if features.assets then
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

if asset_import then
    target("module-import")
        set_kind("binary")
        add_deps("vultra")
        add_files("import.cpp")
    target_end()
    target("module-meshlet-build")
        set_kind("binary")
        add_deps("vultra")
        add_files("../meshlet_data.cpp")
    target_end()
end

if features.assets and shader_compiler then
    target("module-builtin-pack")
        set_kind("binary")
        add_deps("vultra")
        add_files("../../tools/pack_builtins.cpp")
    target_end()
end
