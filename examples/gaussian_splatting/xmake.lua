local vultra_project_dir = get_config("project_dir") or path.join(os.scriptdir(), "..", "..")
local generated_dir = path.join(vultra_project_dir,
                                "build", ".generated", "wasm_resources", "example-gaussian-splatting")
local output_vpk = path.join(generated_dir, "resources.vpk")

-- WASM only: cook the project's resources.vpk in a dedicated, input-guarded target so the
-- import+pack runs once (not on every binary build/run) and is decoupled from compilation.
if is_plat("wasm") then
    target("example-gaussian-splatting-resources")
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
                       "scenes/3dgs_example.vmanifest",
                       "models/3dgs/hornedlizard.spz",
                   })
end

target("example-gaussian-splatting")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("vultra")
    add_rules("vultra.builtin_pack")

    if is_plat("wasm") then
        add_deps("example-gaussian-splatting-resources")
        add_rules("wasm.link")
        set_values("wasm.vpk_path", output_vpk)
        set_values("wasm.vpk_mount", "/resources.vpk")
        set_values("wasm.shell_file", path.join(vultra_project_dir, "web", "emscripten_vultra.html"))
        set_values("wasm.imgui_ini", path.join(os.scriptdir(), "imgui.ini"))
        set_values("wasm.imgui_ini_mount", "/imgui.ini")
        -- Optional overrides on the -resources target:
        -- set_values("vpk.output_vpk", "<custom_output_vpk>")
        -- set_values("vpk.import_script", "<custom_import_script>")
        -- set_values("vpk.pack_script", "<custom_pack_script>")
        -- set_values("vpk.pack_args", {"--include", "splats/demo"})
    end

    add_files("imgui.ini")

    set_rundir(vultra_project_dir)
    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-gaussian-splatting")
