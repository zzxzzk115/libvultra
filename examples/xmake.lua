print(get_config("project_dir"))

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

includes("window")
includes("rhi/triangle")
includes("imgui")
includes("framegraph/triangle")
includes("openxr/triangle")
includes("openxr/sponza")
includes("gltf_viewer")
includes("sponza")
includes("raytracing/triangle")
includes("raytracing/cornell_box")
includes("rayquery")
includes("rendergraph")
includes("meshshading/triangle")

target("normal-examples")
    set_kind("phony")
    on_run(function ()
        import("core.project.project")
        local examples = {
            "window",
            "rhi-triangle",
            "imgui",
            "framegraph-triangle",
            "openxr-triangle",
            "openxr-sponza",
            "gltf-viewer",
            "rendergraph",
        }
        for _, example in ipairs(examples) do
            os.execv("xmake", {"run", "example-" .. example})
        end
    end)
task_end()