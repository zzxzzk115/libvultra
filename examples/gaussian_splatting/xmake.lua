target("example-gaussian-splatting")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    -- add resource files to be copied after build
    add_values("resource_files", "resources/models/**.spz")

    add_rules("copy_resources")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-gaussian-splatting")