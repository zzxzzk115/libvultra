target("example-openxr-sponza")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")
    add_rules("vultra.builtin_pack")

    add_files("imgui.ini")

    set_rundir(get_config("project_dir") or path.join(os.scriptdir(), "..", "..", ".."))

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-openxr-sponza")
