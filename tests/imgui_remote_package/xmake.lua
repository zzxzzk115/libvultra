-- NOTE: Renderdoc is not supported on Wayland, so we disable it when Wayland is enabled.
disable_renderdoc = is_plat("linux") and has_config("wayland")
add_requires("libvultra", {configs = {renderdoc = not disable_renderdoc, debug = is_mode("debug"), tracky = has_config("tracky"), tracy = has_config("tracy")}})

target("test-imgui-remote")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("libvultra")

    -- add rules
    add_rules("linux.sdl.driver")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test-imgui-remote")