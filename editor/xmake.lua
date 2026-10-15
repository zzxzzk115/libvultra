target("vultra-app")
    set_kind("binary")
    set_default(false)
    add_deps("vultra-research-extension", "vultra-builtin-pack", "flip")
    add_packages("nlohmann_json", "imgui-node-editor", "vtask", "xxhash")
    add_files("src/main.cpp", "src/graph_editor.cpp", "src/research_workspace.cpp", "src/scene_inspector.cpp",
              "../examples/research/color_gain.cpp")
    if vultraFeatures().research_stereo then
        add_defines("VULTRA_RESEARCH_STEREO")
        add_files("src/research_project*.cpp")
    end
    add_rules("utils.bin2c", {extensions = ".slang"})
    add_files("../examples/research/shaders/color_gain.slang")
    if is_plat("windows") then
        add_files("../builtin/builtin_pack.rc")
    elseif is_plat("linux", "macosx") then
        add_files("../builtin/builtin_pack.S")
    end
    set_rundir("$(projectdir)")
target_end()
