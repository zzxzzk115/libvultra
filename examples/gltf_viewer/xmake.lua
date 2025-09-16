target("example-gltf-viewer")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    set_values("resource_files", "resources/models/DamagedHelmet/**")

    add_rules("copy_resources")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-gltf-viewer")