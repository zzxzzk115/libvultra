target("example-imgui")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    if is_plat("wasm") then
        add_rules("wasm.link")
    end

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-imgui")