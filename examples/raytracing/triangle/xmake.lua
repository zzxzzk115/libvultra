target("example-raytracing-triangle")
    set_kind("binary")
    add_files("main.cpp", "imgui.ini")
    add_deps("vultra")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-raytracing-triangle")