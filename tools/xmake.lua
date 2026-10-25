local features = vultraFeatures()

if features.tool_import then
    target("vultra-import")
        set_kind("binary")
        set_default(false)
        add_deps("vultra")
        add_files("import_asset.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

if features.tool_shader then
    target("vultra-shader")
        set_kind("binary")
        set_default(false)
        add_deps("vultra")
        add_packages("nlohmann_json")
        add_files("cook_shader.cpp")
        set_rundir("$(projectdir)")
    target_end()
end

if features.builtin_pack then
    target("vultra-builtins")
        set_kind("binary")
        set_default(false)
        set_policy("build.fence", true)
        add_deps("vultra")
        add_files("pack_builtins.cpp")
        set_rundir("$(projectdir)")
    target_end()
end
