target("test-audio")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    add_rules("linux.sdl.driver")

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test-audio")
