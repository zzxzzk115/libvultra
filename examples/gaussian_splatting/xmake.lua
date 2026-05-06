target("example-gaussian-splatting")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")

    if is_plat("wasm") then
        add_rules("resources.vpk_pack", "wasm.link")

        local project_dir = os.projectdir()
        local generated_dir = path.join(project_dir,
                        "build",
                        ".generated",
                        "wasm_resources",
                        "example-gaussian-splatting")
        local output_vpk = path.join(generated_dir, "resources.vpk")

        set_values("vpk.project_dir", project_dir)
        set_values("vpk.resources_dir", path.join(project_dir, "resources"))
        set_values("vpk.generated_dir", generated_dir)
        set_values("vpk.mount_path", "/resources.vpk")
        set_values("vpk.enable_import", true)
        set_values("vpk.enable_pack", true)
        set_values("vpk.include_paths",
                   {
                       "scenes/3dgs_example.vmanifest",
                       "models/3dgs/hornedlizard.spz",
                   })
        set_values("wasm.vpk_path", output_vpk)
        set_values("wasm.vpk_mount", "/resources.vpk")
        set_values("wasm.shell_file", path.join(project_dir, "web", "emscripten_vultra.html"))
        set_values("wasm.imgui_ini", path.join(os.scriptdir(), "imgui.ini"))
        set_values("wasm.imgui_ini_mount", "/imgui.ini")
        -- Optional overrides:
        -- set_values("vpk.output_vpk", "<custom_output_vpk>")
        -- set_values("vpk.import_script", "<custom_import_script>")
        -- set_values("vpk.pack_script", "<custom_pack_script>")
        -- set_values("vpk.include_paths", {"scenes", "scripts", "models/DamagedHelmet", "models/Sponza", "textures"})
        -- set_values("vpk.pack_args", {"--include", "splats/demo"})
        -- set_values("wasm.vpk_path", "<prebuilt_vpk>")
        -- set_values("wasm.vpk_mount", "/resources.vpk")
        -- set_values("wasm.extra_ldflags", {"-sALLOW_MEMORY_GROWTH=1", ...})
    end

    add_files("imgui.ini")

    set_rundir("$(projectdir)")
    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-gaussian-splatting")
