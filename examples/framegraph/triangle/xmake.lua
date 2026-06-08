target("example-framegraph-triangle")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")
    add_rules("vultra.builtin_pack")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-framegraph-triangle")
