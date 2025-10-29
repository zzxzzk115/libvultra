target("test-event-center")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    -- add rules
    add_rules("linux.sdl.driver")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test-event-center")