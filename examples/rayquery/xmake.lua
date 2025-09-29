target("example-rayquery")
    set_kind("binary")
    add_files("main.cpp", "imgui.ini")
    add_deps("vultra")

    -- add resource files to be copied after build
    add_values("resource_files", "resources/models/raytracing_shadow/**")

    add_rules("copy_resources")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-rayquery")