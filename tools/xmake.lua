target("vultra-import")
    set_kind("binary")
    set_default(false)
    add_deps("vultra")
    add_files("import_asset.cpp")
    set_rundir("$(projectdir)")
target_end()

target("vultra-batch")
    set_kind("binary")
    set_default(false)
    add_rules("vultra.linux.delivery")
    add_deps("vultra-scripting", "vultra-builtin-pack")
    add_files("offline_render.cpp", "../examples/research/color_gain.cpp")
    add_rules("utils.bin2c", {extensions = ".slang"})
    add_files("../examples/research/shaders/color_gain.slang")
    if is_plat("windows") then
        add_files("../runtime/src/builtin_pack.rc")
    elseif is_plat("linux", "macosx") then
        add_files("../runtime/src/builtin_pack.S")
    end
    if is_plat("linux") then
        add_ldflags("-static-libgcc", "-static-libstdc++")
    end
    set_rundir("$(projectdir)")
target_end()

target("vultra-pack")
    set_kind("binary")
    -- The runtime builtin-pack target executes this tool before compiling its embedded resource.
    set_policy("build.fence", true)
    set_default(false)
    add_deps("vultra")
    add_files("pack_project.cpp")
    set_rundir("$(projectdir)")
target_end()

target("vultra-research")
    set_kind("shared")
    set_default(false)
    add_rules("vultra.linux.delivery")
    add_deps("vultra-scripting", "vultra-builtin-pack")
    add_defines("VULTRA_RESEARCH_BUILD")
    add_files("research_host.cpp", "../examples/research/color_gain.cpp")
    add_rules("utils.bin2c", {extensions = ".slang"})
    add_files("../examples/research/shaders/color_gain.slang")
    if is_plat("windows") then
        add_files("../runtime/src/builtin_pack.rc")
    elseif is_plat("linux", "macosx") then
        add_files("../runtime/src/builtin_pack.S")
    end
    set_rundir("$(projectdir)")
target_end()
