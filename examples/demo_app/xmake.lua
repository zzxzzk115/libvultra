target("example-demo-app")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    set_rundir("$(projectdir)")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-demo-app")