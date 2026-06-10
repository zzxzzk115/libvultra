if not is_plat("android") then
    local vultra_project_dir = get_config("project_dir") or path.join(os.scriptdir(), "..", "..")
    local generated_dir = path.join(vultra_project_dir,
                                    "build", ".generated", "wasm_resources", "example-audio")
    local output_vpk = path.join(generated_dir, "resources.vpk")

    if is_plat("wasm") then
        target("example-audio-resources")
            set_kind("phony")
            set_default(false)
            add_rules("resources.vpk_pack")
            set_values("vpk.project_dir", vultra_project_dir)
            set_values("vpk.resources_dir", path.join(vultra_project_dir, "resources"))
            set_values("vpk.generated_dir", generated_dir)
            set_values("vpk.mount_path", "/resources.vpk")
            set_values("vpk.enable_import", true)
            set_values("vpk.enable_pack", true)
            set_values("vpk.include_paths",
                       {
                           "sounds",
                       })
    end

    target("example-audio")
        set_kind("binary")
        add_files("main.cpp")
        add_deps("vultra")
        add_rules("vultra.builtin_pack")

        if is_plat("wasm") then
            add_deps("example-audio-resources")
            add_rules("wasm.link")
            set_values("wasm.vpk_path", output_vpk)
            set_values("wasm.vpk_mount", "/resources.vpk")
            set_values("wasm.shell_file", path.join(vultra_project_dir, "web", "emscripten_vultra.html"))
        end

        set_rundir(vultra_project_dir)
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-audio")
end
