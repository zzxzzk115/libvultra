target("vultra")
    set_kind("static")
    if is_plat("linux", "macosx") then
        -- The optional research shared library embeds this static core.
        add_cxflags("-fPIC")
    end

    -- One public library; the directories express dependency boundaries in source.
    for _, module in ipairs({"core", "platform", "drivers", "assets", "servers", "scene", "ui", "main", "api"}) do
        add_includedirs(module .. "/include", {public = true})
        add_headerfiles(module .. "/include/(vultra/**.hpp)")
    end
    add_includedirs("assets/src")
    add_includedirs("drivers/src")
    add_files("core/src/**.cpp", "drivers/src/rhi/**.cpp", "drivers/src/profiling/**.cpp", "assets/src/**.cpp")
    add_files("servers/src/**.cpp", "scene/src/**.cpp", "ui/src/editor_gui*.cpp", "main/src/**.cpp", "api/src/**.cpp")
    add_files("platform/src/input/**.cpp", "platform/src/os/**.cpp")

    local window_backend = get_config("libvultra_window_backend") or "glfw"
    add_files("platform/src/" .. window_backend .. "/**.cpp")
    add_files("ui/src/" .. window_backend .. "/**.cpp")
    add_defines("VULTRA_WINDOW_" .. window_backend:upper(), {public = true})
    add_packages(window_backend == "sdl3" and "libsdl3" or "glfw", {public = true})

    if is_plat("windows") then
        add_files("platform/src/windows/**.cpp")
        add_syslinks("comdlg32", "ole32")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", {public = true})
    elseif is_plat("linux") then
        add_files("platform/src/linux/**.cpp")
        add_packages("nativefiledialog-extended")
        add_syslinks("dl")
    end

    add_deps("filewatch", "renderdoc-api", "bc7enc", "openpbr")
    add_packages("vri", "imgui", "slang-static", "argparse", "spdlog", "glm", {public = true})
    add_packages("stb", "tinygltf", "tinyobjloader", "xxhash", "vtask", "openfbx", "directxtex", "meshoptimizer")
    add_packages("antlr4-runtime")
    add_includedirs("$(builddir)/generated/shaders")
    before_build(function (target)
        local inputs = os.files(path.join(os.projectdir(), "source/assets/src/shaders/*"))
        table.join2(inputs, os.files(path.join(os.projectdir(), "source/assets/src/shaders/generated/*.cpp")))
        table.join2(inputs, os.files(path.join(os.projectdir(), "source/assets/src/shaders/generated/*.h")))
        table.join2(inputs, os.files(path.join(os.projectdir(), "source/drivers/src/rhi/shader*")))
        table.join2(inputs, os.files(path.join(os.projectdir(), "source/assets/include/vultra/assets/shader_asset.hpp")))
        table.join2(inputs, os.files(path.join(os.projectdir(), "source/drivers/include/vultra/drivers/rhi/shader_program.hpp")))
        table.sort(inputs)
        local hashes = {"antlr-4.13.2", "vultra-shader-language-1"}
        for _, file in ipairs(inputs) do
            table.insert(hashes, path.relative(file, os.projectdir()):gsub("\\", "/") .. ":" .. hash.sha256(file))
        end
        local output = path.join(os.projectdir(), get_config("builddir") or "build",
                                 "generated/shaders/shader_toolchain_key.hpp")
        -- Only the shader compiler includes this private header. Ordinary builds never run Java.
        local contents = '#pragma once\ninline constexpr char kShaderToolchainKey[] = "' ..
                         table.concat(hashes, ";") .. '";\n'
        if not os.isfile(output) or io.readfile(output) ~= contents then
            io.writefile(output, contents)
        end
    end)

    if has_config("libvultra_with_openxr") then
        add_defines("VULTRA_WITH_OPENXR", {public = true})
        add_files("drivers/src/openxr/**.cpp")
        add_packages(is_plat("linux") and "openxr-static" or "openxr", "vulkan-headers", {public = true})
    end
target_end()

-- Compile the shared managed API once before either example or test script assemblies.
target("vultra-managed-host")
    set_kind("phony")
    set_default(false)
    set_policy("build.fence", true)
    on_build(function ()
        local output = path.join(os.projectdir(), "build", ".tmp", "scripting-managed")
        os.execv("dotnet", {"build", path.join(os.projectdir(), "source", "scripting", "managed", "Vultra.ManagedHost.csproj"),
                           "-c", "Release", "-o", output, "--nologo"})
    end)
target_end()

-- Authored in-game UI is opt-in; applications that only use VRI keep one vultra dependency.
target("vultra-vgui")
    set_kind("static")
    set_default(false)
    add_deps("vultra")
    add_includedirs("ui/include", {public = true})
    add_headerfiles("ui/include/vultra/ui/vgui.hpp")
    add_rules("utils.bin2c", {extensions = ".png"})
    add_files("ui/assets/vgui_skin.png", "ui/src/vgui.cpp")
    add_packages("rmlui", {public = true})
target_end()

-- Project scripting is optional. The runtime links it statically, while C++ experiments may omit it.
target("vultra-scripting")
    set_kind("static")
    set_default(false)
    if is_plat("linux", "macosx") then
        add_cxflags("-fPIC")
    end
    add_deps("vultra")
    add_includedirs("scripting/include", {public = true})
    add_headerfiles("scripting/include/(vultra/**.hpp)")
    add_files("scripting/src/**.cpp")
    add_packages("lua", {public = true})
    add_packages("nlohmann_json")
target_end()
