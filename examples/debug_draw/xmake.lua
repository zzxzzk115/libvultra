target("example-debugdraw")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")
    add_rules("vultra.builtin_pack")

    set_values("resource_files", "resources/models/DamagedHelmet/**", "resources/textures/environment_maps/**")

    add_rules("copy_resources")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-debugdraw")
