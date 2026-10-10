target("vultra-import")
    set_kind("binary")
    set_default(false)
    add_deps("vultra")
    add_files("import_asset.cpp")
    set_rundir("$(projectdir)")
target_end()

target("vultra-batch")
    add_packages("nlohmann_json")
    set_kind("binary")
    set_default(false)
    add_rules("vultra.linux.delivery")
    add_deps("vultra-scripting", "vultra-builtin-pack")
    add_files("offline_render.cpp", "../examples/research/color_gain.cpp")
    add_rules("utils.bin2c", {extensions = ".slang"})
    add_files("../examples/research/shaders/color_gain.slang")
    if is_plat("windows") then
        add_files("../runtime/src/builtin_pack.rc")
    elseif is_plat("linux", "macosx") then
        add_files("../runtime/src/builtin_pack.S")
    end
    if is_plat("linux") then
        add_ldflags("-static-libgcc", "-static-libstdc++")
    end
    set_rundir("$(projectdir)")
target_end()

target("vultra-pack")
    set_kind("binary")
    -- The runtime builtin-pack target executes this tool before compiling its embedded resource.
    set_policy("build.fence", true)
    set_default(false)
    add_deps("vultra")
    add_files("pack_project.cpp")
    set_rundir("$(projectdir)")
target_end()

target("vultra-research")
    set_kind("shared")
    set_default(false)
    add_rules("vultra.linux.delivery")
    add_deps("vultra-scripting", "vultra-builtin-pack")
    add_defines("VULTRA_RESEARCH_BUILD")
    add_files("research_host.cpp", "../examples/research/color_gain.cpp")
    add_rules("utils.bin2c", {extensions = ".slang"})
    add_files("../examples/research/shaders/color_gain.slang")
    if is_plat("windows") then
        add_files("../runtime/src/builtin_pack.rc")
    elseif is_plat("linux", "macosx") then
        add_files("../runtime/src/builtin_pack.S")
    end
    set_rundir("$(projectdir)")
target_end()

target("vultra-shader")
    set_kind("binary")
    set_default(false)
    add_deps("vultra")
    add_packages("nlohmann_json")
    add_files("cook_shader.cpp")
    set_rundir("$(projectdir)")
target_end()

target("vultra-sdk")
    set_kind("phony")
    set_default(false)
    set_policy("build.fence", true)
    add_deps("vultra")
    on_build(function (target)
        local include = path.join(os.projectdir(), "build", "sdk", "include")
        os.mkdir(include)
        local function copyTree(source, destination)
            for _, file in ipairs(os.files(path.join(source, "**"))) do
                local output = path.join(destination, path.relative(file, source))
                os.mkdir(path.directory(output))
                os.cp(file, output)
            end
        end
        for _, name in ipairs({"native_plugin.h", "research_api.h", "research_editor_api.h", "vultra_abi.generated.h",
                               "vultra_scene.generated.h", "vultra_ui.generated.h"}) do
            os.cp(path.join(os.projectdir(), "source", "api", "include", "vultra", "api", name),
                  path.join(include, "vultra", "api", name))
        end
        local shaders = path.join(os.projectdir(), "build", "sdk", "shaders")
        for _, name in ipairs({"lib", "resources"}) do
            copyTree(path.join(os.projectdir(), "builtin", "shaders", name),
                     path.join(shaders, "builtin", "shaders", name))
        end
        copyTree(path.join(os.projectdir(), "external", "openpbr"), path.join(shaders, "external", "openpbr"))
        local package = target:dep("vultra"):pkg("vri")
        assert(package, "VRI package is required to export the SDK")
        copyTree(path.join(package:installdir(), "include", "vri"), path.join(include, "vri"))
        for _, name in ipairs({"LICENSE", "README.md"}) do
            if os.isfile(path.join(package:installdir(), name)) then
                os.cp(path.join(package:installdir(), name), path.join(os.projectdir(), "build", "sdk", "vri-" .. name))
            end
        end
        os.cp(path.join(os.projectdir(), "external", "vri_license.txt"), path.join(os.projectdir(), "build", "sdk", "vri-LICENSE"))
        os.cp(path.join(os.projectdir(), "LICENSE"), path.join(os.projectdir(), "build", "sdk", "LICENSE"))
    end)
target_end()
