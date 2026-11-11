local features = vultraFeatures()
local shader_compiler = features.shader and has_config("libvultra_shader_compiler")
local asset_import = features.assets and has_config("libvultra_asset_import")

if asset_import then
    target("vultra-import")
        set_kind("binary")
        set_default(false)
        add_deps("vultra")
        add_files("import_asset.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

if shader_compiler then
    target("vultra-shader")
        set_kind("binary")
        set_default(false)
        add_deps("vultra")
        add_packages("nlohmann_json")
        add_files("cook_shader.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

if shader_compiler and features.assets then
    target("vultra-builtins")
        set_kind("binary")
        set_default(false)
        set_policy("build.fence", true)
        add_deps("vultra")
        add_files("pack_builtins.cpp")
        set_rundir("$(projectdir)")
    target_end()
end
