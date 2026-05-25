add_requires("libvultra", {configs = {debug = is_mode("debug"), tracky = has_config("tracky"), tracy = has_config("tracy")}})

target("test-imgui-remote")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("libvultra")

    -- add rules
    add_rules("linux.sdl.driver")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test-imgui-remote")
