target("vultra-app")
    set_kind("binary")
    set_default(false)
    add_deps("vultra", "vultra-builtin-pack")
    add_packages("nlohmann_json", "imgui-node-editor")
    add_files("src/*.cpp", "../examples/research/color_gain.cpp")
    add_rules("utils.bin2c", {extensions = ".slang"})
    add_files("../examples/research/shaders/color_gain.slang")
    if is_plat("windows") then
        add_files("../runtime/src/builtin_pack.rc")
    elseif is_plat("linux", "macosx") then
        add_files("../runtime/src/builtin_pack.S")
    end
    set_rundir("$(projectdir)")
target_end()
