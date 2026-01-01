target("example-gaussian-splatting")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-gaussian-splatting")