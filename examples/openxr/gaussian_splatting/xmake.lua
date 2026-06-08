target("example-openxr-gaussian-splatting")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")
    add_rules("vultra.builtin_pack")

    set_rundir("$(projectdir)")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-openxr-gaussian-splatting")
