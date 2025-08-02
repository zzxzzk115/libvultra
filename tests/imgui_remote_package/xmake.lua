add_requires("libvultra")

target("example-imgui-remote")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("libvultra")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-imgui-remote")