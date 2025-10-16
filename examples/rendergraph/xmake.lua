target("example-rendergraph")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    add_rules("imguiconfig")

    add_files("imgui.ini", "NodeEditor.json")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-rendergraph")