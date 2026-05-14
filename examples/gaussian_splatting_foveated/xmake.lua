target("example-gaussian-splatting-foveated")
    set_kind("binary")
    add_files("../gaussian_splatting/main.cpp")
    add_deps("vultra")
    add_defines("VULTRA_GAUSSIAN_FOVEATED_EXAMPLE=1")

    add_files("../gaussian_splatting/imgui.ini")

    set_rundir("$(projectdir)")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-gaussian-splatting-foveated")
