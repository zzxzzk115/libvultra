-- NOTE: the shared WASM packaging rules (resources.vpk_pack / wasm.link) now live in
-- xmake/rules/wasm.lua, included from the root xmake.lua so both examples and the
-- standalone runtime/export-template targets can use them.

rule("copy_resources")
	after_build(function (target)
        local resource_files = target:values("resource_files")
        if resource_files then
            for _, pattern in ipairs(resource_files) do
                pattern = path.join(get_config("project_dir"), pattern)
                local files = os.files(pattern)
                for _, file in ipairs(files) do
                    local relpath = path.relative(file, get_config("project_dir"))
                    local target_dir = path.join(target:targetdir(), path.directory(relpath))
                    os.mkdir(target_dir)
                    os.cp(file, target_dir)
                    print("Copying resource file: " .. file .. " -> " .. target_dir)
                end
            end
        end
    end)

    after_install(function (target)
        local resource_files = target:values("resource_files")
        if resource_files then
            for _, pattern in ipairs(resource_files) do
                pattern = path.join(get_config("project_dir"), pattern)
                local files = os.files(pattern)
                for _, file in ipairs(files) do
                    local relpath = path.relative(file, get_config("project_dir"))
                    local target_dir = path.join(target:installdir(), "bin", path.directory(relpath))
                    os.mkdir(target_dir)
                    os.cp(file, target_dir)
                    print("Copying resource file: " .. file .. " -> " .. target_dir)
                end
            end
        end
    end)
rule_end()

if is_plat("wasm") then
    includes("demo_app")
    includes("imgui")
    includes("gaussian_splatting")
else
    includes("window")
    includes("rhi/triangle")
    includes("imgui")
    includes("framegraph/triangle")
    includes("openxr/triangle")
    includes("openxr/sponza")
    includes("openxr/gaussian_splatting")
    includes("gltf_viewer")
    includes("sponza")
    includes("raytracing/triangle")
    includes("raytracing/cornell_box")
    includes("rayquery")
    includes("meshshading/triangle")
    -- includes("debug_draw")
    includes("gaussian_splatting")
    includes("demo_app")
    includes("plugins/native_math")
    if is_plat("android") then
        includes("android_app")
    end
end
