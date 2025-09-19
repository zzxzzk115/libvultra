target("example-gltf-viewer")
    set_kind("binary")
    add_files("main.cpp", "imgui.ini")
    add_deps("vultra")

    set_values("resource_files", "resources/models/DamagedHelmet/**", "resources/textures/environment_maps/**")

    add_rules("copy_resources")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-gltf-viewer")