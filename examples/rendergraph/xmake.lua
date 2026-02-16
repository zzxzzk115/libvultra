add_requires("nlohmann_json")

target("example-rendergraph")
    set_kind("binary")
    add_headerfiles("*.hpp")
    add_files("*.cpp")
    add_deps("vultra")

    add_packages("nlohmann_json", {public = true})

    add_rules("vfg")

    add_files("imgui.ini", "graph.vfg")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-rendergraph")