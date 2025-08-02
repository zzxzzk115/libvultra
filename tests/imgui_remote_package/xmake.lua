add_requires("libvultra", {configs = {debug = is_mode("debug")}})

target("example-imgui-remote")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("libvultra")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-imgui-remote")